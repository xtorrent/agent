/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/*
 * @file   plugin.h
 *
 * @author liuming03
 * @date   2013-05-29
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_PLUGIN_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_PLUGIN_H

#include <stdint.h>
#include <sys/types.h>

#include <string>
#include <vector>

namespace bbts {

struct FileSlice {
    int file_index;
    std::string path;
    int64_t file_size;  // the size of this file
    mode_t mode;
    int64_t offset;
    int64_t size;
    bool pad_file :1;
};

class BTInfoInterface {
public:
    BTInfoInterface() {}
    virtual ~BTInfoInterface() {};

    virtual std::vector<FileSlice> map_block(int piece, int64_t offset, int size) const = 0;
    virtual int piece_size(unsigned int index) const = 0;
    virtual std::string get_prefix_path() const = 0;
};

}  // namespace bbts

extern "C" {

/*
 * 根据torrent_info的pieces的start位置, 读取length个piece.
 * @param fs_point: HDFSConect对象
 * @param info: BTInfoInterface接口
 * @param piece_index: piece索引
 * @param data: 返回的数据缓存
 * @param errstr: 保存错误信息
 * 
 * @retval  0: 成功
 *         -1: 打开hdfs文件错误
 *         -2: 读hdfs文件错误
 *         -3: 读取的字节超过size大小错误
 * */
int read_piece_content_from_cluster(
        const void* fs_point,
        const bbts::BTInfoInterface* info,
        int piece_index,
        char* data,
        std::string &errstr);

/*
 * 获取cluster connect对象
 *
 * @retval null: 获取失败
 *         其他: 获取的连接对象句柄
 * */
void* connect_cluster(
        const std::string& host,
        const int port,
        const std::string& user,
        const std::string& passwd,
        const std::string& prefix_path);

/*
 * 释放cluster connect对象
 * @param fs: cluster connect 句柄对象
 * */
int close_cluster(void* fs);

int read_file_from_cluster(const void* fs, const std::string &filename, std::vector<char> *buffer);

}

#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_PLUGIN_H
