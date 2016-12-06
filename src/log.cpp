/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   log.cpp
 *
 * @author liuming03
 * @date   2014年10月8日
 * @brief 
 */

#include "bbts/log.h"

using std::string;

#include <log4cpp/RollingFileAppender.hh>
#include <log4cpp/OstreamAppender.hh>
#include <log4cpp/PatternLayout.hh>
#include <log4cpp/PropertyConfigurator.hh>

namespace bbts {

static void set_log_level(log4cpp::Category *category, LogLevel level) {
    switch (level) {
    case LOG_LEVEL_DEBUG:
        category->setPriority(log4cpp::Priority::DEBUG);
        break;
    case LOG_LEVEL_TRACE:
        category->setPriority(log4cpp::Priority::INFO);
        break;
    case LOG_LEVEL_NOTICE:
        category->setPriority(log4cpp::Priority::NOTICE);
        break;
    case LOG_LEVEL_WARNING:
        category->setPriority(log4cpp::Priority::WARN);
        break;
    case LOG_LEVEL_FATAL:
        category->setPriority(log4cpp::Priority::FATAL);
        break;
    default:
        break;
    }
}

void init_log(const string &dir, const string &log, LogLevel level, LogLevel stderr_level) {
    log4cpp::PatternLayout* file_layout = new log4cpp::PatternLayout();
    file_layout->setConversionPattern("[%p][%d{%m-%d %H:%M:%S}][%t]%m%n");
    string logfile = dir + '/' + log;
    log4cpp::Appender* file_appender = new log4cpp::RollingFileAppender(
            log,
            logfile,
            10 * 1024 * 1024,
            1);
    file_appender->setLayout(file_layout);

    log4cpp::PatternLayout* stderr_layout = new log4cpp::PatternLayout();
    stderr_layout->setConversionPattern("[%p][%d{%m-%d %H:%M:%S}][%t]%m%n");
    log4cpp::OstreamAppender* stderr_appender = new log4cpp::OstreamAppender(
            "stderr_appender",
            &std::cerr);
    stderr_appender->setLayout(stderr_layout);
    log4cpp::Category& category = log4cpp::Category::getRoot();
    category.setAdditivity(false);
    category.setAppender(file_appender);
    category.addAppender(stderr_appender);
    set_log_level(&category, stderr_level);
}

bool load_log_by_configure(const string &dir, const string &conf) {
    string conf_file = dir + '/' + conf;
    try {
        log4cpp::PropertyConfigurator::configure(conf_file);
    } catch (log4cpp::ConfigureFailure& f) {
        return false;
    }
    return true;
}

}

