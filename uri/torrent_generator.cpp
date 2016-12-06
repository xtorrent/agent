/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   torrent_generator.cpp
 *
 * @author hechaobin01 
 * @date   2015-8-26
 * @brief 
 */
#include "torrent_generator.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bbts/lazy_singleton.hpp"
#include "bbts/log.h"
#include "bbts/path.h"
#include "bbts/torrent_file_util.h"
#include "io.hpp"
#include "tcp_connection.h"

using boost::shared_ptr;
using boost::system::error_code;

namespace bbts {

TorrentGenerator *g_torrent_generator = LazySingleton<TorrentGenerator>::instance();

TorrentGenerator::TorrentGenerator() : _torrent_thread_number(kDefaultThreadNumber) {
    // do nothing
}

TorrentGenerator::~TorrentGenerator() {
    // do nothing
}

void TorrentGenerator::start() {
    // start torrent generate thread
    for (int i = 0; i < _torrent_thread_number; ++i) {
        _thread_group.create_thread(boost::bind(&TorrentGenerator::run, this));
    }
}

void TorrentGenerator::run() {
    OPEN_LOG_R();
    TRACE_LOG("torrent generator thread run begin");
    boost::asio::io_service::work work(_ios);
    boost::system::error_code ec;
    _ios.run(ec);
    if (ec) {
        WARNING_LOG("[torrent generator thread run fail: %s", ec.message().c_str());
    }
    TRACE_LOG("torrent generator thread run end");
    CLOSE_LOG_R();
}

void TorrentGenerator::async_get_torrent(
        shared_ptr<TcpConnection> connection,
        const std::string &path) {
    _ios.post(boost::bind(&TorrentGenerator::get_torrent, this, connection, path));
}

void TorrentGenerator::get_torrent(
        shared_ptr<TcpConnection> connection,
        const std::string &path) {
    MakeTorrentArgs make_torrent_args;
    make_torrent_args.path = Path::trim(Path::absolute(path));

    // no calculate hash
    make_torrent_args.flags |= MakeTorrentArgs::NO_CALCULATE_HASH;

    std::string infohash;
    std::vector<char> torrent;
    std::string message;
    if (!make_torrent(make_torrent_args, &infohash, &torrent, &message)) {
        message.append("\n");
        WARNING_LOG("generate torrent code failed for path[%s]:%s",
                path.c_str(), message.c_str());
    } else {
        message.assign("OK\n");
        TRACE_LOG("path[%s] generate torrent success, infohash:%s",
                path.c_str(), infohash.c_str());
    }

    // construct return message
    message.append(infohash);
    message.append("\n");
    for (size_t i = 0; i < torrent.size(); ++i) {
        message.append(1, torrent.at(i));
    }
    message.append("\n");
    message.append("TORRENT_END\n");

    boost::mutex::scoped_lock lock(_mutex);
    connection->async_write_torrent_message(message);
}

void TorrentGenerator::async_get_timestamp(
        shared_ptr<TcpConnection> connection,
        const std::string &path) {
    _ios.post(boost::bind(&TorrentGenerator::get_timestamp, this, connection, path));
}

void TorrentGenerator::get_timestamp(
        shared_ptr<TcpConnection> connection,
        const std::string &path) {
    std::string message("OK\n");
    struct stat buf;
    if (stat(path.c_str(), &buf) != 0) {
        message.assign("stat path:" + path + " error:");
        message.append(strerror(errno));
        message.append("\n");
        WARNING_LOG("stat path:%s error[%d]:%s", path.c_str(), errno, strerror(errno));
    } else {
        char tmp[50] = {'\0'};
        snprintf(tmp, 50, "%ld", buf.st_mtime);
        message.append(tmp);
        message.append("\n");
    }
    message.append("TORRENT_END\n");

    boost::mutex::scoped_lock lock(_mutex);
    connection->async_write_torrent_message(message);
}

void TorrentGenerator::async_check_path(
        shared_ptr<TcpConnection> connection,
        const std::string &path) {
    _ios.post(boost::bind(&TorrentGenerator::check_path, this, connection, path));
}

void TorrentGenerator::check_path(
        shared_ptr<TcpConnection> connection,
        const std::string &path) {
    std::string message("OK\n");
    struct stat buf;
    if (stat(path.c_str(), &buf) != 0) {
        message.assign(path + " error:");
        message.append(strerror(errno));
        message.append("\n");
        WARNING_LOG("stat path:%s error[%d]:%s", path.c_str(), errno, strerror(errno));
    }

    boost::mutex::scoped_lock lock(_mutex);
    connection->async_write_torrent_message(message);
}

} // namespace bbts
