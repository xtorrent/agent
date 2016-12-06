/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   timer_util.cpp
 *
 * @author liuming03
 * @date   2015年1月19日
 * @brief 
 */

#include "bbts/timer_util.h"

#include <boost/bind.hpp>
#include <boost/ref.hpp>

#include "bbts/log.h"

namespace bbts {

static void timer_callback(
        const std::string &timer_name,
        boost::asio::deadline_timer &timer,
        const boost::posix_time::time_duration &expiry_time,
        boost::function<void (void)> callback,
        const boost::system::error_code &ec,
        bool rejoin) {
    if (ec) {
        DEBUG_LOG("%s timer: %s", timer_name.c_str(), ec.message().c_str());
        return;
    }
    callback();
    if (rejoin) {
        timer.expires_from_now(expiry_time);
        timer.async_wait(boost::bind(
                timer_callback, timer_name, boost::ref(timer), expiry_time, callback, _1, rejoin));
    }
}

void timer_run_once(
        const std::string &timer_name,
        boost::asio::deadline_timer &timer,
        const boost::posix_time::time_duration &expiry_time,
        boost::function<void ()> callback) {
    timer.expires_from_now(expiry_time);
    timer.async_wait(boost::bind(
            timer_callback, timer_name, boost::ref(timer), expiry_time, callback, _1, false));
}

void timer_run_cycle(
        const std::string &timer_name,
        boost::asio::deadline_timer &timer,
        const boost::posix_time::time_duration &expiry_time,
        boost::function<void (void)> callback) {
    timer.expires_from_now(expiry_time);
    timer.async_wait(boost::bind(
            timer_callback, timer_name, boost::ref(timer), expiry_time, callback, _1, true));
}

} // namespace bbts

