/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file tcp_server.h
 *
 * @author liuming03
 * @date 2015-2-5
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_TCP_SERVER_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_TCP_SERVER_H

#include <map>
#include <queue>
#include <vector>

#include <boost/asio/deadline_timer.hpp>
#include <boost/noncopyable.hpp>
#include <boost/system/error_code.hpp>
#include <boost/unordered_map.hpp>

#include "tcp_connection.h"

namespace bbts {

/**
 * @brief
 */

class TcpServer : public boost::noncopyable {
public:
    TcpServer();
    ~TcpServer();

    bool start();

    void stop();

    void join() {
        _thread.join();
    }

    void set_tcp_port(int port) {
        Tcp::endpoint listen_endpoint(Tcp::v4(), port);
        _endpoint = listen_endpoint;
    }

    boost::asio::io_service& get_io_service() {
        return _ios;
    }

    bool del_connection(boost::shared_ptr<TcpConnection> conn);

    void set_max_connection(int64_t max_connection) {
        if (max_connection == 0) {
            // 0 means not limit
            max_connection = -1;
        }
        _max_connection_num = max_connection;
    }

    void set_default_upload_limit_for_data(int default_limit) {
        if (_default_upload_limit >= 1) {
            _default_upload_limit = default_limit;
        }
    }

    void set_total_upload_limit(int64_t upload_limit);  // total upload_limit in byte/s unit

    bool set_upload_limit(const std::string &infohash, int64_t upload_limit);

    void add_total_upload_per_unit_by_infohash(const std::string &infohash, int64_t total_upload);

    void add_wait_by_infohash(const std::string &infohash, int64_t wait_bytes);

    void del_wait_by_infohash(const std::string &infohash, int64_t wait_bytes);
    
    const time_t current_timestamp() {
        return _current_timestamp;
    }

    void init_infohash_upload_limit(const std::string &infohash);

    void delete_infohash_upload_limit(const std::string &infohash);

    void add_connection_to_wait(boost::shared_ptr<TcpConnection> conn, int64_t require_quota);

private:
    struct UploadLimitData {
        // 1048576 means 10M/s, for 10 * 1024 * 1024 * (100 / 1000)
        UploadLimitData()
            : set_upload_limit(1048576),
              upload_limit(1048576),
              connection_num(0),
              total_wait_bytes(0),
              total_upload_per_unit(0) {}

        int64_t set_upload_limit;       // user set limit
        int64_t upload_limit;           // real upload limit by agent
        int64_t connection_num;         // connection number for this infohash
        int64_t total_wait_bytes;       // wait bytes for sending
        int64_t total_upload_per_unit;  // per unit upload bytes
    };

    struct TcpConnectionQuota {
        TcpConnectionQuota() : require_quota(0) {}

        boost::shared_ptr<TcpConnection> conn;
        int64_t require_quota;
    };

    typedef boost::unordered_map<int64_t, boost::shared_ptr<TcpConnection> > ConnectionMap;

    typedef boost::unordered_map<std::string, UploadLimitData> UploadLimitMap;

    bool init();

    void async_accept();

    void accept_cb(boost::shared_ptr<TcpConnection> conn,
            const boost::system::error_code& ec);

    void add_connection(boost::shared_ptr<TcpConnection> conn);

    void speed_timer_callback();

    void adjust_upload_limit();

    void calculate_connection_quota();

    void on_tick();

    void run();

    bool is_reached_max_connection() {
        if (_max_connection_num <= 0) {
            return false;
        }

        return _connection_map.size() + 1 > static_cast<size_t>(_max_connection_num);
    }

    static const int kCheckPeriod = 100;  // check per 100ms for speed limit
    static const int kDefaultUploadLimit = 10;  // default 10M/s

    Tcp::endpoint _endpoint;
    boost::asio::io_service _ios;
    boost::thread _thread;
    Tcp::acceptor _acceptor;
    int64_t _max_connection_num;
    int _default_upload_limit;
    boost::asio::deadline_timer _speed_timer;
    boost::asio::deadline_timer _tick_timer;
    int64_t _total_upload_limit;      // in byte unit, upload limit in per kCheckPeriod
    UploadLimitMap _upload_limit_map;
    // connection wait for quota queue 
    std::queue<TcpConnectionQuota> _connection_quota_queue;
    // all connection map, each connection is identified by an unique id
    ConnectionMap _connection_map;
    int64_t _inner_connection_id;
    time_t _current_timestamp;
};

extern TcpServer *g_tcp_server;

}  // namespace bbts

#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_TCP_SERVER_H
