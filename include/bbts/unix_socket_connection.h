/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file unix_socket_connection.h
 *
 * @author liuming03
 * @date 2014-1-11
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_UNIX_SOCKET_CONNECTION_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_UNIX_SOCKET_CONNECTION_H

#include <queue>
#include <utility> // for std::pair
#include <vector>

#include <boost/any.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <google/protobuf/message.h>

struct ucred;

namespace bbts {

class Header {
public:
    Header() : _length(0), _type(0), _magic(0), _checksum(0) {}

    Header(uint32_t type, uint32_t length) {
        assign(type, length);
    }

    bool is_valid() {
        return (_length ^ _type ^ _magic) == _checksum;
    }

    void assign(uint32_t type, uint32_t length) {
        _length = length;
        _type = type;
        _magic = 47417;
        _checksum = length ^ type ^ _magic;
    }

    uint32_t type() {
        return _type;
    }

    uint32_t length() {
        return _length;
    }

private:
    uint32_t _length;
    uint32_t _type;
    uint32_t _magic;
    uint32_t _checksum;
};

/**
 * @brief
 */
class UnixSocketConnection :
        public boost::enable_shared_from_this<UnixSocketConnection>,
        public boost::noncopyable {
public:
    enum MessageType {
        HEARTBEAT,
        USERDATA,
    };

    typedef boost::asio::local::stream_protocol::socket Socket;
    typedef boost::asio::local::stream_protocol::endpoint EndPoint;
    typedef std::pair<MessageType, boost::shared_ptr<const std::vector<char> > > Message;
    typedef boost::function<void(const EndPoint &)> CloseCallback;
    typedef boost::function<void(const boost::shared_ptr<UnixSocketConnection> &,
            const boost::shared_ptr<const std::vector<char> > &)> RWCallback;

    ~UnixSocketConnection();

    static boost::shared_ptr<UnixSocketConnection> create(
            boost::asio::io_service &io_service,
            RWCallback read_callback,
            RWCallback write_callback,
            CloseCallback close_callback,
            int heartbeat_recv_cycle,
            int heartbeat_send_cycle) {
        return boost::shared_ptr<UnixSocketConnection>(
                new UnixSocketConnection(
                        io_service,
                        read_callback,
                        write_callback,
                        close_callback,
                        heartbeat_recv_cycle,
                        heartbeat_send_cycle));
    }

    Socket& get_socket() {
        return _socket;
    }

    EndPoint& get_remote_endpoint() {
        return _remote_endpoint;
    }

    void start();

    void write(const boost::shared_ptr<const std::vector<char> > &data);

    void close() {
        boost::system::error_code ec;
        _heartbeat_recv_timer.cancel(ec);
        _heartbead_send_timer.cancel(ec);
        _socket.close(ec);
    }

    void set_user_argument(const boost::any &user_argument) {
        _user_argument = user_argument;
    }

    const boost::any& get_user_argument() const {
        return _user_argument;
    }

private:
    UnixSocketConnection(
            boost::asio::io_service &io_service,
            RWCallback read_callback,
            RWCallback write_callback,
            CloseCallback close_callback_,
            int heartbeat_recv_cycle,
            int heartbeat_send_cycle);

    void handle_read_data(
            boost::shared_ptr<std::vector<char> > data,
            const boost::system::error_code &ec,
            size_t bytes_readed);

    void write_message(const Message &message);
    void handle_read_head(const boost::system::error_code &ec, size_t bytes_transferred);
    void handle_write_message(const boost::system::error_code &ec, size_t bytes_transferred);
    void async_write(MessageType type, boost::shared_ptr<const std::vector<char> > data);
    void handle_heartbeat_recv(const boost::system::error_code &ec);
    void handle_heartbeat_send(const boost::system::error_code &ec);
    void update_heartbeat_recv_timer();
    void update_heartbeat_send_timer();

    boost::asio::io_service &_io_service;
    Socket _socket;
    EndPoint _remote_endpoint;
    RWCallback _read_callback;
    RWCallback _write_callback;
    CloseCallback _close_callback;
    Header _read_header;
    Header _write_header;
    std::queue<Message> _write_message_queue;
    int _heartbeat_recv_cycle;
    int _heartbeat_send_cycle;
    boost::asio::deadline_timer _heartbeat_recv_timer;
    boost::asio::deadline_timer _heartbead_send_timer;

    boost::any _user_argument;
};

bool write_message(
        const boost::shared_ptr<UnixSocketConnection> &connection,
        uint32_t type,
        const google::protobuf::Message &response);

/**
 * @brief 发送认证信息给对端
 *
 * @param fd          已连接的unix sock
 * @return          成功返回0，失败返回-1
 */
bool send_cred(int fd);

/**
 * @brief 从对端接收认证信息
 *
 * @param fd          已连接的unix sock
 * @return          成功返回0，失败返回-1
 */
bool recv_cred(int fd, struct ucred *ucptr);

}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_UNIX_SOCKET_CONNECTION_H
