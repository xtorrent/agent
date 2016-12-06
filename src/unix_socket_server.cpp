/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   unix_socket_server.cpp
 *
 * @author liuming03
 * @date   2014-1-11
 * @brief 
 */

#include "bbts/unix_socket_server.h"

#include <boost/bind.hpp>
#include <boost/system/error_code.hpp>

#include "bbts/log.h"

namespace bbts {

using std::string;

using boost::asio::io_service;
using boost::system::error_code;
using boost::shared_ptr;

static void empty_callback() {}

UnixSocketServer::UnixSocketServer(io_service &io_service) :
        _io_service(io_service),
        _acceptor(_io_service),
        _heartbeat_recv_cycle(0),
        _heartbeat_send_cycle(0),
        _accept_callback(boost::bind(&empty_callback)),
        _read_callback(boost::bind(&empty_callback)),
        _write_callback(boost::bind(&empty_callback)),
        _close_callback(boost::bind(&empty_callback)) {}

UnixSocketServer::~UnixSocketServer() {
    close();
}

void UnixSocketServer::close() {
    if (_acceptor.is_open()) {
        unlink(_endpoint.path().c_str());
    }
    error_code ec;
    _acceptor.close(ec);
}

void UnixSocketServer::handle_accepted(
        shared_ptr<UnixSocketConnection> connection,
        const error_code& ec) {
    if (ec) {
        if (ec == boost::asio::error::make_error_code(boost::asio::error::operation_aborted)) {
            DEBUG_LOG("server(%s) accept canceled.", _endpoint.path().c_str());
            return;
        } else {
            WARNING_LOG("server(%s) accept failed: %s",
                    _endpoint.path().c_str(), ec.message().c_str());
        }
        if (!_acceptor.is_open()) {
            return;
        }
    } else {
        _accept_callback(connection);
        connection->start();
    }
    async_accept();
}

void UnixSocketServer::async_accept() {
    shared_ptr<UnixSocketConnection> connection = UnixSocketConnection::create(
            _io_service,
            _read_callback,
            _write_callback,
            _close_callback,
            _heartbeat_recv_cycle,
            _heartbeat_send_cycle);
    _acceptor.async_accept(
            connection->get_socket(),
            connection->get_remote_endpoint(),
            boost::bind(&UnixSocketServer::handle_accepted, this, connection, _1));
}

bool UnixSocketServer::can_connect() {
    error_code ec;
    Socket sock(_io_service);
    sock.connect(_endpoint, ec);
    if (ec) {
        unlink(_endpoint.path().c_str());
        return false;
    }
    return true;
}

bool UnixSocketServer::serve(mode_t mode) {
    if (can_connect()) {
        WARNING_LOG("bind address(%s) can connect, can't start serve.", _endpoint.path().c_str());
        return false;
    }

    error_code ec;
    _acceptor.open(Acceptor::protocol_type(), ec);
    if (ec) {
        WARNING_LOG("acceptor open failed: %s", ec.message().c_str());
        return false;
    }

    _acceptor.bind(_endpoint, ec);
    if (ec) {
        WARNING_LOG("bind path(%s) failed: %s", _endpoint.path().c_str(), ec.message().c_str());
        return false;
    }
    chmod(_endpoint.path().c_str(), mode);

    Acceptor::non_blocking_io non_block(true);
    _acceptor.io_control(non_block, ec);
    if (ec) {
        WARNING_LOG("set socket(%s) non blocking io failed.", _endpoint.path().c_str());
        return false;
    }
     _acceptor.listen(128, ec);
    if (ec) {
        WARNING_LOG("listen socket(%s) failed: %s", _endpoint.path().c_str(), ec.message().c_str());
        return false;
    }

    async_accept();
    return true;
}

bool UnixSocketServerWithThread::start(mode_t mode) {
    if (!_server.serve(mode)) {
        return false;
    }
    boost::thread tmp_thread(boost::bind(&UnixSocketServerWithThread::run, this));
    _thread.swap(tmp_thread);
    return true;
}

void UnixSocketServerWithThread::join() {
    _thread.join();
}

void UnixSocketServerWithThread::run() {
    OPEN_LOG_R();
    DEBUG_LOG("Unix socket server start success.");
    _io_service.reset();
    _io_service.run();
    DEBUG_LOG("Unix socket server stoped success.");
    CLOSE_LOG_R();
}

}  // namespace bbts
