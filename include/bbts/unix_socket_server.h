/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file unix_socket_server.h
 *
 * @author liuming03
 * @date 2014-1-11
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_UNIX_SOCKET_SERVER_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_UNIX_SOCKET_SERVER_H

#include <boost/asio/io_service.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/system/error_code.hpp>
#include <boost/thread/thread.hpp>

#include "unix_socket_connection.h"

namespace bbts {

/**
 * @brief
 */
class UnixSocketServer : public boost::noncopyable {
public:
    typedef boost::asio::local::stream_protocol::socket Socket;
    typedef boost::asio::local::stream_protocol::acceptor Acceptor;
    typedef boost::function<void(const boost::shared_ptr<UnixSocketConnection> &)> AcceptedCallback;

    UnixSocketServer(boost::asio::io_service &io_service);

    ~UnixSocketServer();

    bool serve(mode_t mode);
    void close();

    boost::asio::io_service& get_io_service() {
        return _io_service;
    }

    void set_endpoint(const UnixSocketConnection::EndPoint &endpoint) {
        _endpoint = endpoint;
    }

    void set_accept_callback(AcceptedCallback accept_callback) {
        _accept_callback = accept_callback;
    }

    void set_read_callback(UnixSocketConnection::RWCallback read_callback) {
        _read_callback = read_callback;
    }

    void set_write_callback(UnixSocketConnection::RWCallback write_callback) {
        _write_callback = write_callback;
    }

    void set_close_callback(UnixSocketConnection::CloseCallback close_callback) {
        _close_callback = close_callback;
    }

    void set_heartbeat_recv_cycle(int cycle) {
        _heartbeat_recv_cycle = cycle;
    }

    void set_heartbeat_send_cycle(int cycle) {
        _heartbeat_send_cycle = cycle;
    }

private:
    void async_accept();

    bool can_connect();

    void handle_accepted(boost::shared_ptr<UnixSocketConnection> conn,
            const boost::system::error_code& ec);

    UnixSocketConnection::EndPoint _endpoint;
    boost::asio::io_service &_io_service;
    Acceptor _acceptor;
    int _heartbeat_recv_cycle;
    int _heartbeat_send_cycle;
    AcceptedCallback _accept_callback;
    UnixSocketConnection::RWCallback _read_callback;
    UnixSocketConnection::RWCallback _write_callback;
    UnixSocketConnection::CloseCallback _close_callback;
};

class UnixSocketServerWithThread : public boost::noncopyable {
public:
    UnixSocketServerWithThread() : _server(_io_service) {}

    ~UnixSocketServerWithThread() {}

    bool start(mode_t mode);

    void join();

    void stop() {
        _server.close();
        _io_service.stop();
    }

    boost::asio::io_service& get_io_service() {
        return _io_service;
    }

    void set_endpoint(const UnixSocketConnection::EndPoint &endpoint) {
        _server.set_endpoint(endpoint);
    }

    void set_accept_callback(UnixSocketServer::AcceptedCallback accept_callback) {
        _server.set_accept_callback(accept_callback);
    }

    void set_read_callback(UnixSocketConnection::RWCallback read_callback) {
        _server.set_read_callback(read_callback);
    }

    void set_write_callback(UnixSocketConnection::RWCallback write_callback) {
        _server.set_write_callback(write_callback);
    }

    void set_close_callback(UnixSocketConnection::CloseCallback close_callback) {
        _server.set_close_callback(close_callback);
    }

    void set_heartbeat_recv_cycle(int cycle) {
        _server.set_heartbeat_recv_cycle(cycle);
    }

    void set_heartbeat_send_cycle(int cycle) {
        _server.set_heartbeat_send_cycle(cycle);
    }

private:
    void run();

    boost::asio::io_service _io_service;
    UnixSocketServer _server;
    boost::thread _thread;
};

}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_UNIX_SOCKET_SERVER_H
