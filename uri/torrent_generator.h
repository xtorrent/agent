/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   torrent_generator.h
 *
 * @author hechaobin01
 * @date   2015-8-26
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_TORRENT_GENERATOR_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_TORRENT_GENERATOR_H

#include <boost/asio/io_service.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>

#include "cache_manager.h"

namespace bbts {

class TcpConnection;

/**
 * @brief
 */
class TorrentGenerator {
public:
    TorrentGenerator();
    ~TorrentGenerator();

    void start();

    void stop() {
        _ios.stop();
    }

    void join() {
        _thread_group.join_all();
    }

    void async_get_torrent(boost::shared_ptr<TcpConnection> connection,
            const std::string &path);

    void async_get_timestamp(boost::shared_ptr<TcpConnection> connection,
            const std::string &path);

    void async_check_path(boost::shared_ptr<TcpConnection> connection,
            const std::string &path);

    void set_torrent_thread_number(int thread_number) {
        if (thread_number > 0) {
            _torrent_thread_number = thread_number;
        }
    }

private:
    void run();

    /**
     * @brief 制作种子文件（不计算hash值）
     *
     * @param [in] connection   : boost::shared_ptr<TcpConnection>
     * @param [in] path   : const std::string&
     * @return  void 
     * @retval   
     * @see 
     * @author yanghanlin
     * @date 2016/01/04 19:23:00
    **/
    void get_torrent(boost::shared_ptr<TcpConnection> connection,
            const std::string &path);

    /**
     * @brief 获得时间戳
     *
     * @param [in] connection   : boost::shared_ptr<TcpConnection>
     * @param [in] path   : const std::string&
     * @return  void 
     * @retval   
     * @see APUE stat
     * @author yanghanlin
     * @date 2016/01/04 19:23:53
    **/
    void get_timestamp(boost::shared_ptr<TcpConnection> connection,
            const std::string &path);

    /**
     * @brief 检查文件路径 
     *
     * @param [in] connection   : boost::shared_ptr<TcpConnection>
     * @param [in] path   : const std::string&
     * @return  void 
     * @retval   
     * @see APUE stat 
     * @author yanghanlin
     * @date 2016/01/04 19:24:13
    **/
    void check_path(boost::shared_ptr<TcpConnection> connection,
            const std::string &path);

    static const int kDefaultThreadNumber = 1;

    int _torrent_thread_number;
    boost::thread_group _thread_group;
    boost::asio::io_service _ios;
    boost::mutex _mutex;
};

extern TorrentGenerator *g_torrent_generator;

} // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_TORRENT_GENERATOR_H
