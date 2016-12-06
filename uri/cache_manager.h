/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   cache_manager.h
 *
 * @author liuming03
 * @date   2015-2-8
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_CACHE_MANAGER_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_CACHE_MANAGER_H

#include <string>
#include <vector>

#include <boost/asio/io_service.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>

#include "bbts/encode.h"
#include "bbts/lazy_singleton.hpp"
#include "uri_piece_request.h"

namespace bbts {

namespace multi_index = boost::multi_index;

struct BlockItem {
    std::string infohash;
    uint32_t piece_index;
    uint32_t piece_offset;
    uint32_t size;
    time_t expired_time;
    boost::shared_ptr<Buffer> buffer;

    BlockItem() : piece_index(0), piece_offset(0), size(0), expired_time(0) {}

    BlockItem(const URIPieceRequest &r) :
        piece_index(r.piece_index),
        piece_offset(r.piece_offset),
        size(r.size),
        expired_time(0) {
        if (hex_decode(r.infohash, &infohash)) {
            infohash = r.infohash;
        }
    }

    bool operator < (const BlockItem &o) const {
        if (infohash != o.infohash) {
            return infohash < o.infohash;
        }
        if (piece_index != o.piece_index) {
            return piece_index < o.piece_index;
        }
        if (piece_offset != o.piece_offset) {
            return piece_offset < o.piece_offset;
        }
        return size < o.size;
    }
};

/**
 * @brief
 */
class CacheManager {
public:
    typedef multi_index::multi_index_container<BlockItem, multi_index::indexed_by<
                    multi_index::ordered_unique<multi_index::identity<BlockItem> >,
                    multi_index::ordered_non_unique<
                            multi_index::member<BlockItem, time_t, &BlockItem::expired_time> >
                    > > BlockCache; // TODO change to hash index
    typedef BlockCache::nth_index<0>::type CacheIndex;
    typedef BlockCache::nth_index<1>::type ExpiredCacheIndex;

    CacheManager(boost::asio::io_service &ios);

    ~CacheManager();

    boost::shared_ptr<Buffer> fetch(const URIPieceRequest &piece_request,
            std::string &error_msg);

    boost::shared_ptr<Buffer> calculate_hash(
            const URIPieceRequest &piece_request,
            std::string &error_msg);
    
    void set_cache_size(int cache_size) {
        if (cache_size >= 1024) {
            // min is 16M(1024*16K)
            _max_cache_size = cache_size;
        }
    }

    void set_cache_expire_time(int cache_expire_time) {
        if (cache_expire_time >= 5) {
            // min is 5s
            _cache_expired_time = cache_expire_time;
        }
    }

    void set_read_line_size(int32_t read_line_size) {
        _read_line_size = read_line_size;
    }

private:
    bool insert_into_block_cache(const URIPieceRequest &piece_request,
            BlockItem *item, int block_num, std::string &error_msg);

    bool insert_into_hash_check_cache(
            const URIPieceRequest &piece_request,
            BlockItem *item,
            std::string &error_msg);

    void clean_timer_callback();

    /**
     * if size is out of max_cache_size, if so, delete outdate
     * if all cache aren't outdate, delete the 10% blocks
     */
    void delete_overdue_when_cache_full(
            BlockCache &cache,
            boost::mutex &mutex,
            time_t current_time);

    void clean_expired_cache(BlockCache &cache, boost::mutex &mutex, time_t current_time);

    std::vector<boost::shared_ptr<Buffer> > read_block(const URIPieceRequest &piece_request,
            int block_num,
            std::string &error_msg);

    boost::shared_ptr<Buffer> get_all_blocks_by_request(
        const URIPieceRequest &piece_request,
        std::string &error_msg);

    static const int kDefaultMaxCacheSize = 4096;          // max cache size 
    static const int kDefaultCacheExpiredTime = 10;        // cache expired time, 10s
    static const int kCleanPeriod = 1;                     // clear cache backup timer period, 1s
    static const uint32_t kReadLineSize = 262144;          // read line size, 256K
    static const int32_t kDefaultPieceRequestSize = 16384; // default request size, 16K

    BlockCache _block_cache;
    BlockCache _hash_check_cache;
    int _max_cache_size;
    int _cache_expired_time;
    uint32_t _read_line_size;
    boost::mutex _block_mutex;
    boost::mutex _hash_check_mutex;
    boost::asio::io_service &_ios;
    boost::asio::deadline_timer _clean_timer;
};

} // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_CACHE_MANAGER_H
