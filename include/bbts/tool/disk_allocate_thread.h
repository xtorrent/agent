/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   disk_allocate_thread.h
 *
 * @author liuming03
 * @date   2015-1-18
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_DISK_ALLOCATE_THREAD_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_DISK_ALLOCATE_THREAD_H

#include <boost/noncopyable.hpp>
#include <boost/thread/thread.hpp>

#include <libtorrent/torrent_handle.hpp>

#include "bbts/tool/cluster_downloader.h"
#include "configure.pb.h"

namespace bbts {
namespace tool {

/**
 * @brief
 */
class DiskAllocateThread : public boost::noncopyable {
public:
    DiskAllocateThread(const message::DownloadConfigure *configure);

    ~DiskAllocateThread();

    bool is_start() const {
        return _is_start;
    }

    bool is_end() const {
        return _is_end;
    }

    int pieces_count() const {
        return _pieces_count;
    }

    void start(const libtorrent::torrent_handle &torrent);

    void set_cluster_downloader(ClusterDownloader *cluster_downloader) {
        _cluster_downloader = cluster_downloader;
    }

private:
    void disk_allocate();

    void notify_cluster_downloader(int allocated_pieces);

    // allocate thread
    boost::thread _thread;
    // this flag ensure pre allocate thread start only once
    bool _is_start;
    // this flag ensure pred allocate thread end and cluster downloader will work normally
    bool _is_end;

    libtorrent::torrent_handle _torrent;
    const message::DownloadConfigure *_configure;
    ClusterDownloader *_cluster_downloader;
    int _pieces_count;
};

} // namespace tool
} // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_DISK_ALLOCATE_THREAD_H
