/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   tcp_connection.cpp
 *
 * @author liuming03
 * @date   2015-2-5
 * @brief 
 */

#include "tcp_connection.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <sstream>
#include <vector>

#include <boost/array.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>
#include <boost/system/error_code.hpp>

#include <libtorrent/bt_peer_connection.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/lazy_entry.hpp>
#include <libtorrent/hasher.hpp>

#include "bbts/config.h"
#include "bbts/string_util.h"

#include "bbts/log.h"
#include "io.hpp"
#include "uri_piece_request.h"
#include "disk_manager.h"
#include "tcp_server.h"
#include "torrent_generator.h"

using std::vector;
using boost::posix_time::seconds;
using boost::system::error_code;
using boost::array;
using boost::shared_ptr;
using boost::asio::async_read;
using boost::asio::async_write;
using boost::asio::buffer;
using boost::bind;

namespace error = boost::asio::error;

namespace bbts {

const std::string TcpConnection::kTorrentProviderInfohash = "INFOHASH_FORPROVIDER";

TcpConnection::TcpConnection(boost::asio::io_service &ios, int64_t id) :
        _ios(ios),
        _id(id),
        _socket(_ios),
        _read_state(READ_BEGIN),
        _is_connection_counter_deleted(false),
        _is_write_in_waiting(false),
        _wait_write_size(0),
        _upload_quota(0),
        _total_upload_per_unit(0),
        _piece_length(0),
        _is_provider_request(false),
        _active_timeout(kTorrentActiveTimeout) {
    _tick_timestamp = g_tcp_server->current_timestamp();
}

TcpConnection::~TcpConnection() {
    // close maybe a bug for double release
    // close();
    error_code ec;
    DEBUG_LOG("peer connection[%s:%d] close",
            _remote.address().to_string(ec).c_str(), _remote.port());
}

void TcpConnection::close() {
    boost::system::error_code ec;
    _socket.close(ec);
    if (!_is_connection_counter_deleted) {
        // only delete once, thus tcp_server counter must be correct
        _is_connection_counter_deleted = true;
        g_tcp_server->del_connection(shared_from_this());
    }
}

void TcpConnection::write_data_cb(const error_code &ec, size_t bytes_transferred) {
    if (ec) {
        DEBUG_LOG("write data to remote(%s): %s",
                StringUtil::to_string(_remote).c_str(), ec.message().c_str());
        close();
        return;
    }

    _wait_write_size -= bytes_transferred;
    g_tcp_server->del_wait_by_infohash(_infohash, bytes_transferred);

    if (_write_buffers.empty()) {
        return;
    }

    WriteBuffer &write_buffer = _write_buffers.front();
    if (static_cast<size_t>(write_buffer.current_offset)
        == write_buffer.buffer->size()) {
        _write_buffers.pop();
    }
    if (!_write_buffers.empty()) {
        get_quota_and_asnyc_write();
    }
}

void TcpConnection::async_write_data(const shared_ptr<Buffer> &data) {
    bool is_writting = !_write_buffers.empty();
    WriteBuffer write_buffer;
    write_buffer.buffer = data;
    _wait_write_size += data->size();
    g_tcp_server->add_wait_by_infohash(_infohash, data->size());
    _write_buffers.push(write_buffer);
    if (!is_writting) {
        get_quota_and_asnyc_write();
    }
}

void TcpConnection::write_data(const shared_ptr<Buffer> &data) {
    if (data) {
        _ios.post(boost::bind(&TcpConnection::async_write_data, shared_from_this(), data));
    }
}

int64_t TcpConnection::get_quota(int64_t require_quota) {
    if (require_quota <= 0) {
        return 0;
    }

    if (_upload_quota < 0) {
        // no limit
        return require_quota;
    } else if (_upload_quota == 0) {
        // no quota left
        return 0;
    }

    if (require_quota > kMaxSendBytesPerPeriod) {
        require_quota = kMaxSendBytesPerPeriod;
    }

    int64_t current_left = _upload_quota - _total_upload_per_unit;
    if (current_left <= 0) {
        // no enough quota
        return 0;
    }

    int64_t diff = 0;
    if (current_left >= require_quota) {
        diff = require_quota;
    } else {
        diff = current_left;
    }
    _total_upload_per_unit += diff;
    g_tcp_server->add_total_upload_per_unit_by_infohash(_infohash, diff);

    return diff;
}

void TcpConnection::get_quota_and_asnyc_write() {
    if (_write_buffers.empty()) {
        return;
    }

    WriteBuffer &write_buffer = _write_buffers.front();
    char *ptr = &(*write_buffer.buffer)[0];
    int64_t require_quota = write_buffer.buffer->size() - write_buffer.current_offset;
    int64_t quota = get_quota(require_quota);
    if (quota >= require_quota) {
        async_write(_socket, buffer(ptr + write_buffer.current_offset, require_quota),
                bind(&TcpConnection::write_data_cb, shared_from_this(), _1, _2));
        write_buffer.current_offset = write_buffer.buffer->size();
        _is_write_in_waiting = false;
    } else if (quota > 0) {
        async_write(_socket, buffer(ptr + write_buffer.current_offset, quota),
                bind(&TcpConnection::write_data_cb, shared_from_this(), _1, _2));
        write_buffer.current_offset += quota;
        _is_write_in_waiting = false;
    } else if (!_is_write_in_waiting) {
        g_tcp_server->add_connection_to_wait(shared_from_this(), require_quota);
        _is_write_in_waiting = true;
    }
}

int TcpConnection::get_request_from_buffer(
        Buffer::iterator i,
        Buffer::iterator end,
        URIPieceRequest *request) {
    assert(request != NULL);
    if (end - i < 8) {
        TRACE_LOG("invalid uri piece request");
        close();
        return -1;
    }
    request->piece_index = read_uint32(i);
    request->piece_offset = read_uint32(i);
    request->piece_length = _piece_length;

    if (!_root_path.empty()) {
        request->root_path = _root_path;
    } else {
        request->root_path = read_string(i, end);
    }

    while (i != end) {
        FilePiece file_piece;
        file_piece.name = read_string(i, end);
        if (end - i < 12) {
            TRACE_LOG("invalid uri request");
            close();
            return -1;
        }
        file_piece.offset = read_uint64(i);
        file_piece.size = read_uint32(i);
        request->size += file_piece.size;
        request->file_pieces.push_back(file_piece);
    };
    if (request->file_pieces.empty()) {
        TRACE_LOG("invalid uri request");
        close();
        return -1;
    }
    request->infohash = _infohash;
    return 0;
}

void TcpConnection::on_uri_piece_request(Buffer::iterator i, Buffer::iterator end) {
    URIPieceRequest request;
    if (get_request_from_buffer(i, end, &request) != 0) {
        return;
    }

    g_disk_manager->async_piece_request(shared_from_this(), request);
}

void TcpConnection::on_uri_piece_hash_request(Buffer::iterator i, Buffer::iterator end) {
    URIPieceRequest request;
    if (get_request_from_buffer(i, end, &request) != 0) {
        return;
    }

    g_disk_manager->async_piece_hash_request(shared_from_this(), request);
}

void TcpConnection::on_uri_message(uint8_t type, Buffer::iterator i, Buffer::iterator end) {
    switch (type) {
    case MESSAGE_URI_PIECE_REQUEST:
        on_uri_piece_request(i, end);
        break;
    case MESSAGE_URI_PIECE_HASH_REQUEST:
        on_uri_piece_hash_request(i, end);
        break;
    default:
        break;
    }
}

void TcpConnection::on_bt_plugin_message(uint8_t type, Buffer::iterator i, Buffer::iterator end) {
    switch (type) {
    case URI_PLUGIN_TYPE: {
        if (i == end) {
            TRACE_LOG("empty uri message");
            close();
            return;
        }
        uint8_t uri_message_type = read_uint8(i);
        on_uri_message(uri_message_type, i, end);
        break;
    }

    case EXTEND_HANDSHAKE_TYPE:
        on_extend_handshake(i, end);
        break;

    case URI_GENERATE_TYPE: {
        std::string path = read_string(i, end);
        g_torrent_generator->async_get_torrent(shared_from_this(), path);
        break;
    }

    case URI_CHECK_TIMESTAMP_TYPE: {
        std::string path = read_string(i, end);
        g_torrent_generator->async_get_timestamp(shared_from_this(), path);
        break;
    }

    case URI_CHECK_PATH: {
        std::string path = read_string(i, end);
        g_torrent_generator->async_check_path(shared_from_this(), path);
        break;
    }

    default:
        break;
    }
}

void TcpConnection::on_extend_handshake(Buffer::iterator i, Buffer::iterator end) {
    std::string entry_code = read_string(i, end);
    int pos = 0;
    error_code ec;
    libtorrent::lazy_entry root;
    int ret = libtorrent::lazy_bdecode(&entry_code[0], &entry_code[0] + entry_code.size(), root, ec, &pos, 100, 100000);
    if (ret != 0 || ec || root.type() != libtorrent::lazy_entry::dict_t) {
        WARNING_LOG("invalid extend handshake:%s, pos:%s, entry_code:%s",
                ec.message().c_str(), pos, entry_code.c_str());
        close();
        return;
    }

    libtorrent::lazy_entry const *m = root.dict_find_dict("m");
    if (m != NULL) {
        _piece_length = m->dict_find_int_value("piece_length", 0);
        _root_path = m->dict_find_string_value("root_path");
    }
}

void TcpConnection::on_bt_message(uint8_t type, Buffer::iterator i, Buffer::iterator end) {
    switch (type) {
    case MSG_EXTENDED: {
        if (i == end) {
            TRACE_LOG("empty extended message");
            close();
            break;
        }
        uint8_t plugin_type = read_uint8(i);
        on_bt_plugin_message(plugin_type, i, end);
        break;
    }

    case MSG_CANCEL:
        break;

    default:
        break;
    }
}

void TcpConnection::read_cb(const error_code &ec, size_t readed) {
    if (ec) {
        DEBUG_LOG("read from remote(%s): %s",
                StringUtil::to_string(_remote).c_str(), ec.message().c_str());
        close();
        return;
    }

    on_keep_alive();

    assert(readed == _recv_buffer.size());
    vector<char>::iterator i = _recv_buffer.begin();
    switch (_read_state) {
    case READ_BEGIN:
        _recv_buffer.resize(68);
        _read_state = READ_HANDSHAKE;
        break;

    case READ_HANDSHAKE:
        _infohash.assign(_recv_buffer.begin() + 28, _recv_buffer.begin() + 48);
        if (_infohash == kTorrentProviderInfohash) {
            _is_provider_request = true;
            _active_timeout = kProviderActiveTimeout;
        }
        char ih[41];
        libtorrent::to_hex((char const*)&_infohash.c_str()[0], 20, ih);
        _infohash.assign(ih);
        g_tcp_server->init_infohash_upload_limit(_infohash);
        _peer_id.assign(_recv_buffer.begin() + 48, _recv_buffer.end());
        write_handshake(_recv_buffer);
        _read_state = READ_MSG_LENGTH;
        _recv_buffer.resize(4);
        break;

    case READ_MSG_LENGTH: {
        uint32_t msg_length = read_uint32(i);
        if (msg_length == 0) {
            break;
        }
        if (msg_length > 1024 * 1024) {
            WARNING_LOG("message too large: %u", msg_length);
            close();
            return;
        }
        _read_state = READ_MSG;
        _recv_buffer.resize(msg_length);
        break;
    }

    case READ_MSG: {
        uint8_t type = read_uint8(i);
        on_bt_message(type, i, _recv_buffer.end());
        _read_state = READ_MSG_LENGTH;
        _recv_buffer.resize(4);
        break;
    }

    default:
        assert(false);
        break;
    }

    async_read(_socket, buffer(_recv_buffer),
            bind(&TcpConnection::read_cb, shared_from_this(), _1, _2));
}

void TcpConnection::on_keep_alive() {
    _tick_timestamp = g_tcp_server->current_timestamp();
}

void TcpConnection::write_failed_message(const std::string &message) {
    Buffer msg_buffer;
    std::back_insert_iterator<Buffer> ptr(msg_buffer);
    write_uint32(0, ptr);
    write_uint8(libtorrent::bt_peer_connection::msg_extended, ptr);
    write_uint8(MSG_EXTENDED, ptr);
    write_uint8(MESSAGE_URI_PIECE_FAILED, ptr);
    write_string(message, ptr);
    char *p = &msg_buffer[0];
    write_uint32(msg_buffer.size() - 4, p);
    shared_ptr<Buffer> send_buffer(new Buffer(msg_buffer.begin(), msg_buffer.end()));
    async_write_data(send_buffer);
}

void TcpConnection::async_write_piece_hash(
        uint32_t piece,
        const shared_ptr<Buffer> &hash) {
    if (hash) {
        _ios.post(boost::bind(&TcpConnection::write_piece_hash, shared_from_this(), piece, hash));
    }
}

void TcpConnection::write_piece_hash(uint32_t piece, const shared_ptr<Buffer> &hash) {
    Buffer msg_buffer;
    std::back_insert_iterator<Buffer> ptr(msg_buffer);
    write_uint32(0, ptr);
    write_uint8(libtorrent::bt_peer_connection::msg_extended, ptr);
    write_uint8(MSG_EXTENDED, ptr);
    write_uint8(MESSAGE_URI_PIECE_HASH_RESPONSE, ptr);
    write_uint32(piece, ptr);

    std::copy(hash->begin(), hash->end(), ptr);
    char *p = &msg_buffer[0];
    write_uint32(msg_buffer.size() - 4, p);
    shared_ptr<Buffer> send_buffer(new Buffer(msg_buffer.begin(), msg_buffer.end()));
    async_write_data(send_buffer);
}

void TcpConnection::async_write_torrent_message(
        const std::string &message) {
    if (message.empty()) {
        return;
    }
    _ios.post(boost::bind(&TcpConnection::write_torrent_message, shared_from_this(), message));
}

void TcpConnection::write_torrent_message(
        const std::string &message) {
    Buffer msg_buffer;
    std::back_insert_iterator<Buffer> ptr(msg_buffer);
    write_string(message, ptr);
    shared_ptr<Buffer> send_buffer(new Buffer(msg_buffer.begin(), msg_buffer.end()));
    async_write_data(send_buffer);
}

void TcpConnection::write_handshake(const Buffer &handshake) {
    shared_ptr<Buffer> buffer(new Buffer(handshake.begin(), handshake.end()));
    (*buffer)[50] = 'a';
    async_write_data(buffer);
}

void TcpConnection::start() {
    read_cb(error_code(), 0);
}

std::string TcpConnection::to_string() {
    std::stringstream ss;
    ss << "id:" << _id;
    if (_infohash.empty()) {
        ss << ", infohash:empty";
    } else {
        ss << ", infohash:" << _infohash;
    }

    if (_peer_id.empty()) {
        ss << ", peer_id:empty";
    } else {
        ss << ", peer_id:" << _peer_id;
    }

    ss << ", remote:" << StringUtil::to_string(_remote);

    return ss.str();
}

}  // namespace bbts
