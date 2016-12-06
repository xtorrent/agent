/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   bt_info.h
 *
 * @author liuming03
 * @date   2013-05-29
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_BT_INFO_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_BT_INFO_H

#include "bbts/plugin.h"

#include <libtorrent/torrent_info.hpp>

namespace bbts {

class BTInfo : public BTInfoInterface {
public:
    BTInfo(const libtorrent::torrent_info& info, const std::string &path) :
            _info(info),
            _path(path) {}

    virtual ~BTInfo() {}

    virtual std::vector<FileSlice> map_block(int piece, int64_t offset, int size) const {
        std::vector<libtorrent::file_slice> vfs = _info.map_block(piece, offset, size);
        std::vector<FileSlice> nvfs;
        for (std::vector<libtorrent::file_slice>::iterator i = vfs.begin(); i != vfs.end(); ++i) {
            libtorrent::file_entry file_entry = _info.file_at(i->file_index);
            FileSlice fs;
            fs.path = file_entry.path;
            fs.file_size = file_entry.size;
            fs.pad_file = file_entry.pad_file;
            fs.mode = file_entry.mode;
            fs.file_index = i->file_index;
            fs.offset = i->offset;
            fs.size = i->size;
            nvfs.push_back(fs);
        }
        return nvfs;
    }

    virtual int piece_size(unsigned int index) const {
        return _info.piece_size(index);
    }

    virtual std::string get_prefix_path() const {
        return _path;
    }

    inline int64_t total_size() const {
        return _info.total_size();
    }

    inline int piece_length() const {
        return _info.piece_length();
    }

    inline int num_pieces() const {
        return _info.num_pieces();
    }

    inline int num_files() const {
        return _info.num_files();
    }

    inline libtorrent::sha1_hash hash_for_piece(int index) const {
        return _info.hash_for_piece(index);
    }

private:
    libtorrent::torrent_info _info;
    std::string _path;
};

}  // namespace bbts

#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_BT_INFO_H
