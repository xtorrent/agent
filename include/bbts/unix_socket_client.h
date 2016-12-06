/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   unix_socket_client.h
 *
 * @author liuming03
 * @date   2014-1-22
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_UNIX_SOCKET_CLIENT_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_UNIX_SOCKET_CLIENT_H

#include <boost/asio/io_service.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include "bbts/unix_socket_connection.h"

namespace bbts {

/**
 * @brief
 */
class UnixSocketClient : public boost::noncopyable {
public:
    typedef boost::function<
            void(const boost::shared_ptr<UnixSocketConnection> &)> ConnectedCallback;

    UnixSocketClient(boost::asio::io_service &ios);
    ~UnixSocketClient();

    bool start(UnixSocketConnection::EndPoint endpoint);

    void asyn_connect(UnixSocketConnection::EndPoint endpoint);

    void set_connect_callback(ConnectedCallback connected_callback) {
        _connected_callback = connected_callback;
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
    void handle_connected(
            boost::shared_ptr<UnixSocketConnection> connection,
            const boost::system::error_code& ec);

    boost::asio::io_service &_io_service;
    int _heartbeat_recv_cycle;
    int _heartbeat_send_cycle;
    ConnectedCallback _connected_callback;
    UnixSocketConnection::RWCallback _read_callback;
    UnixSocketConnection::RWCallback _write_callback;
    UnixSocketConnection::CloseCallback _close_callback;
};

class SyncUnixSocketClient : public boost::noncopyable {
public:
    SyncUnixSocketClient(boost::asio::io_service &ios);
    ~SyncUnixSocketClient();
    bool connect(const UnixSocketConnection::EndPoint &endpoint);
    bool write_data(const boost::shared_ptr<const std::vector<char> > &data);
    bool read_data(boost::shared_ptr<std::vector<char> > *data);
    void close();

    UnixSocketConnection::Socket& get_socket() {
        return _socket;
    }

private:
    boost::asio::io_service &_io_service;
    UnixSocketConnection::Socket _socket;
};

}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_UNIX_SOCKET_CLIENT_H
