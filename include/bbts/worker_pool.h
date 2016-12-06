/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file worker_pool.h
 *
 * @author liuming03
 * @date 2013-7-10
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_WORKER_POOL_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_WORKER_POOL_H

#include <string>

#include <boost/asio/io_service.hpp>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread.hpp>

namespace bbts {

/**
 * @brief
 */
class WorkerPool : public boost::noncopyable {
public:
    explicit WorkerPool(const std::string &tag);
    ~WorkerPool();

    void start(int thread_num);
    void join_all();

    boost::asio::io_service& get_io_service() {
        return _io_service;
    }

    const std::string& get_tag() const {
        return _tag;
    }

    void stop() {
        _empty_work.reset();
        _io_service.stop();
    }

private:
    void thread_main(int worker_id);

    boost::asio::io_service _io_service;
    boost::scoped_ptr<boost::asio::io_service::work> _empty_work;
    boost::thread_group _threads;
    int _thread_num;
    std::string _tag;
};

}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_WORKER_POOL_H
