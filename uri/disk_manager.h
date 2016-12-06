/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   disk_manager.h
 *
 * @author liuming03
 * @date   2015-2-12
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_DISK_MANAGER_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_DISK_MANAGER_H

#include <vector>

#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>

#include "cache_manager.h"

namespace bbts {

struct URIPieceRequest;
class TcpConnection;

/**
 * @brief
 */
class DiskManager {
public:
    DiskManager();

    ~DiskManager();

    void start();

    void stop() {
        _ios.stop();
    }

    void join() {
        _thread_group.join_all();
    }

    void async_piece_request(boost::shared_ptr<TcpConnection> connection,
            const URIPieceRequest &request);

    void async_piece_hash_request(boost::shared_ptr<TcpConnection> connection,
            const URIPieceRequest &request);

    void set_cache_size(int cache_size) {
        if (_cache_manager != NULL) {
            _cache_manager->set_cache_size(cache_size);
        }
    }

    void set_cache_expire_time(int cache_expire_time) {
        if (_cache_manager != NULL) {
            _cache_manager->set_cache_expire_time(cache_expire_time);
        }
    }

    void set_read_line_size(int read_line_size) {
        if (_cache_manager != NULL) {
            _cache_manager->set_read_line_size(read_line_size);
        }
    }

    void set_disk_thread_number(int thread_number) {
        if (thread_number > 0) {
            _disk_thread_number = thread_number;
        }
    }

private:
    typedef boost::asio::io_service::strand IoServiceStrand;
    void run();

    void piece_request(boost::shared_ptr<TcpConnection> connection,
            const URIPieceRequest &request);

    void piece_hash_request(boost::shared_ptr<TcpConnection> connection,
            const URIPieceRequest &request);

    int get_request_thread_hash(const URIPieceRequest &request);

    static const int kReadThreadNumber = 5;

    int _disk_thread_number;
    boost::thread_group _thread_group;
    boost::asio::io_service _ios;
    CacheManager *_cache_manager;
    boost::mutex _mutex;
    std::vector<boost::shared_ptr<IoServiceStrand> > _strand_list;
};

extern DiskManager *g_disk_manager;

} // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_DISK_MANAGER_H
