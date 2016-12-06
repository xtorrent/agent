/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   speed_limit.cpp
 *
 * @author liuming03
 * @date   2013-9-3
 * @brief 
 */

#include "bbts/speed_limit.h"

#include <errno.h>

namespace bbts {

SpeedLimit::SpeedLimit() : _lamt(0), _limit_rate(0) {
    gettimeofday(&_bw_start, NULL);
}

SpeedLimit::SpeedLimit(int limit_rate) : _lamt(0), _limit_rate(limit_rate) {
    gettimeofday(&_bw_start, NULL);
}

SpeedLimit::~SpeedLimit() {}

void SpeedLimit::bandwidth_limit(int amount, int limit_rate) {
    if (amount <= 0 || limit_rate <= 0) {
        return;
    }

    boost::mutex::scoped_lock lock(_mutex);
    _lamt += amount;
    struct timeval bw_end;
    gettimeofday(&bw_end, NULL);
    timersub(&bw_end, &_bw_start, &bw_end);
    if (!timerisset(&bw_end)) {
        return;
    }

    uint64_t waitlen = (uint64_t)1000000L * _lamt / limit_rate;
    struct timeval should_use_time;
    should_use_time.tv_sec = waitlen / 1000000L;
    should_use_time.tv_usec = waitlen % 1000000L;
    if (timercmp(&should_use_time, &bw_end, >)) {
        timersub(&should_use_time, &bw_end, &bw_end);
        struct timespec ts;
        struct timespec rm;
        ts.tv_sec = bw_end.tv_sec;
        ts.tv_nsec = bw_end.tv_usec * 1000;
        while (nanosleep(&ts, &rm) == -1) {
            if (errno != EINTR) {
                break;
            }
            ts = rm;
        }
    }

    _lamt = 0;
    gettimeofday(&_bw_start, NULL);
    return;
}

}  // namespace bbts
