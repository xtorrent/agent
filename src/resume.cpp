/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   resume.cpp
 *
 * @author liuming03
 * @date   2015年6月6日
 * @brief 
 */
#include "bbts/tool/resume.h"

#include <string.h>
#include <sys/stat.h>

#include <boost/system/error_code.hpp>

#include "bbts/file.h"
#include "bbts/log.h"

using std::string;
using std::vector;

using boost::system::error_code;

namespace bbts {
namespace tool {

Resume::Resume() {}

Resume::~Resume() {}

void Resume::set_filename(const string &filename) {
    _filename = filename;
    _tmp_filename = filename + ".tmp";
}

void Resume::load(vector<char> *buffer) const {
    if (_filename.empty()) {
        return;
    }
    vector<char> tmp_buffer;
    error_code ec;
    File::read(_filename, &tmp_buffer, ec, 10 * 1024 * 1024);
    if (ec) {
        WARNING_LOG("load resume %s failed: %s, ignore", _filename.c_str(), ec.message().c_str());
        return;
    }
    buffer->swap(tmp_buffer);
    NOTICE_LOG("load resume %s success", _filename.c_str());
}

void Resume::save(const vector<char> &buffer) const {
    error_code ec;
    File::write(_tmp_filename, buffer, ec);
    if (ec) {
        WARNING_LOG("save resume %s failed: %s", _tmp_filename.c_str(), ec.message().c_str());
        return;
    }
    if (rename(_tmp_filename.c_str(), _filename.c_str()) < 0) {
        WARNING_LOG("rename to resume %s failed: %s", _filename.c_str(), strerror(errno));
        return;
    }
    DEBUG_LOG("save reume %s success", _filename.c_str());
}

void Resume::remove() const {
    unlink(_filename.c_str());
    unlink(_tmp_filename.c_str());
}

bool Resume::check_file() const {
    if (_filename.empty()) {
        return false;
    }
    struct stat stat_buf;
    if (stat(_filename.c_str(), &stat_buf) != 0) {
        // some error, think file not exist
        return false;
    }

    if (S_ISREG(stat_buf.st_mode)) {
        return true;
    }

    return false;
}

} // namespace tool
} // namespace bbts
