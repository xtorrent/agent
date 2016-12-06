/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   file.cpp
 *
 * @author liuming03
 * @date   2014-12-29
 * @brief 
 */

#include "bbts/file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

#include "bbts/cluster_api.h"
#include "bbts/error_category.h"
#include "bbts/options_parse.h"
#include "bbts/path.h"

using std::string;
using std::vector;

using boost::system::error_code;
using boost::system::system_category;

namespace bbts {

bool File::lock(const std::string &filename, boost::system::error_code &ec) {
    int fd = open(filename.c_str(), O_WRONLY | O_CREAT, 0600);
    if (fd < 0) {
        ec.assign(errno, system_category());
        return false;
    }
    struct flock flockbuf = { F_WRLCK, 0, SEEK_SET, 0, 0 };
    if (fcntl(fd, F_SETLK, &flockbuf) < 0) {
        ec.assign(errno, system_category());
        return false;
    }
    return true;
}

int64_t File::size(const std::string &filename, error_code &ec) {
    struct stat statbuf;
    if (::stat(filename.c_str(), &statbuf) != 0) {
        ec.assign(errno, system_category());
        return -1;
    }
    return statbuf.st_size;
}

int File::write(const string &filename, const vector<char> &buffer, error_code &ec) {
    FILE *fp = fopen(filename.c_str(), "wb+");
    if (!fp) {
        ec.assign(errno, system_category());
        return -1;
    }
    int writed = fwrite(&buffer[0], 1, buffer.size(), fp);
    if (writed < 0) {
        ec.assign(errno, system_category());
    }
    fclose(fp);
    return writed;
}

namespace detail {

int read_cluster_file(
        const message::SourceURI &file_entry,
        vector<char> *buffer,
        error_code &ec,
        int64_t limit) {
    ClusterAPI *api = ClusterAPI::get_cluster_api(file_entry.protocol());
    if (!api) {
        ec.assign(errors::CLUSTER_NOT_SUPPORT, get_error_category());
        return -1;
    }
    void *fs = api->connect_cluster_with_timeout(file_entry, 120);
    if (!fs) {
        ec.assign(errors::CLUSTER_CONNECT_FAIL, get_error_category());
        return -1;
    }

    int ret = api->read_file(fs, file_entry.path(), buffer);
    if (ret < 0) {
        ec.assign(errors::CLUSTER_READ_FILE_FAIL, get_error_category());
    }
    return ret;
}

int read_local_file(
        const message::SourceURI &file_entry,
        vector<char> *buffer,
        error_code &ec,
        int64_t limit) {
    struct stat statbuf;
    if (::stat(file_entry.path().c_str(), &statbuf) != 0) {
        ec.assign(errno, system_category());
        return -1;
    }
    if (limit > 0 && statbuf.st_size > limit) {
        ec.assign(errors::FILE_TOO_LARGE, get_error_category());
        return -1;
    }

    FILE *file = fopen(file_entry.path().c_str(), "rb");
    if (!file) {
        ec.assign(errno, system_category());
        return -1;
    }
    buffer->resize(statbuf.st_size);
    if (fread(&(*buffer)[0], 1, statbuf.st_size, file) < 0) {
        ec.assign(errno, system_category());
        fclose(file);
        return -1;
    }
    fclose(file);
    return statbuf.st_size;
}

} // namespace detail

int File::read(const string &filename, vector<char> *buffer, error_code &ec, int64_t limit) {
    message::SourceURI file_entry;
    if (!parse_uri_entry(filename, &file_entry)) {
        ec.assign(errors::FILE_URI_INVALID, get_error_category());
        return -1;
    }
    if (file_entry.protocol() == "file") {
        return detail::read_local_file(file_entry, buffer, ec, limit);
    } else if (file_entry.protocol() == "nfs" || file_entry.protocol() == "hdfs") {
        return detail::read_cluster_file(file_entry, buffer, ec, limit);
    }
    ec.assign(errors::FILE_SYSTEM_PROTOCOL_NOT_SUPPORT, get_error_category());
    return -1;
}

} // namespace bbts

