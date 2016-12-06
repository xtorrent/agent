/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   speed_limit.h
 *
 * @author liuming03
 * @date   2013-9-3
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_SPEED_LIMIT_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_SPEED_LIMIT_H

#include <time.h>
#include <boost/noncopyable.hpp>
#include <boost/thread/mutex.hpp>

namespace bbts {

/**
 * @brief
 */
class SpeedLimit : public boost::noncopyable {
public:
    SpeedLimit();
    SpeedLimit(int limit_rate);
    ~SpeedLimit();

    void bandwidth_limit(int amount) {
        bandwidth_limit(amount, _limit_rate);
    }

    void bandwidth_limit(int amount, int limit_rate);

    void set_limit_rate(int limit_rate) {
        _limit_rate = limit_rate;
    }

private:
    struct timeval _bw_start;
    int _lamt;
    int _limit_rate;
    boost::mutex _mutex;
};

}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_SPEED_LIMIT_H
