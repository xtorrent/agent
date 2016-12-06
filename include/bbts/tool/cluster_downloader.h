/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file cluster_downloader.h
 *
 * @author liuming03
 * @date 2013-7-29
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_CLUSTER_DOWNLOADER_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_CLUSTER_DOWNLOADER_H

#include <set>
#include <queue>

#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread.hpp>
#include <libtorrent/torrent_handle.hpp>

#include "bbts/speed_limit.h"
#include "configure.pb.h"

namespace bbts {

class BTInfo;
class ClusterAPI;

namespace tool {

class ClusterDownloader : public boost::noncopyable {
public:
    enum {
        IGNORE_ERROR = 1,
        DISABLE_HASH_CHECK = 2,
        USE_DYNAMIC_ALLOCATE = 4,
        ENABLE_DYNAMIC_HASH_CHECK = 8
    };

    ClusterDownloader(
            const message::SourceURI &cluster_uri,
            libtorrent::torrent_handle handle,
            int threads_num,
            int flags,
            int max_hdfs_cache_pieces);

    ~ClusterDownloader();

    bool start_download();
    void join();
    void stop_download();
    bool is_stoped();
    void notify_all();
    void notify_piece_finished(int piece_index);
    void update_allocated_pieces(int allocated_pieces);
    void add_failed_piece(int piece_index);

    int get_retval() {
        return _retval;
    }

    void set_retval(int retval) {
        _retval = retval;
    }

    bool is_start() {
        return _is_start || _retval != 0;
    }

    int64_t total_download() {
        return _total_download;
    }

    time_t start_time() {
        return _start_time;
    }

    const message::SourceURI& cluster_uri() {
        return _cluster_uri;
    }

    void set_cluster_download_limit(int limit_rate) {
        _cluster_download_limit.set_limit_rate(limit_rate);
    }

private:
    void download();
    int get_undownloaded_piece();
    int get_piece_to_download();
    void add_waiting_disk_piece(int piece_index);
    void add_waiting_download_pieces(int start_piece, int end_piece);

    message::SourceURI _cluster_uri;
    ClusterAPI *_cluster_api;
    void *_fs;

    libtorrent::torrent_handle _torrent;
    boost::scoped_ptr<BTInfo> _btinfo;

    int _threads_num;                          // 集群下载线程数
    int _complete_threads_num;
    boost::thread_group _thread_group;         // 集群下载线程池
    volatile bool _should_stop;                // 下载线程是否需要停止下载
    int _retval;
    time_t _start_time;

    libtorrent::bitfield _pieces_have;         // 已经有的pieces位域
    std::list<int> _waiting_for_download_pieces;
    std::set<int> _waiting_for_disk_pieces;    // 已经下载完成但正在等待的piece集合
    size_t _max_hdfs_cache_pieces;
    int _allocated_pieces;

    int64_t _last_p2p_total_downloaded;
    int64_t _total_download;
    SpeedLimit _download_limit;
    SpeedLimit _cluster_download_limit;

    boost::mutex _mutex;
    boost::condition_variable _cond;

    bool _ignore_error:1;
    bool _disable_hash_check:1;
    bool _enable_dynamic_hash_check:1;
    bool _use_dynamic_allocate:1;
    bool _is_start:1;
};

}  // namespace tool
}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_DOWNLOAD_CLUSTER_DOWNLOADER_H
