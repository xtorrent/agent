/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   unix_socket_connection.cpp
 *
 * @author liuming03
 * @date   2014-1-11
 * @brief 
 */

#include "bbts/unix_socket_connection.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <vector>

#include <boost/array.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>
#include <boost/system/error_code.hpp>
#include <google/protobuf/message.h>

#include "bbts/config.h"
#include "bbts/log.h"

using std::vector;
using boost::posix_time::seconds;
using boost::system::error_code;
using boost::array;
using boost::shared_ptr;

namespace error = boost::asio::error;

namespace bbts {

UnixSocketConnection::UnixSocketConnection(
        boost::asio::io_service &io_service,
        RWCallback read_callback,
        RWCallback write_callback,
        CloseCallback close_callback,
        int heartbeat_recv_cycle,
        int heartbeat_send_cycle) :
        _io_service(io_service),
        _socket(_io_service),
        _read_callback(read_callback),
        _write_callback(write_callback),
        _close_callback(close_callback),
        _heartbeat_recv_cycle(heartbeat_recv_cycle),
        _heartbeat_send_cycle(heartbeat_send_cycle),
        _heartbeat_recv_timer(_io_service),
        _heartbead_send_timer(_io_service) {}

UnixSocketConnection::~UnixSocketConnection() {
    close();
    DEBUG_LOG("connection release with remote(%s)", _remote_endpoint.path().c_str());
    _close_callback(_remote_endpoint);
}

void UnixSocketConnection::handle_write_message(const error_code &ec, size_t bytes_transferred) {
    if (ec) {
        if (ec == error::make_error_code(error::operation_aborted)) {
            //CDEBUG_LOG("write data to remote(%s) canceled.", remote_endpoint_.path().c_str());
        } else {
            WARNING_LOG("write data to remote(%s) fail: %s",
                    _remote_endpoint.path().c_str(), ec.message().c_str());
        }
        close();
        return;
    }
    if (_write_message_queue.front().second) {
        _write_callback(shared_from_this(), _write_message_queue.front().second);
    }
    _write_message_queue.pop();
    if (!_write_message_queue.empty()) {
        write_message(_write_message_queue.front());
    }
}

void UnixSocketConnection::write_message(const Message &message) {
    if (message.second) {
        _write_header.assign(message.first, message.second->size());
        array<boost::asio::const_buffer, 2> buffers = {
                boost::asio::buffer(&_write_header, sizeof(_write_header)),
                boost::asio::buffer(*message.second)
        };
        boost::asio::async_write(_socket, buffers, boost::bind(
                &UnixSocketConnection::handle_write_message, shared_from_this(), _1, _2));
        return;
    }

    _write_header.assign(message.first, 0);
    boost::asio::async_write(_socket, boost::asio::buffer(&_write_header, sizeof(_write_header)),
            boost::bind(&UnixSocketConnection::handle_write_message, shared_from_this(), _1, _2));
}

void UnixSocketConnection::async_write(MessageType type, shared_ptr<const vector<char> > data) {
    bool is_writting = !_write_message_queue.empty();
    _write_message_queue.push(std::make_pair(type, data));
    if (!is_writting) {
        write_message(_write_message_queue.front());
    }
}

void UnixSocketConnection::write(const shared_ptr<const vector<char> > &data) {
    if (data) {
        _io_service.post(boost::bind(
                &UnixSocketConnection::async_write, shared_from_this(), USERDATA, data));
    }
}

void UnixSocketConnection::handle_read_data(
        shared_ptr<vector<char> > data,
        const error_code &ec,
        size_t bytes_readed) {
    if (ec) {
        if (ec == error::make_error_code(error::eof)) {
            DEBUG_LOG("connection close by remote(%s): %s", _remote_endpoint.path().c_str());
        }
        if (ec == error::make_error_code(error::operation_aborted)) {
            //CDEBUG_LOG("read data from remote(%s) canceled.", remote_endpoint_.path().c_str());
        } else {
            WARNING_LOG("read data from remote(%s) failed: %s",
                    _remote_endpoint.path().c_str(), ec.message().c_str());
        }
        close();
        return;
    }
    if (data->size() != bytes_readed) {
        WARNING_LOG("read data from remote(%s) fail, length too short.",
                _remote_endpoint.path().c_str());
        close();
        return;
    }

    _read_callback(shared_from_this(), data);
    boost::asio::async_read(_socket, boost::asio::buffer(&_read_header, sizeof(_read_header)),
            boost::bind(&UnixSocketConnection::handle_read_head, shared_from_this(), _1, _2));
}

void UnixSocketConnection::handle_read_head(const error_code &ec, size_t bytes_transferred) {
    if (ec) {
        if (ec == error::make_error_code(error::eof)) {
            DEBUG_LOG("connection close by remote(%s)", _remote_endpoint.path().c_str());
        } else if (ec == error::make_error_code(error::operation_aborted)) {
            //CDEBUG_LOG("read header from remote(%s) canceled.", remote_endpoint_.path().c_str());
        } else {
            WARNING_LOG("read header from remote(%s) failed: %s",
                    _remote_endpoint.path().c_str(), ec.message().c_str());
        }
        close();
        return;
    }
    if (!_read_header.is_valid()) {
        WARNING_LOG("read header from remote(%s) vaild fail",
                _remote_endpoint.path().c_str(), ec.message().c_str());
        close();
        return;
    }

    switch (_read_header.type()) {
    case HEARTBEAT:
        update_heartbeat_recv_timer();
        boost::asio::async_read(_socket, boost::asio::buffer(&_read_header, sizeof(_read_header)),
                boost::bind(&UnixSocketConnection::handle_read_head, shared_from_this(), _1, _2));
        break;

    case USERDATA: {
        shared_ptr<vector<char> > data(new vector<char>(_read_header.length()));
        boost::asio::async_read(_socket, boost::asio::buffer(*data), boost::bind(
                &UnixSocketConnection::handle_read_data, shared_from_this(), data, _1, _2));
        break;
    }

    default:
        WARNING_LOG("head type(%u) not support.", _read_header.type());
        close();
        break;
    }
}

void UnixSocketConnection::handle_heartbeat_recv(const error_code &ec) {
    if (ec) {
        if (ec == error::make_error_code(error::operation_aborted)) {
            //CDEBUG_LOG("remote(%s) heartbeat recv timer canceled.", remote_endpoint_.path().c_str());
        } else {
            WARNING_LOG("remote(%s) heartbeat recv timer failed: %s",
                    _remote_endpoint.path().c_str(), ec.message().c_str());
        }
        return;
    }
    close();
}

void UnixSocketConnection::handle_heartbeat_send(const error_code &ec) {
    if (ec) {
        if (ec == error::make_error_code(error::operation_aborted)) {
            //CDEBUG_LOG("remote(%s) heartbeat send timer canceled.", remote_endpoint_.path().c_str());
        } else {
            WARNING_LOG("remote(%s) heartbeat send timer failed: %s",
                    _remote_endpoint.path().c_str(), ec.message().c_str());
        }
        return;
    }
    async_write(HEARTBEAT, shared_ptr<const vector<char> >());
    update_heartbeat_send_timer();
}

void UnixSocketConnection::update_heartbeat_recv_timer() {
    if (_heartbeat_recv_cycle > 0) {
        error_code ec;
        _heartbeat_recv_timer.cancel(ec);
        _heartbeat_recv_timer.expires_from_now(seconds(_heartbeat_recv_cycle), ec);
        _heartbeat_recv_timer.async_wait(
                boost::bind(&UnixSocketConnection::handle_heartbeat_recv, shared_from_this(), _1));
    }
}

void UnixSocketConnection::update_heartbeat_send_timer() {
    if (_heartbeat_send_cycle > 0) {
        error_code ec;
        _heartbead_send_timer.expires_from_now(seconds(_heartbeat_send_cycle), ec);
        _heartbead_send_timer.async_wait(
                boost::bind(&UnixSocketConnection::handle_heartbeat_send, shared_from_this(), _1));
    }
}

void UnixSocketConnection::start() {
    boost::asio::async_read(
            _socket,
            boost::asio::buffer(&_read_header, sizeof(_read_header)),
            boost::bind(&UnixSocketConnection::handle_read_head, shared_from_this(), _1, _2));
    update_heartbeat_recv_timer();
    update_heartbeat_send_timer();
}

bool write_message(
        const shared_ptr<UnixSocketConnection> &connection,
        uint32_t type,
        const google::protobuf::Message &response) {
    shared_ptr<vector<char> > data(new vector<char>(response.ByteSize() + sizeof(type)));
    char *ptr = &(*data)[0];
    *reinterpret_cast<uint32_t *>(ptr) = type;
    if (!response.SerializeToArray(ptr + sizeof(type), data->size() - sizeof(type))) {
        WARNING_LOG("serialize message to string failed.");
        return false;
    }
    connection->write(data);
    return true;
}

bool send_cred(int fd) {
    char c = 255;

    struct iovec iov;
    iov.iov_base = &c;
    iov.iov_len = 1;

    union {
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(struct ucred))];
    } control_un;

    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = &control_un.control;
    msg.msg_controllen = sizeof(control_un.control);
    msg.msg_flags = 0;

    struct cmsghdr * cmptr = CMSG_FIRSTHDR(&msg);
    cmptr->cmsg_len = CMSG_LEN(sizeof(struct ucred));
    cmptr->cmsg_level = SOL_SOCKET;
    cmptr->cmsg_type = SCM_CREDENTIALS;
    struct ucred *ucptr = (struct ucred *)CMSG_DATA(cmptr);
    ucptr->pid = getpid();
    ucptr->uid = getuid();
    ucptr->gid = getgid();

    int n = sendmsg(fd, &msg, 0);
    if (n != 1) {
        return false;
    }
    return true;
}

bool recv_cred(int fd, struct ucred *ucptr) {
    int on = 1;
    int ret = setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));
    if (ret < 0) {
        return false;
    }

    char c = '\0';
    struct iovec iov;
    iov.iov_base = &c;  //数据
    iov.iov_len = 1;

    union {
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(struct ucred))];
    } control_un;

    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);
    msg.msg_flags = 0;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    ret = poll(&pfd, 1, 1000);
    if (ret <= 0) {  //超时未能读取则算失败，agent会close该fd
        return false;
    }

    int n = recvmsg(fd, &msg, 0);
    if (n != 1) {
        return false;
    }

    if (msg.msg_controllen < sizeof(struct cmsghdr)) {
        return false;
    }

    struct cmsghdr *cmptr = CMSG_FIRSTHDR(&msg);
    if (cmptr->cmsg_len != CMSG_LEN(sizeof(struct ucred))) {
        return false;
    }

    if (cmptr->cmsg_level != SOL_SOCKET || cmptr->cmsg_type != SCM_CREDENTIALS) {
        return false;
    }

    struct ucred * tmp = (struct ucred *)CMSG_DATA(cmptr);
    memcpy(ucptr, tmp, sizeof(struct ucred));
    return true;
}

}  // namespace bbts
