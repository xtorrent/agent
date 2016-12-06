/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   tcp_server.cpp
 *
 * @author liuming03  hechaobin01
 * @date   2015-3-06
 * @brief 
 */

#include "tcp_server.h"

#include <boost/bind.hpp>

#include "bbts/lazy_singleton.hpp"
#include "bbts/log.h"
#include "bbts/string_util.h"
#include "bbts/timer_util.h"

namespace bbts {

using std::string;

using boost::asio::io_service;
using boost::system::error_code;
using boost::shared_ptr;

TcpServer *g_tcp_server = LazySingleton<TcpServer>::instance();

TcpServer::TcpServer() :
        _acceptor(_ios),
        _max_connection_num(-1),
        _default_upload_limit(kDefaultUploadLimit),
        _speed_timer(_ios),
        _tick_timer(_ios),
        _total_upload_limit(-1),
        _inner_connection_id(0),
        _current_timestamp(time(NULL)) {}

TcpServer::~TcpServer() {
    stop();
}

bool TcpServer::start() {
    if (!init()) {
        FATAL_LOG("init tcp server failed!");
        return false;
    }

    boost::thread t(&TcpServer::run, this);
    _thread.swap(t);

    return true;
}

void TcpServer::stop() {
    _ios.stop();
    error_code ec;
    _acceptor.close(ec);
    if (ec) {
        TRACE_LOG("acceptor close failed: %s\n", ec.message().c_str());
    }
}

void TcpServer::run() {
    OPEN_LOG_R();
    TRACE_LOG("tcp server thread run begin");
    boost::system::error_code ec;
    _ios.run(ec);
    if (ec) {
        WARNING_LOG("[tcp server thread run fail: %s", ec.message().c_str());
    }
    TRACE_LOG("tcp server thread run end");
    CLOSE_LOG_R();
}

void TcpServer::accept_cb(shared_ptr<TcpConnection> connection, const error_code& ec) {
    if (ec) {
        TRACE_LOG("server accept: %s", ec.message().c_str());
        if (_acceptor.is_open()) {
            async_accept();
        }
        return;
    }

    if (!is_reached_max_connection()) {
        add_connection(connection);
        connection->start();
    } else {
        WARNING_LOG("reached max connection:%ld, close it!", _max_connection_num);
        connection->close();
    }

    async_accept();
}

void TcpServer::async_accept() {
    ++_inner_connection_id;
    shared_ptr<TcpConnection> connection = TcpConnection::create(_ios, _inner_connection_id);
    _acceptor.async_accept(connection->socket(), connection->remote(),
            boost::bind(&TcpServer::accept_cb, this, connection, _1));
}

bool TcpServer::init() {
    error_code ec;
    _acceptor.open(_endpoint.protocol(), ec);
    if (ec) {
        WARNING_LOG("open acceptor failed: %s", ec.message().c_str());
        return false;
    }

    _acceptor.set_option(Tcp::acceptor::reuse_address(true));
    _acceptor.bind(_endpoint, ec);
    if (ec) {
        WARNING_LOG("bind address[%s] failed: %s",
                StringUtil::to_string(_endpoint).c_str(), ec.message().c_str());
        return false;
    }

    Tcp::acceptor::non_blocking_io non_block(true);
    _acceptor.io_control(non_block, ec);
    if (ec) {
        WARNING_LOG("acceptor[%s] set non blocking failed: %s",
                StringUtil::to_string(_endpoint).c_str(), ec.message().c_str());
        return false;
    }
    _acceptor.listen(128, ec);
    if (ec) {
        WARNING_LOG("listen[%s] failed: %s",
                StringUtil::to_string(_endpoint).c_str(), ec.message().c_str());
        return false;
    }

    timer_run_cycle("speed timer", _speed_timer, boost::posix_time::millisec(kCheckPeriod),
            boost::bind(&TcpServer::speed_timer_callback, this));

    timer_run_cycle("tick timer", _tick_timer, boost::posix_time::seconds(1),
            boost::bind(&TcpServer::on_tick, this));

    async_accept();
    return true;
}

void TcpServer::add_connection(shared_ptr<TcpConnection> conn) {
    DEBUG_LOG("add connection:%s", conn->to_string().c_str());
    _connection_map.insert(ConnectionMap::value_type(conn->id(), conn));
}

void TcpServer::adjust_upload_limit() {
    if (_total_upload_limit <= 0) {
        return;
    }

    int64_t total_set_limit = 0;
    int64_t total_used_per_unit = 0;
    UploadLimitMap::iterator it = _upload_limit_map.begin();
    for (; it != _upload_limit_map.end(); ++it) {
        total_set_limit += it->second.set_upload_limit;
        total_used_per_unit += it->second.total_upload_per_unit;
    }

    if (total_set_limit == 0) {
        return;
    }

    DEBUG_LOG("total_set:%ld, total_used:%ld, total_limit:%ld",
            total_set_limit, total_used_per_unit, _total_upload_limit);

    if (total_set_limit <= _total_upload_limit) {
        int64_t left = _total_upload_limit - total_set_limit;
        // not exceed total upload limit, set user set_upload_limit and use left
        for (it = _upload_limit_map.begin(); it != _upload_limit_map.end(); ++it) {
            it->second.upload_limit = it->second.set_upload_limit;
            // use the left upload limit
            double rate = static_cast<double>(it->second.set_upload_limit) / total_set_limit;
            it->second.upload_limit += static_cast<int64_t>(rate * left);
        }
        return;
    }

    if (total_set_limit == 0 && total_used_per_unit == 0) {
        // not one connection, do not adjust
        return;
    } else if (total_set_limit != 0 && total_used_per_unit == 0) {
        for (it = _upload_limit_map.begin(); it != _upload_limit_map.end(); ++it) {
            double rate = static_cast<double>(it->second.set_upload_limit) / total_set_limit;
            it->second.upload_limit = static_cast<int64_t>(rate * _total_upload_limit);
            if (it->second.upload_limit > it->second.set_upload_limit) {
                it->second.upload_limit = it->second.set_upload_limit;
            }
        }
    } else if (total_set_limit == 0 && total_used_per_unit != 0) {
        // strange!
        WARNING_LOG("total_set_limit is 0, but total used per unit is not 0:%d, strange!",
                total_used_per_unit);
        return;
    } else {
        for (it = _upload_limit_map.begin(); it != _upload_limit_map.end(); ++it) {
            double rate = 0.5 * static_cast<double>(it->second.set_upload_limit) / total_set_limit
                + 0.5 * static_cast<double>(it->second.total_upload_per_unit) / total_used_per_unit;
            DEBUG_LOG("infohash:%s, set:%ld, used:%ld, rate is %lf",
                    it->first.c_str(), it->second.set_upload_limit,
                    it->second.total_upload_per_unit, rate);
            it->second.upload_limit = static_cast<int64_t>(rate * _total_upload_limit);
            if (it->second.upload_limit > it->second.set_upload_limit) {
                it->second.upload_limit = it->second.set_upload_limit;
            }
        }
    } // if total_set_limit == 0 && total_used_per_unit == 0
}

void TcpServer::init_infohash_upload_limit(const std::string &infohash) {
    if (infohash.empty()) {
        return;
    }

    UploadLimitMap::iterator it = _upload_limit_map.find(infohash);
    if (it != _upload_limit_map.end()) {
        return;
    }
    int64_t default_limit = _default_upload_limit * 1024 * 1024 * kCheckPeriod / 1000;
    UploadLimitData data;
    data.set_upload_limit = default_limit;
    data.upload_limit = default_limit;
    data.connection_num += 1;
    _upload_limit_map.insert(UploadLimitMap::value_type(infohash, data));

    adjust_upload_limit();
    return;
}

void TcpServer::delete_infohash_upload_limit(const std::string &infohash) {
    if (infohash.empty()) {
        return;
    }

    UploadLimitMap::iterator it = _upload_limit_map.find(infohash);
    if (it != _upload_limit_map.end()) {
        _upload_limit_map.erase(it);
    }

    return;
}

bool TcpServer::del_connection(shared_ptr<TcpConnection> conn) {
    if (!conn) {
        return false;
    }
    DEBUG_LOG("delete conn:%s", conn->to_string().c_str());
    ConnectionMap::iterator it = _connection_map.find(conn->id());
    if (it == _connection_map.end()) {
        return false;
    }
    std::string infohash = it->second->infohash();
    _connection_map.erase(it);

    // delete from limit map
    // check if connection for this infohash has all been deleted 
    bool is_need_delete = true;
    for (it = _connection_map.begin(); it != _connection_map.end(); ++it) {
        if (it->second->infohash() == infohash) {
            is_need_delete = false;
            break;
        }
    }

    if (is_need_delete) {
        delete_infohash_upload_limit(infohash);
    } else {
        // delete connection_num from upload_limit_map
        UploadLimitMap::iterator it = _upload_limit_map.find(infohash);
        if (it != _upload_limit_map.end()) {
            it->second.connection_num -= 1;
            if (it->second.connection_num <= 0) {
                it->second.connection_num = 0;
            }
        }
    }

    return true;
}

void TcpServer::add_total_upload_per_unit_by_infohash(
        const std::string &infohash,
        int64_t total_upload) {
    if (infohash.empty()) {
        return;
    }

    UploadLimitMap::iterator it = _upload_limit_map.find(infohash);
    if (it == _upload_limit_map.end()) {
        WARNING_LOG("add total upload per unit, but infohash:%s not found!", infohash.c_str());
        return;
    }
    it->second.total_upload_per_unit += total_upload;
}

void TcpServer::add_wait_by_infohash(
        const std::string &infohash,
        int64_t wait_bytes) {
    if (infohash.empty()) {
        return;
    }

    UploadLimitMap::iterator it = _upload_limit_map.find(infohash);
    if (it == _upload_limit_map.end()) {
        WARNING_LOG("add wait bytes but infohash:%s not found!", infohash.c_str());
        return;
    }
    it->second.total_wait_bytes += wait_bytes;
}

void TcpServer::del_wait_by_infohash(
        const std::string &infohash,
        int64_t wait_bytes) {
    if (infohash.empty()) {
        return;
    }

    UploadLimitMap::iterator it = _upload_limit_map.find(infohash);
    if (it == _upload_limit_map.end()) {
        WARNING_LOG("del wait bytes but infohash:%s not found!", infohash.c_str());
        return;
    }
    it->second.total_wait_bytes -= wait_bytes;

    if (it->second.total_wait_bytes < 0) {
        it->second.total_wait_bytes = 0;
    }
}

void TcpServer::calculate_connection_quota() {
    static int counter = 0;
    ++counter;

    UploadLimitMap::iterator upload_it = _upload_limit_map.begin();
    ConnectionMap::iterator conn_it = _connection_map.begin();
    for (; conn_it != _connection_map.end(); ++conn_it) {
        shared_ptr<TcpConnection> conn = conn_it->second;

        upload_it = _upload_limit_map.find(conn->infohash());
        if (upload_it == _upload_limit_map.end()) {
            continue;
        }
        UploadLimitData &upload_data = upload_it->second;

        // TODO: need more intelligent algorithm
        if (upload_data.total_wait_bytes == 0
            && upload_data.total_upload_per_unit == 0) {
            if (upload_data.connection_num != 0) {
                conn->set_upload_quota(static_cast<int64_t>(upload_data.upload_limit / upload_data.connection_num));
            }
        } else if (upload_data.total_wait_bytes == 0
            && upload_data.total_upload_per_unit != 0) {
            double rate = static_cast<double>(conn->total_upload_per_unit()) / upload_data.total_upload_per_unit;
            conn->set_upload_quota(static_cast<int64_t>(rate * upload_data.upload_limit));
        } else if (upload_data.total_wait_bytes != 0
            && upload_data.total_upload_per_unit == 0) {
            double rate = static_cast<double>(conn->wait_write_size()) / upload_data.total_wait_bytes;
            conn->set_upload_quota(static_cast<int64_t>(rate * upload_data.upload_limit));
        } else {
            double rate = 0.3 * static_cast<double>(conn->total_upload_per_unit()) / upload_data.total_upload_per_unit
                + 0.7 * static_cast<double>(conn->wait_write_size()) / upload_data.total_wait_bytes;
            conn->set_upload_quota(static_cast<int64_t>(rate * upload_data.upload_limit));
        }

        DEBUG_LOG("%d, conn:%d, upload_quota:%ld, conn_total_wait:%ld, conn_total_per:%ld, total_wait:%ld, total_send:%ld",
                counter, conn->id(), conn->upload_quota(), conn->wait_write_size(), conn->total_upload_per_unit(),
                upload_data.total_wait_bytes, upload_data.total_upload_per_unit);

        conn->set_total_upload_per_unit(0);
    }

    // clean total_upload_per_unit
    for (upload_it = _upload_limit_map.begin(); upload_it != _upload_limit_map.end(); ++upload_it) {
        upload_it->second.total_upload_per_unit = 0;
    }
}

void TcpServer::speed_timer_callback() {
    // calculate each connection upload quota
    calculate_connection_quota();

    // wake up wait connections
    while (!_connection_quota_queue.empty()) {
        _connection_quota_queue.front().conn->get_quota_and_asnyc_write();
        _connection_quota_queue.pop();
    }
}

void TcpServer::on_tick() {
    _current_timestamp = time(NULL);

    ConnectionMap::iterator it = _connection_map.begin();
    for (; it != _connection_map.end();) {
        shared_ptr<TcpConnection> conn = it->second;
        if (_current_timestamp - conn->tick_timestamp() >= conn->active_timeout()) {
            DEBUG_LOG("connection[%s] active timeout, current:%ld, before:%ld, close it!",
                    conn->to_string().c_str(), _current_timestamp, conn->tick_timestamp());
            // if connection close called, connection has been delete from _connection_map
            // close may erase it, so we get the next before erase
            ++it;
            conn->close();
        } else {
            ++it;
        }
    }

    // update upload limit for all infohashes
    adjust_upload_limit();
}

void TcpServer::set_total_upload_limit(int64_t upload_limit) {
    if (upload_limit <= 0) {
        // not limit
        _total_upload_limit = -1;
        return;
    }
    _total_upload_limit = static_cast<int64_t>(upload_limit * kCheckPeriod / 1000);
}

bool TcpServer::set_upload_limit(const std::string &infohash, int64_t upload_limit) {
    if (infohash.empty() || upload_limit <= 0) {
        return false;
    }

    UploadLimitMap::iterator it = _upload_limit_map.find(infohash);
    if (it == _upload_limit_map.end()) {
        WARNING_LOG("infohash:%s not exist!", infohash.c_str());
        return false;
    }

    TRACE_LOG("set upload limit, infohash:%s, org limit:%ld, new limit:%ld",
            infohash.c_str(), it->second.set_upload_limit, upload_limit * kCheckPeriod / 1000);

    it->second.set_upload_limit = upload_limit * kCheckPeriod / 1000;
    return true;
}

void TcpServer::add_connection_to_wait(
        boost::shared_ptr<TcpConnection> conn,
        int64_t require_quota) {
    TcpConnectionQuota quota;
    quota.require_quota = require_quota;
    quota.conn = conn;
    _connection_quota_queue.push(quota);
}

}  // namespace bbts
