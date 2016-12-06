/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file tcp_connection.h
 *
 * @author liuming03
 * @date 2014-2-5
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_TCP_CONNECTION_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_TCP_CONNECTION_H

#include <queue>
#include <vector>

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/thread/thread.hpp>

#include "uri_piece_request.h"

namespace bbts {

typedef boost::asio::ip::tcp Tcp;

/**
 * @brief
 */
class TcpConnection :
        public boost::enable_shared_from_this<TcpConnection>,
        public boost::noncopyable {
public:
    friend class TcpServer;

    ~TcpConnection();

    static boost::shared_ptr<TcpConnection> create(boost::asio::io_service &ios, int64_t id) {
        return boost::shared_ptr<TcpConnection>(new TcpConnection(ios, id));
    }

    const Tcp::socket& get_socket() const {
        return _socket;
    }

    const Tcp::endpoint& get_remote() const {
        return _remote;
    }

    void start();

    void write_data(const boost::shared_ptr<Buffer> &data);

    void get_quota_and_asnyc_write();

    void write_failed_message(const std::string &message);

    void async_write_piece_hash(uint32_t piece, const boost::shared_ptr<Buffer> &hash);

    void async_write_torrent_message(const std::string &message);

    void close();

    const int64_t id() const {
        return _id;
    }

    const time_t tick_timestamp() const {
        return _tick_timestamp;
    }

    const std::string& infohash() const {
        return _infohash;
    }

    const int64_t wait_write_size() const {
        return _wait_write_size;
    }

    const int64_t total_upload_per_unit() const {
        return _total_upload_per_unit;
    }

    const int64_t upload_quota() const {
        return _upload_quota;
    }

    const bool is_provider_request() const {
        return _is_provider_request;
    }

    const int active_timeout() const {
        return _active_timeout;
    }

    void set_upload_quota(int64_t quota) {
        if (quota < kMinQuotaData) {
            quota = kMinQuotaData;
        }
        _upload_quota = quota;
    }

    void set_total_upload_per_unit(int64_t total) {
        _total_upload_per_unit = total;
    }

    std::string to_string();

private:
    TcpConnection(boost::asio::io_service &io_service, int64_t id);

    enum ReadStateEnum {
        READ_BEGIN,
        READ_HANDSHAKE,
        READ_MSG_LENGTH,
        READ_MSG,
    };

    void read_cb(const boost::system::error_code &ec, size_t readed);

    enum {
        MSG_KEEP_ALIVE = -1,
        MSG_CHOKE = 0,
        MSG_UNCHOKE = 1,
        MSG_INTERESTED = 2,
        MSG_NOT_INTERESTED = 3,
        MSG_HAVE = 4,
        MSG_BITFIELD = 5,
        MSG_REQUEST = 6,
        MSG_PIECE = 7,
        MSG_CANCEL = 8, // TODO
        MSG_PORT = 9,
        MSG_EXTENDED = 20,
    };
    void on_bt_message(uint8_t type, Buffer::iterator i, Buffer::iterator end);

    enum {
        EXTEND_HANDSHAKE_TYPE = 0,
        URI_PLUGIN_TYPE = 20,
        URI_GENERATE_TYPE = 30,
        URI_CHECK_TIMESTAMP_TYPE = 31,
        URI_CHECK_PATH = 32,
    };
    void on_bt_plugin_message(uint8_t type, Buffer::iterator i, Buffer::iterator end);

    enum {
        MESSAGE_URI_PIECE_REQUEST = 1,
        MESSAGE_URI_PIECE_FAILED = 2,
        MESSAGE_URI_PIECE_HASH_REQUEST  = 3,
        MESSAGE_URI_PIECE_HASH_RESPONSE  = 4,
    };
    void on_uri_message(uint8_t type, Buffer::iterator i, Buffer::iterator end);

    void on_uri_piece_request(Buffer::iterator i, Buffer::iterator end);
    void on_uri_piece_hash_request(Buffer::iterator i, Buffer::iterator end);

    void on_extend_handshake(Buffer::iterator i, Buffer::iterator end);

    void write_data_cb(const boost::system::error_code &ec, size_t bytes_transferred);

    void async_write_data(const boost::shared_ptr<Buffer> &data);

    void write_handshake(const Buffer &handshake);

    void write_piece_hash(uint32_t piece, const boost::shared_ptr<Buffer> &hash);

    void on_keep_alive();
        
    void write_torrent_message(const std::string &message);

    int get_request_from_buffer(Buffer::iterator i, Buffer::iterator end, URIPieceRequest *request);

    /**
     * get quota, if upload_limit <= 0, return require_quota direct
     * if no enough quota, return quota left (may be 0)
     */
    int64_t get_quota(int64_t require_quota);

    Tcp::socket& socket() {
        return _socket;
    }

    Tcp::endpoint& remote() {
        return _remote;
    }

    struct WriteBuffer {
        WriteBuffer() : current_offset(0) {}

        int64_t current_offset;
        boost::shared_ptr<Buffer> buffer;
    };

    static const int kMaxSendBytesPerPeriod = 256 * 1024;  // 256K bytes per period send
    static const int64_t kMinQuotaData  = 70;       // handshake need 68 bytes
    static const std::string kTorrentProviderInfohash;
    static const int kTorrentActiveTimeout = 10;  // normal torrent connection active timeout
    static const int kProviderActiveTimeout = 30;  // torrent-provider connection active timeout

    boost::asio::io_service &_ios;
    int64_t _id;    // unique connection id
    Tcp::socket _socket;
    Tcp::endpoint _remote;
    Buffer _recv_buffer;
    ReadStateEnum _read_state;
    std::queue<WriteBuffer> _write_buffers;
    std::queue<boost::shared_ptr<Buffer> > _write_cmds;
    std::string _infohash;
    std::string _peer_id;
    bool _is_connection_counter_deleted;
    bool _is_write_in_waiting;
    time_t _tick_timestamp;
    int64_t _wait_write_size;
    int64_t _upload_quota;
    int64_t _total_upload_per_unit;
    int64_t _piece_length;
    std::string _root_path;
    bool _is_provider_request;
    int _active_timeout;
};

}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_TCP_CONNECTION_H
