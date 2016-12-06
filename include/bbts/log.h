/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   log.h
 *
 * @author liuming03
 * @date   2014-9-29
 * @brief 
 */

#ifndef OP_OPED_NOAH_BBTS_AGENT_LOG_H
#define OP_OPED_NOAH_BBTS_AGENT_LOG_H

#define LOGLINE(x) LOGLINE_(x)
#define LOGLINE_(x) #x
#define LOGINFO() "["__FILE__":"LOGLINE(__LINE__)"]"

#include <log4cpp/Category.hh>

#define OPEN_LOG_R()
#define CLOSE_LOG_R()
#define CLOSE_LOG() log4cpp::Category::getRoot().shutdown()

#define FATAL_LOG(fmt, arg...) \
do { \
  log4cpp::Category::getRoot().fatal(LOGINFO() fmt, ##arg); \
} while (0)

#define WARNING_LOG(fmt, arg...) \
do { \
  log4cpp::Category::getRoot().warn(LOGINFO() fmt, ##arg); \
} while (0)

#define NOTICE_LOG(fmt, arg...) \
do { \
  log4cpp::Category::getRoot().notice(LOGINFO() fmt, ##arg); \
} while (0)

#define TRACE_LOG(fmt, arg...) \
do { \
  log4cpp::Category::getRoot().info(LOGINFO() fmt, ##arg); \
} while (0)

#define DEBUG_LOG(fmt, arg...) \
do { \
  log4cpp::Category::getRoot().debug(LOGINFO() fmt, ##arg); \
} while (0)

#include <string>

namespace bbts {

enum LogLevel {
    LOG_LEVEL_QUITE   = 0,
    LOG_LEVEL_FATAL   = 1,
    LOG_LEVEL_WARNING = 2,
    LOG_LEVEL_NOTICE  = 3,
    LOG_LEVEL_TRACE   = 4,
    LOG_LEVEL_DEBUG   = 5
};

void init_log(
        const std::string &dir,
        const std::string &log,
        LogLevel level,
        LogLevel stderr_level);

bool load_log_by_configure(const std::string &dir, const std::string &conf);
}

#endif // OP_OPED_NOAH_BBTS_AGENT_LOG_H
