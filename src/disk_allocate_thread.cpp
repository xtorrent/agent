/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   disk_allocate_thread.cpp
 *
 * @author liuming03
 * @date   2015-1-18
 * @brief 
 */

#include "bbts/tool/disk_allocate_thread.h"

#include "bbts/log.h"
#include "bbts/path.h"

using std::string;
using std::vector;

using libtorrent::torrent_handle;
using libtorrent::torrent_info;

namespace bbts {
namespace tool {

DiskAllocateThread::DiskAllocateThread(const message::DownloadConfigure *configure) :
        _is_start(false),
        _is_end(false),
        _configure(configure),
        _cluster_downloader(NULL),
        _pieces_count(0) {}

DiskAllocateThread::~DiskAllocateThread() {}

void DiskAllocateThread::start(const torrent_handle &torrent) {
    _torrent = torrent;
    DEBUG_LOG("disk allocate thread will start");
    boost::thread thread(boost::bind(&DiskAllocateThread::disk_allocate, this));
    _thread.swap(thread);
    _is_start = true;
}

void DiskAllocateThread::notify_cluster_downloader(int allocated_pieces) {
    if (_cluster_downloader) {
        _cluster_downloader->update_allocated_pieces(allocated_pieces);
        _cluster_downloader->notify_all();
    }
}

void DiskAllocateThread::disk_allocate() {
    OPEN_LOG_R();
    DEBUG_LOG("disk allocate thread start");

    boost::intrusive_ptr<torrent_info const> ti = _torrent.torrent_file();
    vector<int> file_priorities = _torrent.file_priorities();
    vector<int> piece_priorities = _torrent.piece_priorities();
    _pieces_count = std::count(piece_priorities.begin(), piece_priorities.end(), 1);
    vector<int> current_piece_priorities(ti->num_pieces(), 0);
    _torrent.prioritize_pieces(current_piece_priorities);
    _torrent.set_upload_mode(false);
    _torrent.auto_managed(true);

    SpeedLimit speed_limit(_configure->dynamic_allocate_limit());
    off_t each_allocate_size = _configure->write_cache_line_size() * 16 * 1024;
    const long PAGE_SIZE = sysconf(_SC_PAGESIZE);
    int last_piece = 0;
    for (int i = 0; i < ti->num_files(); ++i) {
        libtorrent::file_entry fe = ti->file_at(i);
        if (file_priorities[i] == 0 || fe.pad_file || fe.size == 0) {
            continue;
        }
        int start_piece = ti->map_file(i, 0, 0).piece;
        if (last_piece < start_piece) {
            current_piece_priorities[last_piece] = 1;
        }
        last_piece = ti->map_file(i, fe.size - 1, 0).piece;
        string file_name = Path::is_absolute(fe.path) ?
                fe.path : _torrent.save_path() + "/" + fe.path;
        off_t current_offset = 0;
        struct stat statbuf;
        if (stat(file_name.c_str(), &statbuf) == 0) {
            TRACE_LOG("file %s has size:%ld, and goat size is %ld"
                    , file_name.c_str(), statbuf.st_size, fe.size);
            int end_piece = ti->map_file(i, statbuf.st_size, 0).piece;
            for (int piece_index = start_piece; piece_index < end_piece; ++piece_index) {
                current_piece_priorities[piece_index] = 1;
            }
            _torrent.prioritize_pieces(current_piece_priorities);
            notify_cluster_downloader(end_piece);
            if (statbuf.st_size == fe.size) {
                TRACE_LOG("file %s already allocated, so don't allocate again", file_name.c_str());
                continue;
            }
            current_offset = statbuf.st_size;
        } else if (errno == ENOENT) {
            Path::mkdirs(Path::parent_dir(file_name), 0755);
        }

        int open_flag = O_WRONLY | O_CREAT;
        if (_configure->use_dio_write()) {
            open_flag |= O_DIRECT;
        }
        int fd = open(file_name.c_str(), open_flag, fe.mode);
        if (fd < 0) {
            WARNING_LOG("open file %s failed, will not use dynamic pre-allocate mode: [%d]:%s",
                    file_name.c_str(), errno, strerror(errno));
            close(fd);
            break;
        }

        while (current_offset < fe.size) {
            off_t allocate_length = std::min(each_allocate_size, fe.size - current_offset);
            if (_configure->use_dio_write() && allocate_length % PAGE_SIZE != 0) {
                allocate_length = (allocate_length + PAGE_SIZE) / PAGE_SIZE * PAGE_SIZE;
            }
            int fallocate_ret = posix_fallocate(fd, current_offset, allocate_length);
            if (fallocate_ret != 0 && fallocate_ret != EINVAL) {
                WARNING_LOG("posix_fallocate failed:ret=%d, offset=%ld, len=%ld, file=%s",
                        fallocate_ret, current_offset, allocate_length, file_name.c_str());
                break;
            }
            DEBUG_LOG("allocate current_offset:%ld", current_offset);
            int begin_piece = ti->map_file(i, current_offset, 0).piece;
            int end_piece = ti->map_file(i, current_offset + allocate_length, 0).piece;
            for (int piece_index = begin_piece; piece_index < end_piece; ++piece_index) {
                current_piece_priorities[piece_index] = 1;
            }
            _torrent.prioritize_pieces(current_piece_priorities);
            notify_cluster_downloader(end_piece);
            current_offset += allocate_length;
            speed_limit.bandwidth_limit(allocate_length);
        }  // while
        if (_configure->use_dio_write()) {
            ftruncate(fd, fe.size);
        }
        close(fd);
    }  // for

    _torrent.prioritize_files(file_priorities);
    notify_cluster_downloader(ti->num_pieces());
    _is_end = true;
    TRACE_LOG("pre allocate thread exit");
    CLOSE_LOG_R();
}

} // namespace tool
} // namespace bbts
