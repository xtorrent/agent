/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   worker_pool.cpp
 *
 * @author liuming03
 * @date   2013-7-10
 * @brief 
 */

#include "bbts/worker_pool.h"

#include <string>

#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "bbts/log.h"

namespace bbts {

WorkerPool::WorkerPool(const std::string &tag) : _thread_num(0), _tag(tag) {}

WorkerPool::~WorkerPool() {}

void WorkerPool::thread_main(int worker_id) {
    OPEN_LOG_R();
    TRACE_LOG("[%s][%d] run begin.", _tag.c_str(), worker_id);
    boost::system::error_code ec;
    _io_service.run(ec);
    if (ec) {
        WARNING_LOG("[%s]thread pool run fail: %s", _tag.c_str(), ec.message().c_str());
    }
    TRACE_LOG("[%s][%d] run end.", _tag.c_str(), worker_id);
    CLOSE_LOG_R();
}

void WorkerPool::start(int thread_num) {
    _empty_work.reset(new boost::asio::io_service::work(_io_service));
    _thread_num = thread_num;
    _io_service.reset();
    for (int i = 0; i < _thread_num; ++i) {
        _threads.create_thread(boost::bind(&WorkerPool::thread_main, this, i));
    }
}

void WorkerPool::join_all() {
    _threads.join_all();
}

}  // namespace bbts
