/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   cluster_manager.cpp
 *
 * @author liuming03
 * @date   2013-6-2
 */

#include "bbts/tool/cluster_downloader.h"

#include <boost/system/error_code.hpp>
#include <libtorrent/torrent_info.hpp>

#include "bbts/bt_info.hpp"
#include "bbts/cluster_api.h"
#include "bbts/log.h"
#include "bbts/path.h"
#include "bbts/process_info.h"

using std::string;
using boost::scoped_array;
using boost::shared_array;
using boost::system::error_code;
using libtorrent::cluster_config_entry;
using libtorrent::torrent_handle;
using libtorrent::torrent_info;

namespace bbts {
namespace tool {

typedef boost::mutex::scoped_lock scoped_lock;

ClusterDownloader::ClusterDownloader(
        const message::SourceURI &cluster_uri,
        torrent_handle handle,
        int threads_num,
        int flags,
        int max_hdfs_cache_pieces) :
        _cluster_uri(cluster_uri),
        _cluster_api(NULL),
        _fs(NULL),
        _torrent(handle),
        _threads_num(threads_num),
        _complete_threads_num(0),
        _should_stop(false),
        _retval(0),
        _start_time(0),
        _max_hdfs_cache_pieces(max_hdfs_cache_pieces),
        _allocated_pieces(-1),
        _last_p2p_total_downloaded(0),
        _total_download(0),
        _download_limit(handle.download_limit()),
        _cluster_download_limit(0),
        _ignore_error(flags & IGNORE_ERROR),
        _disable_hash_check(flags & DISABLE_HASH_CHECK),
        _enable_dynamic_hash_check(flags & ENABLE_DYNAMIC_HASH_CHECK),
        _use_dynamic_allocate(flags & USE_DYNAMIC_ALLOCATE),
        _is_start(false) {
    _cluster_api = ClusterAPI::get_cluster_api(_cluster_uri.protocol());
}

ClusterDownloader::~ClusterDownloader() {
    TRACE_LOG("close cluster success.");
}

bool ClusterDownloader::start_download() {
    if (!_cluster_api) {
        WARNING_LOG("Not support cluster(%s) download.", _cluster_uri.protocol().c_str());
        _retval = -23;
        return false;
    }
    TRACE_LOG("Support cluster download, will download from %s.", _cluster_uri.protocol().c_str());

    _fs = _cluster_api->connect_cluster_with_timeout(_cluster_uri, 120);
    if (!_fs) {
        _retval = -24;
        return false;
    }

    {
        torrent_info ti(_torrent.info_hash());
        boost::intrusive_ptr<torrent_info const> old_ti = _torrent.torrent_file();
        if (!old_ti) {
            _retval = -25;
            return false;
        }
        shared_array<char> metadata_buf = old_ti->metadata();
        int metadata_size = old_ti->metadata_size();
        libtorrent::lazy_entry metadata;
        error_code ec;
        int ret = libtorrent::lazy_bdecode(metadata_buf.get(),
                metadata_buf.get() + metadata_size, metadata, ec);
        if (ret != 0 || !ti.parse_info_section(metadata, ec, 0)) {
            _retval = -25;
            WARNING_LOG("parse info section failed.");
            return false;
        }
        _btinfo.reset(new BTInfo(ti, _cluster_uri.path()));
    }

    int num_pieces = _btinfo->num_pieces();
    _pieces_have.resize(num_pieces, false);
    if (!_use_dynamic_allocate) {
        _allocated_pieces = num_pieces;
        std::vector<int> piece_priorities = _torrent.piece_priorities();
        std::vector<int> pieces_vector;
        for (int i = 0; i < num_pieces; ++i) {
            if (piece_priorities[i] == 0) {
                _pieces_have.set_bit(i);
            } else {
                pieces_vector.push_back(i);
            }
        }
        int size = pieces_vector.size();
        for (int i = 0; i < size; ++i) {
            int vector_index = g_process_info->random() % pieces_vector.size();
            _waiting_for_download_pieces.push_back(pieces_vector[vector_index]);
            pieces_vector.erase(pieces_vector.begin() + vector_index);
        }
        assert(pieces_vector.empty());
    }

    if (num_pieces < _threads_num) {
        _threads_num = num_pieces;
    }
    DEBUG_LOG("will start cluster thread, max hdfs cache pieces: %d", _max_hdfs_cache_pieces);
    _start_time = time(NULL);
    for (int i = 0; i < _threads_num; ++i) {
        DEBUG_LOG("cluster thread %i start", i);
        _thread_group.create_thread(boost::bind(&ClusterDownloader::download, this));
    }
    _is_start = true;
    return true;
}

bool ClusterDownloader::is_stoped() {
    return _complete_threads_num == _threads_num;
}

void ClusterDownloader::join() {
    _thread_group.join_all();
}

void ClusterDownloader::stop_download() {
    _should_stop = true;
    notify_all();
}

void ClusterDownloader::add_waiting_disk_piece(int piece_index) {
    scoped_lock lock(_mutex);
    if (!_pieces_have.get_bit(piece_index)) {
        _waiting_for_disk_pieces.insert(piece_index);
    }
}

void ClusterDownloader::add_waiting_download_pieces(int start_piece, int end_piece) {
    if (start_piece >= end_piece) {
        return;
    }

    int random = _waiting_for_download_pieces.size() > 0 ?
            g_process_info->random() % _waiting_for_download_pieces.size() : 0;
    std::list<int>::iterator base_it;
    if (static_cast<size_t>(random) < _waiting_for_download_pieces.size() / 2) {
        std::list<int>::iterator it = _waiting_for_download_pieces.begin();
        while (random > 0) {
            ++it;
            --random;
        }
        base_it = it;
    } else {
        std::list<int>::reverse_iterator it = _waiting_for_download_pieces.rbegin();
        while (random > 0) {
            ++it;
            --random;
        }
        base_it = it.base();
    }
    if (g_process_info->random() % 2 == 0) {
        for (int i = start_piece; i < end_piece; ++i) {
            if (_use_dynamic_allocate && _torrent.piece_priority(i) == 0) {
                continue;
            }
            _waiting_for_download_pieces.insert(base_it, i);
            if (base_it == _waiting_for_download_pieces.end()) {
                base_it == _waiting_for_download_pieces.begin();
            } else {
                ++base_it;
            }
        }
    } else {
        for (int i = start_piece; i < end_piece; ++i) {
            if (_use_dynamic_allocate && _torrent.piece_priority(i) == 0) {
                continue;
            }
            std::list<int>::iterator pre_base_it = base_it;
            if (pre_base_it == _waiting_for_download_pieces.begin()) {
                pre_base_it == _waiting_for_download_pieces.end();
            } else {
                --pre_base_it;
            }
            _waiting_for_download_pieces.insert(base_it, i);
            base_it = pre_base_it;
        }
    }
}

void ClusterDownloader::update_allocated_pieces(int allocated_pieces) {
    boost::mutex::scoped_lock lock(_mutex);
    add_waiting_download_pieces(_allocated_pieces, allocated_pieces);
    _allocated_pieces = allocated_pieces;
    _cond.notify_all();
}

void ClusterDownloader::add_failed_piece(int piece_index) {
    boost::mutex::scoped_lock lock(_mutex);
    add_waiting_download_pieces(piece_index, piece_index + 1);
    _cond.notify_one();
}

void ClusterDownloader::notify_piece_finished(int piece_index) {
    scoped_lock lock(_mutex);
    _pieces_have.set_bit(piece_index);
    _waiting_for_disk_pieces.erase(piece_index);
    _cond.notify_one();
}

int ClusterDownloader::get_undownloaded_piece() {
    while (!_waiting_for_download_pieces.empty()) {
        int piece_index = _waiting_for_download_pieces.front();
        _waiting_for_download_pieces.pop_front();
        if (_pieces_have.get_bit(piece_index)) {
            continue;
        }
        if (_torrent.have_piece(piece_index)) {
            _pieces_have.set_bit(piece_index);
            continue;
        }
        return piece_index;
    };
    return -1;
}

int ClusterDownloader::get_piece_to_download() {
    int piece_index = -1;
    scoped_lock lock(_mutex);
    while (piece_index < 0 && !_should_stop) {
        DEBUG_LOG("CURRENT HDFS CACHE PIECES: %d", _waiting_for_disk_pieces.size());
        if (_waiting_for_disk_pieces.size() >= _max_hdfs_cache_pieces) {
            DEBUG_LOG("piece is exceed, wait ...");
            _cond.wait(lock);
            continue;
        }
        // get a piece from libtorrent not download
        piece_index = get_undownloaded_piece();
        if (piece_index < 0) {
            _cond.wait(lock);
        }
    }
    return piece_index;
}

void ClusterDownloader::notify_all() {
    _cond.notify_all();
}

void ClusterDownloader::download() {
    OPEN_LOG_R();
    int64_t downloaded_since_last = 0;
    scoped_array<char> buffer(new char[_btinfo->piece_length()]);
    while (!_should_stop) {
        int piece_index = get_piece_to_download();
        if (_should_stop) {
            break;
        }

        std::string errstr;
        int piece_size = _btinfo->piece_size(piece_index);
        DEBUG_LOG("will read piece %d", piece_index);
        if (_cluster_api->read_piece_content(
                _fs, _btinfo.get(), piece_index, buffer.get(), errstr) < 0) {
            WARNING_LOG("read piece %d fail: %s", piece_index, errstr.c_str());
            if (!_ignore_error) {
                _should_stop = true;
                _retval = -21;
                notify_all();
                break;
            }
            sleep(15);
            continue;
        }
        DEBUG_LOG("piece %d readed success", piece_index);

        {
            scoped_lock lock(_mutex);
            _total_download += piece_size;
        }

        if (_enable_dynamic_hash_check) {
            libtorrent::hasher piece_hash(buffer.get(), piece_size);
            _torrent.set_hash_piece(piece_index, piece_hash.final());
        } else if (!_disable_hash_check) {
            libtorrent::hasher piece_hash(buffer.get(), piece_size);
            if (piece_hash.final() != _btinfo->hash_for_piece(piece_index)) {  // hash失败
                WARNING_LOG("hash for piece %d failed", piece_index);
                if (!_ignore_error) {
                    _should_stop = true;
                    _retval = -22;
                    notify_all();
                    break;
                }
                sleep(15);
                continue;
            }
        }

        _torrent.add_piece(piece_index, buffer.get(), piece_size);
        add_waiting_disk_piece(piece_index);

        // 处理限速
        libtorrent::stat hdfs_stat;
        hdfs_stat.received_bytes(piece_size, 0);
        _torrent.add_stats(hdfs_stat);
        {
            scoped_lock lock(_mutex);
            int64_t p2p_total_downloaded = _torrent.status(0).total_download;
            downloaded_since_last = p2p_total_downloaded - _last_p2p_total_downloaded;
            _last_p2p_total_downloaded = p2p_total_downloaded;
        }
        if (_should_stop) {
            break;
        }
        _cluster_download_limit.bandwidth_limit(piece_size);
        _download_limit.bandwidth_limit(downloaded_since_last);
    }

    {
        scoped_lock lock(_mutex);
        ++_complete_threads_num;
    }
    DEBUG_LOG("cluster thread exit!");
    CLOSE_LOG_R();
}

}  // namespace tool
}  // namespace bbts
