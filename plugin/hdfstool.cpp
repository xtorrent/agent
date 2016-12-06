/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   hdfstool.cpp
 *
 * @author liuming03
 * @date   2013-11-18
 * @brief
 */

#include "bbts/plugin.h"

#include <errno.h>
#include <string.h>

#include <string>
#include <vector>

#include <hdfs.h>

using std::string;
using std::vector;
using bbts::FileSlice;

extern "C" {

/*
 * 根据torrent_info的pieces的start位置,读取length个piece.
 *
 * */
int read_piece_content_from_cluster(
        const void* fs_point,
        const bbts::BTInfoInterface* info,
        int piece_index,
        char* data,
        string &errstr) {
    char *ptr = data;
    string prefix_path = info->get_prefix_path();
    hdfsFS fs = (hdfsFS)fs_point;
    //获取piece内的文件列表
    vector<FileSlice> files = info->map_block(piece_index, 0, info->piece_size(piece_index));
    for (vector<FileSlice>::iterator i = files.begin(); i != files.end(); ++i) {
        if (i->pad_file) {
            memset(ptr, 0, i->size);
            ptr += i->size;
            continue;
        }

        string filename = prefix_path + "/" + i->path;
        hdfsFile read_file = hdfsOpenFile(fs, filename.c_str(), O_RDONLY, 0, 0, 0);
        if (!read_file) {
            errstr = "Open hdfs file" + filename + " failed: " + strerror(errno);
            return -1;
        }
        //调用libhdfs接口读取文件
        int64_t readpos = i->offset;
        int64_t no_read_size = i->size;
        while (no_read_size > 0) {
            int64_t num_read_bytes = hdfsPread(fs, read_file, readpos, (void *)ptr, no_read_size);
            if (num_read_bytes < 0) {
                errstr = "read hdfs file " + filename + " failed: " + strerror(errno);
                hdfsCloseFile(fs, read_file);
                return -2;
            }
            if (num_read_bytes == 0) {
                errstr = "hdfs file " + filename + " size too small";
                hdfsCloseFile(fs, read_file);
                return -3;
            }
            readpos += num_read_bytes;
            ptr += num_read_bytes;
            no_read_size -= num_read_bytes;
        }
        hdfsCloseFile(fs, read_file);
    }
    return 0;
}

int read_file_from_cluster(const void* fs_point, const string &filename, vector<char> *buffer) {
    hdfsFS fs = (hdfsFS)fs_point;
    hdfsFileInfo *file_info = hdfsGetPathInfo(fs, filename.c_str());
    if (!file_info) {
        fprintf(stderr, "stat file %s failed: %s(%d)\n", filename.c_str(), strerror(errno), errno);
        return -1;
    }
    int size = file_info->mSize;
    hdfsFreeFileInfo(file_info, 1);
    if (size > 10 * 1024 * 1024) {
        fprintf(stderr, "file(%s) size(%d) too large\n", filename.c_str(), size);
        return -1;
    }

    hdfsFile file = hdfsOpenFile(fs, filename.c_str(), O_RDONLY, 0, 0, 0);
    if (!file) {
        fprintf(stderr, "open file(%s) failed: %s(%d)\n", filename.c_str(), strerror(errno), errno);
        return -1;
    }
    buffer->resize(size);
    if (hdfsPread(fs, file, 0, &(*buffer)[0], size) < 0) {
        fprintf(stderr, "read file(%s) failed: %s(%d)\n", filename.c_str(), strerror(errno), errno);
        hdfsCloseFile(fs, file);
        return -1;
    }
    hdfsCloseFile(fs, file);
    return size;
}

/*
 * 获取Hadoop Connect的连接对象
 *
 * */
void* connect_cluster(
        const string& host,
        const int port,
        const string& user,
        const string& passwd,
        const string& prefix_path) {
#if BUILD_BAIDU
    hdfsFS fs = hdfsConnectAsUser(host.c_str(), port, user.c_str(), passwd.c_str());
#else
    hdfsFS fs = hdfsConnectAsUser(host.c_str(), port, user.c_str());
#endif
    return (void *)(fs);
}

/*
 * 释放Hadoop Connect的连接对象
 *
 */
int close_cluster(void* fs_point) {
    hdfsFS fs = (hdfsFS)fs_point;
    return hdfsDisconnect(fs);
}

}
