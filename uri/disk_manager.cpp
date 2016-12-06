/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   disk_manager.cpp
 *
 * @author liuming03
 * @date   2015-2-12
 * @brief 
 */

#include "disk_manager.h"
#include "tcp_connection.h"
#include "uri_piece_request.h"

#include "bbts/consistent_hash_ring.hpp"
#include "bbts/lazy_singleton.hpp"
#include "bbts/log.h"
#include "io.hpp"

using boost::shared_ptr;
using boost::system::error_code;

namespace bbts {

DiskManager *g_disk_manager = LazySingleton<DiskManager>::instance();

DiskManager::DiskManager() : _disk_thread_number(kReadThreadNumber) {
    _cache_manager = new CacheManager(_ios);
}

DiskManager::~DiskManager() {
    if (_cache_manager != NULL) {
        delete _cache_manager;
        _cache_manager = NULL;
    }
}

void DiskManager::start() {
    assert(_disk_thread_number > 0);
    // start read disk thread
    for (int i = 0; i < _disk_thread_number; ++i) {
        _thread_group.create_thread(boost::bind(&DiskManager::run, this));
        shared_ptr<IoServiceStrand> strand(new IoServiceStrand(_ios));
        _strand_list.push_back(strand);
    }

}

void DiskManager::run() {
    OPEN_LOG_R();
    TRACE_LOG("disk manager thread run begin");
    boost::asio::io_service::work work(_ios);
    boost::system::error_code ec;
    _ios.run(ec);
    if (ec) {
        WARNING_LOG("[disk manager thread run fail: %s", ec.message().c_str());
    }
    TRACE_LOG("disk manager thread run end");
    CLOSE_LOG_R();
}

void DiskManager::async_piece_request(shared_ptr<TcpConnection> connection,
        const URIPieceRequest &request) {
    int index = get_request_thread_hash(request);
    shared_ptr<IoServiceStrand> strand = _strand_list.at(index);
    _ios.post(strand->wrap(
                boost::bind(&DiskManager::piece_request, this, connection, request)));
}

void DiskManager::piece_request(shared_ptr<TcpConnection> connection,
        const URIPieceRequest &request) {
    std::string error_msg;
    shared_ptr<Buffer> buffer = _cache_manager->fetch(request, error_msg);
    if (!error_msg.empty() || !buffer) {
        std::string msg("fetch block failed:");
        msg.append(error_msg);
        boost::mutex::scoped_lock lock(_mutex);
        connection->write_failed_message(msg);
        // close by remote peer
        // connection->close();
        return;
    }

    {
        boost::mutex::scoped_lock lock(_mutex);
        shared_ptr<Buffer> header(new Buffer(13));
        char *ptr = &(*header)[0];
        write_uint32(9 + buffer->size(), ptr);
        write_uint8(7, ptr);
        write_uint32(request.piece_index, ptr);
        write_uint32(request.piece_offset, ptr);
        connection->write_data(header);
        connection->write_data(buffer);
    }
}

void DiskManager::async_piece_hash_request(shared_ptr<TcpConnection> connection,
        const URIPieceRequest &request) {
    int index = get_request_thread_hash(request);
    shared_ptr<IoServiceStrand> strand = _strand_list.at(index);
    _ios.post(strand->wrap(
                boost::bind(&DiskManager::piece_hash_request, this, connection, request)));
}

void DiskManager::piece_hash_request(shared_ptr<TcpConnection> connection,
        const URIPieceRequest &request) {
    std::string error_msg;
    shared_ptr<Buffer> buffer = _cache_manager->calculate_hash(request, error_msg);
    if (!error_msg.empty() || !buffer) {
        std::string msg("calculate piece hash failed:");
        msg.append(error_msg);
        boost::mutex::scoped_lock lock(_mutex);
        connection->write_failed_message(msg);
        return;
    }

    {
        boost::mutex::scoped_lock lock(_mutex);
        connection->async_write_piece_hash(request.piece_index, buffer);
    }
}

int DiskManager::get_request_thread_hash(const URIPieceRequest &request) {
    uint32_t infohash_hash_index = hash_crc32(
            request.infohash.c_str(),
            request.infohash.length(),
            NULL);
    return (request.piece_index + infohash_hash_index) % _disk_thread_number;
}

} // namespace bbts
