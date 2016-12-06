/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   unix_socket_client.cpp
 *
 * @author liuming03
 * @date   2014-1-22
 * @brief 
 */

#include "bbts/unix_socket_client.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "bbts/log.h"

namespace bbts {

using std::vector;
using boost::system::error_code;
using boost::array;
using boost::shared_ptr;

static void empty_callback() {}

UnixSocketClient::UnixSocketClient(boost::asio::io_service &io_service) :
        _io_service(io_service),
        _heartbeat_recv_cycle(0),
        _heartbeat_send_cycle(0),
        _connected_callback(boost::bind(&empty_callback)),
        _read_callback(boost::bind(&empty_callback)),
        _write_callback(boost::bind(&empty_callback)),
        _close_callback(boost::bind(&empty_callback)) {}

UnixSocketClient::~UnixSocketClient() {}

bool UnixSocketClient::start(UnixSocketConnection::EndPoint remote_endpoint) {
    shared_ptr<UnixSocketConnection> connection = UnixSocketConnection::create(
            _io_service,
            _read_callback,
            _write_callback,
            _close_callback,
            _heartbeat_recv_cycle,
            _heartbeat_send_cycle);
    error_code ec;
    UnixSocketConnection::Socket &sock = connection->get_socket();
    sock.open(UnixSocketConnection::Socket::protocol_type(), ec);
    if (ec) {
        WARNING_LOG("open socket failed: %s", ec.message().c_str());
        return false;
    }
    UnixSocketConnection::Socket::non_blocking_io non_block(true);
    sock.io_control(non_block, ec);
    if (ec) {
        WARNING_LOG("set unix socket non blocking io fail.");
        return false;
    }
    UnixSocketConnection::EndPoint &rep = connection->get_remote_endpoint();
    rep = remote_endpoint;
    sock.async_connect(rep, boost::bind(&UnixSocketClient::handle_connected, this, connection, _1));
    return true;
}

void UnixSocketClient::handle_connected(
        shared_ptr<UnixSocketConnection> connection,
        const error_code& ec) {
    if (ec) {
        UnixSocketConnection::EndPoint &rep = connection->get_remote_endpoint();
        if (ec == boost::asio::error::make_error_code(boost::asio::error::operation_aborted)) {
            DEBUG_LOG("connect server(%s) canceled.", rep.path().c_str());
        } else {
            WARNING_LOG("connect server(%s) failed: %s", rep.path().c_str(), ec.message().c_str());
        }
        WARNING_LOG("connect failed: %s", ec.message().c_str());
        return;
    }
    _connected_callback(connection);
    connection->start();
}

SyncUnixSocketClient::SyncUnixSocketClient(boost::asio::io_service &io_service) :
        _io_service(io_service),
        _socket(io_service) {}

SyncUnixSocketClient::~SyncUnixSocketClient() {
    close();
}

bool SyncUnixSocketClient::connect(const UnixSocketConnection::EndPoint &endpoint) {
    error_code ec;
    _socket.connect(endpoint, ec);
    if (ec) {
        DEBUG_LOG("connect to server(%s) failed: %s",
                endpoint.path().c_str(), ec.message().c_str());
    }
    return !ec;
}

bool SyncUnixSocketClient::write_data(const shared_ptr<const vector<char> > &data) {
    if (!data) {
        return true;
    }
    Header header(UnixSocketConnection::USERDATA, data->size());
    array<boost::asio::const_buffer, 2> buffers = {
            boost::asio::buffer(&header, sizeof(header)),
            boost::asio::buffer(*data)
    };
    error_code ec;
    boost::asio::write(_socket, buffers, ec);
    if (ec) {
        DEBUG_LOG("write data to server failed: %s", ec.message().c_str());
    }
    return !ec;
}

bool SyncUnixSocketClient::read_data(shared_ptr<vector<char> > *data) {
    assert(data);
    error_code ec;
    Header header;
    boost::asio::read(_socket, boost::asio::buffer(&header, sizeof(header)), ec);
    if (ec) {
        DEBUG_LOG("read data from server failed: %s", ec.message().c_str());
        return false;
    }
    if (!header.is_valid()) {
        DEBUG_LOG("valid header failed!");
        return false;
    }
    data->reset(new vector<char>(header.length()));
    boost::asio::read(_socket, boost::asio::buffer(*data->get()), ec);
    return !ec;
}

void SyncUnixSocketClient::close() {
    error_code ec;
    _socket.close(ec);
}

}  // namespace bbts
