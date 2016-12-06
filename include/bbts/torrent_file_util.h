/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file torrent_file_util.h
 *
 * @author liuming03
 * @date 2013-12-12
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_TORRENT_FILE_UTIL_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_TORRENT_FILE_UTIL_H

#include <string>
#include <boost/function.hpp>
#include <boost/regex.hpp>
#include <libtorrent/torrent_info.hpp>

#include "bbts/log.h"
#include "bbts/regex_match.hpp"

namespace bbts {

struct MakeTorrentArgs {
    enum {
        BEST_HASH = 0x0001,
        SYMLINKS = 0x0002,
        FILE_HASH = 0x0004,
        PIECE_HASH = 0x0008,
        UPLOAD_TORRENT = 0x0010,
        NO_CALCULATE_HASH = 0x0020,
    };

    MakeTorrentArgs() : piece_size(0), flags(PIECE_HASH), creator("gingko 3.0") {}

    int piece_size;
    int flags;
    std::string path;
    std::string creator;
    std::string comment;
    std::string cluster_config;
    std::vector<std::string> web_seeds;
    std::vector<std::string> include_regex;
    std::vector<std::string> exclude_regex;
};

/**
 * @brief 制作种子文件
 *
 * @param  args(in)         所需参数列表
 * @param  infohash(out)    生成的种子文件的infohash
 * @param  torrent(out)     生成种子文件具体内容
 * @param  error_msg(out)   错误信息，如果无错误，则为空
 * @return 成功返回true，失败返回非false
 */
bool make_torrent(
        const MakeTorrentArgs &args,
        std::string* infohash,
        std::vector<char> *torrent,
        std::string* error_msg);
/**
 * @brief 打印输出种子文件中信息
 *
 * @param torrent_path 种子文件绝对路径
 * @return 成功返回true，失败返回非false
 */
bool dump_torrent(const std::string &torrent_path, bool debug);

struct DeleteFilesArgs {
    enum {
        DELETE_IN_TORRENT = 0x0001,
        SYMLINKS = 0x0002,
    };

    DeleteFilesArgs() : flags(0) {}

    int flags;
    std::string save_path;
    std::string torrent_path;
    std::string new_name;
};

bool delete_files_by_torrent(const DeleteFilesArgs &args);

bool make_torrent_dir(
        const libtorrent::torrent_info &ti,
        const std::string save_path,
        boost::function<bool (const std::string &)> pred);

template<typename IntegerType, typename Container>
void parse_need_download_files(
        const Container &include,
        const Container &exclude,
        const libtorrent::torrent_info &ti,
        std::vector<IntegerType> *file_priorities) {
    int num_files = ti.num_files();
    file_priorities->resize(num_files, 0);
    for (int i = 0; i < num_files; ++i) {
        std::string filepath = ti.files().file_path(i);
        RegexMatch regex_m(filepath);
        if (include.begin() == include.end()
                || std::find_if(include.begin(), include.end(), regex_m) != include.end()) {
            file_priorities->at(i) = 1;
        }
        if (std::find_if(exclude.begin(), exclude.end(), regex_m) != exclude.end()) {
            file_priorities->at(i) = 0;
        }
        if (file_priorities->at(i)) {
            DEBUG_LOG("will download file: %s", filepath.c_str());
        }
    }
}

}  // namespace bbts

#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_TORRENT_FILE_UTIL_H
