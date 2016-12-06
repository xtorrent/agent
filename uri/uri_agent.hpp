/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   uri_agent.h
 *
 * @author hechaobin01
 * @date   2015-3-06
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_URI_AGENT_HPP
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_URI_AGENT_HPP

#include "disk_manager.h"
#include "torrent_generator.h"
#include "tcp_server.h"

namespace bbts {

/**
 * @brief
 */
class UriAgent {
public:
    UriAgent() {}
    ~UriAgent() {}


    // option setters
    void set_tcp_port(int port) {
        return g_tcp_server->set_tcp_port(port);
    }

    void set_max_connection(int64_t max_connection) {
        return g_tcp_server->set_max_connection(max_connection);
    }

    void set_total_upload_limit(int64_t upload_limit) {
        return g_tcp_server->set_total_upload_limit(upload_limit);
    }

    bool set_upload_limit(const std::string &infohash, int64_t upload_limit) {
        return g_tcp_server->set_upload_limit(infohash, upload_limit);
    }

    void set_max_cache_size(int cache_size) {
        g_disk_manager->set_cache_size(cache_size);
    }

    void set_cache_expire_time(int cache_expire_time) {
        g_disk_manager->set_cache_expire_time(cache_expire_time);
    }

    void set_read_line_size(uint32_t read_line_size) {
        g_disk_manager->set_read_line_size(read_line_size);
    }

    void set_disk_thread_number(int thread_number) {
        g_disk_manager->set_disk_thread_number(thread_number);
    }

    void set_default_upload_limit_for_data(int default_limit) {
        g_tcp_server->set_default_upload_limit_for_data(default_limit);
    }

    bool start() {
        g_disk_manager->start();
        g_torrent_generator->start();
        return g_tcp_server->start();
    }

    void stop() {
        g_disk_manager->stop();
        g_torrent_generator->stop();
        g_tcp_server->stop();
    }

    void join() {
        g_disk_manager->join();
        g_torrent_generator->join();
        g_tcp_server->join();
    }

};  // class UriAgent

} // namespace bbts

#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_URI_AGENT_HPP
