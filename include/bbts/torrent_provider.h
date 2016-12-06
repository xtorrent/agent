/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   torrent_provider.h
 *
 * @author liuming03
 * @date   2015-1-8
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_TORRENT_PROVIDER_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_TORRENT_PROVIDER_H

#include <string>
#include <vector>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

#include "bbts/routing.h"
#include "TorrentProviderService.h"

namespace bbts {

/**
 * @brief
 */
class TorrentProvider : public Routing {
public:
    TorrentProvider();
    ~TorrentProvider();

    bool get_torrent_file(const std::string &uri, std::vector<char> *buffer);

    bool get_infohash_torrent_file(
            const std::string &infohash,
            std::string *source,
            std::vector<char> *buffer);

    bool upload_torrent_file(
            const std::string &infohash,
            const std::string &source,
            const std::vector<char> &torrent_code);

    bool is_file_no_found() const {
        return _is_file_no_found;
    }
private:
    bool _is_file_no_found;
};

} // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_TORRENT_PROVIDER_H
