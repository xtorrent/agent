/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   cache_manager.cpp
 *
 * @author hechaobin01 
 * @date   2015-2-8
 * @brief 
 */
#include "cache_manager.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include <libtorrent/hasher.hpp>

#include "bbts/log.h"
#include "bbts/timer_util.h"
#include "io.hpp"

using std::string;
using std::vector;
using boost::shared_ptr;
using boost::system::error_code;

namespace bbts {

CacheManager::CacheManager(boost::asio::io_service &ios) :
        _max_cache_size(kDefaultMaxCacheSize),
        _cache_expired_time(kDefaultCacheExpiredTime),
        _read_line_size(kReadLineSize),
        _ios(ios),
        _clean_timer(_ios) {
    timer_run_cycle("clear cache timer", _clean_timer, boost::posix_time::seconds(kCleanPeriod),
            boost::bind(&CacheManager::clean_timer_callback, this));
}

CacheManager::~CacheManager() {}

shared_ptr<Buffer> CacheManager::fetch(
        const URIPieceRequest &piece_request,
        std::string &error_msg) {
    BlockItem item(piece_request);

    int block_num = _read_line_size / kDefaultPieceRequestSize;
    time_t current_time = time(NULL);
    {
        boost::mutex::scoped_lock lock(_block_mutex);
        CacheIndex& cache_index = _block_cache.get<0>();
        CacheIndex::iterator ii = cache_index.find(item);
        if (ii != cache_index.end()) {
            // in cache and not expire, return direct
            if (current_time - ii->expired_time <= _cache_expired_time) {
                return ii->buffer;
            } else {
                // expire, delete it
                cache_index.erase(ii);
            }
        }

        if (piece_request.piece_length == 0) {
            block_num = 1;
        } else if (_block_cache.size() > static_cast<size_t>(_max_cache_size)) {
            // TODO: how much?
            block_num = 8;
        } else {
            // calculate readv size, check the newest block index
            ii = cache_index.lower_bound(item);
            for (; ii != cache_index.end(); ++ii) {
                if (ii->infohash == item.infohash
                    && current_time - ii->expired_time <= _cache_expired_time) {
                    break;
                }
            }

            if (ii != cache_index.end()) {
                int64_t total_bytes = ii->piece_index - item.piece_index;
                total_bytes *= piece_request.piece_length;
                if (ii->piece_index == item.piece_index) {
                    total_bytes += ii->piece_offset - item.piece_offset;
                } else {
                    total_bytes = total_bytes - item.piece_offset + ii->piece_offset;
                }
                block_num = total_bytes / kDefaultPieceRequestSize;
                DEBUG_LOG("ii.index:%d,offset:%d,item.index:%d,offset:%d;total:%ld,block_num:%d",
                        ii->piece_index, ii->piece_offset, item.piece_index,
                        item.piece_offset, total_bytes, block_num);
            }
        }  // if block_size
    }  // mutex lock

    if (!insert_into_block_cache(piece_request, &item, block_num, error_msg)) {
        WARNING_LOG("insert block cache failed:%s, infohash:%s, piece_index:%d, piece_offset:%d",
                error_msg.c_str(), item.infohash.c_str(),
                item.piece_index, item.piece_offset);
        return shared_ptr<Buffer>();
    }

    return item.buffer;
}

bool CacheManager::insert_into_block_cache(
        const URIPieceRequest &piece_request,
        BlockItem *item,
        int block_num,
        std::string &error_msg) {
    if (item == NULL) {
        return false;
    }
    time_t current_time = time(NULL);
    std::vector<shared_ptr<Buffer> > buffer_list;
    buffer_list = read_block(piece_request, block_num, error_msg);
    if (!error_msg.empty() || buffer_list.size() == 0) {
        return false;
    }

    item->expired_time = current_time;
    std::vector<shared_ptr<Buffer> >::iterator it = buffer_list.begin();
    item->buffer = *it;

    {
        boost::mutex::scoped_lock lock(_block_mutex);
        BlockItem tmp_item;
        tmp_item.expired_time = current_time;
        tmp_item.infohash = item->infohash;
        tmp_item.size = piece_request.size;

        uint32_t offset = piece_request.piece_offset;
        uint32_t piece_index = piece_request.piece_index;
        CacheIndex& cache_index = _block_cache.get<0>();
        for (; it != buffer_list.end(); ++it) {
            shared_ptr<Buffer> buf = *it;
            tmp_item.piece_index = piece_index;
            tmp_item.piece_offset = offset;
            offset += piece_request.size;
            if (offset > piece_request.piece_length - kDefaultPieceRequestSize) {
                offset = 0;
                ++piece_index;
            }
            tmp_item.buffer = buf;

            // no full, just insert and ignore return value
            cache_index.insert(tmp_item);
        }  // for it != buffer_list.end()
    
        if (_max_cache_size > 0
            && _block_cache.size() <= static_cast<size_t>(_max_cache_size)) {
            return true;
        }
    }  // scoped_lock

    delete_overdue_when_cache_full(_block_cache, _block_mutex, current_time);

    return true;
}

std::vector<shared_ptr<Buffer> > CacheManager::read_block(
        const URIPieceRequest &piece_request,
        int block_num,
        std::string &error_msg) {
    error_msg.clear();
    std::vector<shared_ptr<Buffer> > buffer_list;

    int last_piece_length = 0;
    for (vector<FilePiece>::const_iterator i = piece_request.file_pieces.begin();
            i != piece_request.file_pieces.end(); ++i) {
        string filename = i->name.empty() ? piece_request.root_path :
                piece_request.root_path + '/' + i->name;

        // try to read _read_line_size once by using readv
        struct stat stat_buf;
        if (stat(filename.c_str(), &stat_buf) != 0) {
            error_msg = "stat file " + filename + " failed: " + strerror(errno);
            WARNING_LOG("stat file %s failed:%s", filename.c_str(), strerror(errno));
            buffer_list.clear();
            return buffer_list;
        }

        off_t file_size = stat_buf.st_size;
        off_t total_readed = _read_line_size;
        if (i->size < kDefaultPieceRequestSize) {
            total_readed = i->size;
        } else {
            if (total_readed > static_cast<off_t>(file_size - i->offset)) {
                off_t diff_num = (file_size - i->offset) / kDefaultPieceRequestSize;
                if (diff_num == 0) {
                    total_readed = file_size - i->offset;
                } else if (diff_num > block_num){
                    total_readed = block_num * kDefaultPieceRequestSize;
                } else {
                    total_readed = diff_num * kDefaultPieceRequestSize;
                }
            }
        }

        int block_size = total_readed / piece_request.size;
        if (block_size < 1) {
            block_size = 1;
        }
        if (block_size > block_num) {
            block_size = block_num;
            total_readed = block_num * kDefaultPieceRequestSize;
        }

        struct iovec *iov = new struct iovec[block_size];
        int loop_start = 0;
        DEBUG_LOG("total_readed:%d, piece_size:%d, block_size:%d, loop_start:%d, last_piece_length:%d, file->size:%d, file->offset:%d",
                total_readed, piece_request.size, block_size, loop_start, last_piece_length, i->size, i->offset);
        if (last_piece_length > 0) {
            shared_ptr<Buffer> tmp_buffer = buffer_list.at(buffer_list.size() - 1);
            if (!tmp_buffer) {
                error_msg = "internal error when read file " + filename;
                WARNING_LOG("buffer_list.size():%d, but last is empty, return!", buffer_list.size());
                delete[] iov;
                buffer_list.clear();
                return buffer_list;
            }
            iov[0].iov_base = static_cast<void *>(&(*tmp_buffer)[last_piece_length]);
            iov[0].iov_len = i->size;
            loop_start = 1;
        }
        for (int j = loop_start; j < block_size; ++j) {
            shared_ptr<Buffer> buf(new Buffer(piece_request.size));
            iov[j].iov_base = static_cast<void *>(&(*buf)[0]);
            iov[j].iov_len = piece_request.size;
            buffer_list.push_back(buf);
        }

        int fd = open(filename.c_str(), O_RDONLY);
        if (fd == -1) {
            error_msg = "open file " + filename + " failed:" + strerror(errno);
            WARNING_LOG("%s open: %s", filename.c_str(), strerror(errno));
            delete[] iov;
            buffer_list.clear();
            return buffer_list;
        }

        if (lseek(fd, i->offset, SEEK_SET) != static_cast<off_t>(i->offset)) {
            error_msg = "lseek file " + filename + " failed:" + strerror(errno);
            WARNING_LOG("%s lseek: %s", filename.c_str(), strerror(errno));
            close(fd);
            delete[] iov;
            buffer_list.clear();
            return buffer_list;
        }

        int readed = readv(fd, iov, block_size);
        if (readed != total_readed) {
            error_msg = "readv file " + filename + " failed:" + strerror(errno);
            WARNING_LOG("%s readv: %s, readed:%d, required:%d",
                    filename.c_str(), strerror(errno), readed, total_readed);
            close(fd);
            delete[] iov;
            buffer_list.clear();
            return buffer_list;
        }
        delete[] iov;
        close(fd);

        DEBUG_LOG("piece_request.index:%d, offset:%d, readed is %d, size is %d",
                piece_request.piece_index, piece_request.piece_offset, readed, piece_request.size);

        last_piece_length += readed % piece_request.size;
        last_piece_length = last_piece_length % kDefaultPieceRequestSize;
    };
    return buffer_list;
}

shared_ptr<Buffer> CacheManager::calculate_hash(
        const URIPieceRequest &piece_request,
        std::string &error_msg) {
    BlockItem item(piece_request);
    time_t current_time = time(NULL);
    {
        boost::mutex::scoped_lock lock(_hash_check_mutex);
        CacheIndex& cache_index = _hash_check_cache.get<0>();
        CacheIndex::iterator ii = cache_index.find(item);
        if (ii != cache_index.end()) {
            // in cache and not expire, return direct
            if (current_time - ii->expired_time <= _cache_expired_time) {
                return ii->buffer;
            } else {
                // expire, delete it
                cache_index.erase(ii);
            }
        }
    }  // mutex lock

    if (!insert_into_hash_check_cache(piece_request, &item, error_msg)) {
        WARNING_LOG("insert hash check cache failed:%s, infohash:%s, piece_index:%d",
                error_msg.c_str(), item.infohash.c_str(), item.piece_index);
        return shared_ptr<Buffer>();
    }

    return item.buffer;
}

bool CacheManager::insert_into_hash_check_cache(
        const URIPieceRequest &piece_request,
        BlockItem *item,
        std::string &error_msg) {
    if (item == NULL) {
        return false;
    }

    time_t current_time = time(NULL);
    shared_ptr<Buffer> buffer = get_all_blocks_by_request(piece_request, error_msg);
    if (!error_msg.empty() || !buffer) {
        return false;
    }

    item->expired_time = current_time;
    char *ptr = &(*buffer)[0];
    libtorrent::hasher h(ptr, buffer->size());
    libtorrent::sha1_hash hash = h.final();

    item->buffer.reset(new Buffer(hash.size));
    ptr = &(*item->buffer)[0];
    std::copy(hash.begin(), hash.end(), ptr); 

    {
        boost::mutex::scoped_lock lock(_hash_check_mutex);
        CacheIndex& cache_index = _hash_check_cache.get<0>();
        cache_index.insert(*item);
        if (_max_cache_size > 0
            && _hash_check_cache.size() <= static_cast<size_t>(_max_cache_size)) {
            return true;
        }
    }  // scoped_lock

    delete_overdue_when_cache_full(_hash_check_cache, _hash_check_mutex, current_time);

    return true;
}

void CacheManager::delete_overdue_when_cache_full(
        BlockCache &cache,
        boost::mutex &mutex,
        time_t current_time) {
    boost::mutex::scoped_lock lock(mutex);
    ExpiredCacheIndex& expired_index = cache.get<1>();
    time_t expired_time = current_time - _cache_expired_time;
    ExpiredCacheIndex::iterator ei = expired_index.lower_bound(expired_time);
    if (ei == expired_index.begin()) {
        // not found, delete the first 10%
        int delete_size = static_cast<int>(_max_cache_size * 0.1);
        for (int i = 0; i < delete_size; ++i) {
            ++ei;
        }
    }

    expired_index.erase(expired_index.begin(), ei);
}

shared_ptr<Buffer> CacheManager::get_all_blocks_by_request(
        const URIPieceRequest &piece_request,
        std::string &error_msg) {
    error_msg.clear();
    shared_ptr<Buffer> buffer(new Buffer(piece_request.size));
    char *ptr = &(*buffer)[0];

    // turn piece_request into kDefaultPieceRequestSize requests
    shared_ptr<Buffer> tmp_buffer;
    URIPieceRequest tmp_request;
    tmp_request.piece_index = piece_request.piece_index;
    tmp_request.infohash = piece_request.infohash;
    tmp_request.root_path = piece_request.root_path;
    tmp_request.piece_length = piece_request.piece_length;

    uint32_t current_length = piece_request.size;
    int block_index = 0;
    int file_index = 0;
    int32_t current_file_size = 0;
    while (current_length > 0) {
        uint32_t block_offset = block_index * kDefaultPieceRequestSize;
        tmp_request.piece_offset = piece_request.piece_offset + block_offset;

        tmp_request.size = kDefaultPieceRequestSize;
        if (block_offset + tmp_request.size > piece_request.size) {
            tmp_request.size = piece_request.size - block_offset;
        }
        current_length -= tmp_request.size;

        tmp_request.file_pieces.clear();
        int32_t file_size = 0;
        uint32_t request_size = tmp_request.size;
        while (file_size < static_cast<int32_t>(tmp_request.size)) {
            const FilePiece &request_file_piece = piece_request.file_pieces.at(file_index);

            FilePiece file_piece;
            file_piece.name = request_file_piece.name;
            file_piece.offset = request_file_piece.offset + current_file_size;

            if (request_file_piece.size > static_cast<int32_t>(current_file_size + request_size)) {
                file_piece.size = request_size;
                file_size += request_size;
                current_file_size += request_size;
            } else {
                file_piece.size = request_file_piece.size - current_file_size;
                file_size += file_piece.size;
                request_size -= file_piece.size;
                current_file_size = 0;
                ++file_index;
            }

            tmp_request.file_pieces.push_back(file_piece);
        }

        tmp_buffer = fetch(tmp_request, error_msg);
        if (!error_msg.empty() || !tmp_buffer) {
            return shared_ptr<Buffer>();
        }
        std::copy(tmp_buffer->begin(), tmp_buffer->end(), ptr);
        ptr += tmp_buffer->size();
        ++block_index;
    }

    return buffer;
}

void CacheManager::clean_timer_callback() {
    time_t current_time = time(NULL);
    clean_expired_cache(_block_cache, _block_mutex, current_time);
    clean_expired_cache(_hash_check_cache, _hash_check_mutex, current_time);
}

void CacheManager::clean_expired_cache(
        BlockCache &cache,
        boost::mutex &mutex,
        time_t current_time) {
    boost::mutex::scoped_lock lock(mutex);
    ExpiredCacheIndex& expired_index = cache.get<1>();
    ExpiredCacheIndex::iterator ei = expired_index.lower_bound(current_time - _cache_expired_time);
    expired_index.erase(expired_index.begin(), ei);
}

} // namespace bbts
