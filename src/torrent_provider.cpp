/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   torrent_provider.cpp
 *
 * @author liuming03
 * @date   2015-1-8
 * @brief 
 */
#include "bbts/torrent_provider.h"

#include <algorithm>

#include <boost/shared_ptr.hpp>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/Thrift.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TSocket.h>
#include <snappy.h>

#include "bbts/log.h"
#include "bbts/options_parse.h"
#include "bbts/process_info.h"
#include "configure.pb.h"
#include "TorrentProvider_types.h"

using std::string;
using std::vector;
using boost::shared_ptr;

using apache::thrift::protocol::TProtocol;
using apache::thrift::protocol::TBinaryProtocol;
using apache::thrift::transport::TSocket;
using apache::thrift::transport::TTransport;
using apache::thrift::transport::TFramedTransport;
using apache::thrift::TException;

namespace bbts {

TorrentProvider::TorrentProvider() : _is_file_no_found(false) {
    set_service_name("provider");
}

TorrentProvider::~TorrentProvider() {}

bool TorrentProvider::get_torrent_file(const string &uri, vector<char> *buffer) {
    message::SourceURI source_uri;
    if (!parse_uri_entry(uri, &source_uri)) {
        WARNING_LOG("can't parse uri: %s", uri.c_str());
        return false;
    }
    string machine_room = ProcessInfo::get_machine_room(source_uri.host(), true);
    if (check_machine_room_area_default(machine_room)) {
        machine_room = ProcessInfo::get_machine_room(g_process_info->hostname(), false);
    }
    if (!load_conf(routing_conf(), machine_room)) {
        if (!load_conf(g_process_info->root_dir() + "/conf/routing.conf", machine_room)) {
            WARNING_LOG("load provider conf(%s/conf/routing.conf) failed",
                    g_process_info->root_dir().c_str());
            return false;
        }
    }

    int success = -1;
    NodeVector nodes;
    get_nodes(uri, &nodes);
    TorrentResponse response;
    for (NodeVector::const_iterator it = nodes.begin(); it != nodes.end() && success == -1; ++it) {
        shared_ptr<TSocket> socket(new TSocket(it->first, it->second));
        socket->setConnTimeout(3000);
        socket->setSendTimeout(3000);
        socket->setRecvTimeout(5000);
        shared_ptr<TFramedTransport> transport(new TFramedTransport(socket));
        shared_ptr<TBinaryProtocol> protocol(new TBinaryProtocol(transport));
        TorrentProviderServiceClient client(protocol);
        try {
            transport->open();
            NOTICE_LOG("[open provider:%s:%d][success]", it->first.c_str(), it->second);
        } catch (TException &tx) {
            WARNING_LOG("[open provider:%s:%d][fail:%s]", it->first.c_str(), it->second, tx.what());
            continue;
        }

        while (success == -1) {
            try {
                client.getTorrentZipCode(response, uri);
            } catch (TException &tx) {
                WARNING_LOG("[provider:%s:%d][fail:%s]", it->first.c_str(), it->second, tx.what());
                break;
            }
            if (response.torrentStatus == TorrentStatus::STATUS_PROCESS) {
                TRACE_LOG("get torrent provider in process: %s", response.message.c_str());
                sleep(2);
                continue;
            } else if (response.torrentStatus == TorrentStatus::STATUS_OK) {
                DEBUG_LOG("get torrent from provider success: %s", response.message.c_str());
                success = 0;
                break;
            } else {
                WARNING_LOG("get torrent provider failed: %s", response.message.c_str());
                if (response.torrentStatus == TorrentStatus::STATUS_ERROR_FILE_NOT_EXIST) {
                    _is_file_no_found = true;
                }
                success = 1;
                break;
            }
        }
        try {
            transport->close();
        } catch (TException &tx) {
            WARNING_LOG("[close provider:%s:%d][fail:%s]",
                    it->first.c_str(), it->second, tx.what());
        }
    }

    if (success != 0) {
        return false;
    }

    string data;
    if (!snappy::Uncompress(response.torrentZipCode.data(),
            response.torrentZipCode.length(), &data)) {
        WARNING_LOG("uncompress torrent file from torrent provider failed!");
        return false;
    }
    buffer->clear();
    std::copy(data.begin(), data.end(), std::back_inserter(*buffer));
    return true;
}

bool TorrentProvider::upload_torrent_file(
        const std::string &infohash,
        const std::string &source,
        const std::vector<char> &torrent_code) {
    if (infohash.empty()
        || infohash.length() != 40
        || source.empty()
        || torrent_code.size() == 0) {
        WARNING_LOG("param invalid! infohash:%s, source:%s, code_size:%d",
                infohash.c_str(), source.c_str(), torrent_code.size());
        return false;
    }

    // compress code
    string data;
    size_t number = snappy::Compress(&torrent_code[0], torrent_code.size(), &data);
    if (number != data.length()) {
        WARNING_LOG("compress torrent code error!");
        return false;
    }
    DEBUG_LOG("infohash:%s, source:%s, old_size:%d, new_size:%d",
            infohash.c_str(), source.c_str(), torrent_code.size(), data.length());
    InfohashTorrent infohash_torrent;
    infohash_torrent.__set_infohash(infohash);
    infohash_torrent.__set_source(source);
    infohash_torrent.__set_torrentZipCode(data);
    infohash_torrent.__set_torrentStatus(TorrentStatus::STATUS_OK);

    string machine_room = ProcessInfo::get_machine_room(g_process_info->hostname(), false);
    if (!load_conf(routing_conf(), machine_room)) {
        if (!load_conf(g_process_info->root_dir() + "/conf/routing.conf", machine_room)) {
            WARNING_LOG("load provider conf(%s/conf/routing.conf) failed",
                    g_process_info->root_dir().c_str());
            return false;
        }
    }

    int success = -1;
    NodeVector nodes;
    get_nodes(infohash, &nodes);
    GeneralResponse response;
    for (NodeVector::const_iterator it = nodes.begin(); it != nodes.end() && success == -1; ++it) {
        shared_ptr<TSocket> socket(new TSocket(it->first, it->second));
        socket->setConnTimeout(3000);
        socket->setSendTimeout(3000);
        socket->setRecvTimeout(5000);
        shared_ptr<TFramedTransport> transport(new TFramedTransport(socket));
        shared_ptr<TBinaryProtocol> protocol(new TBinaryProtocol(transport));
        TorrentProviderServiceClient client(protocol);
        try {
            transport->open();
            NOTICE_LOG("[open provider:%s:%d][success]", it->first.c_str(), it->second);
        } catch (TException &tx) {
            WARNING_LOG("[open provider:%s:%d][fail:%s]", it->first.c_str(), it->second, tx.what());
            continue;
        }

        while (success == -1) {
            try {
                client.uploadTorrent(response, infohash_torrent);
            } catch (TException &tx) {
                WARNING_LOG("[provider:%s:%d][fail:%s]", it->first.c_str(), it->second, tx.what());
                break;
            }

            if (response.retCode == 0) {
                DEBUG_LOG("upload torrent to provider success: %s", response.message.c_str());
                success = 0;
                break;
            } else {
                WARNING_LOG("upload torrent to provider failed: %s", response.message.c_str());
                success = 1;
                break;
            }
        }
        try {
            transport->close();
        } catch (TException &tx) {
            WARNING_LOG("[close provider:%s:%d][fail:%s]",
                    it->first.c_str(), it->second, tx.what());
        }
    }

    return success == 0 ? true : false;
}

bool TorrentProvider::get_infohash_torrent_file(
        const string &infohash,
        std::string *source,
        vector<char> *buffer) {
    if (infohash.empty()
        || infohash.length() != 40
        || source == NULL
        || buffer == NULL) {
        return false;
    }
    source->clear();

    string machine_room = ProcessInfo::get_machine_room(g_process_info->hostname(), false);
    if (!load_conf(routing_conf(), machine_room)) {
        if (!load_conf(g_process_info->root_dir() + "/conf/routing.conf", machine_room)) {
            WARNING_LOG("load provider conf(%s/conf/routing.conf) failed",
                    g_process_info->root_dir().c_str());
            return false;
        }
    }

    int success = -1;
    NodeVector nodes;
    get_nodes(infohash, &nodes);
    InfohashTorrent infohash_torrent;
    for (NodeVector::const_iterator it = nodes.begin(); it != nodes.end() && success == -1; ++it) {
        shared_ptr<TSocket> socket(new TSocket(it->first, it->second));
        socket->setConnTimeout(3000);
        socket->setSendTimeout(3000);
        socket->setRecvTimeout(5000);
        shared_ptr<TFramedTransport> transport(new TFramedTransport(socket));
        shared_ptr<TBinaryProtocol> protocol(new TBinaryProtocol(transport));
        TorrentProviderServiceClient client(protocol);
        try {
            transport->open();
            NOTICE_LOG("[open provider:%s:%d][success]", it->first.c_str(), it->second);
        } catch (TException &tx) {
            WARNING_LOG("[open provider:%s:%d][fail:%s]", it->first.c_str(), it->second, tx.what());
            continue;
        }

        while (success == -1) {
            try {
                client.getInfohashTorrent(infohash_torrent, infohash);
            } catch (TException &tx) {
                WARNING_LOG("[provider:%s:%d][fail:%s]", it->first.c_str(), it->second, tx.what());
                break;
            }
            if (infohash_torrent.torrentStatus == TorrentStatus::STATUS_PROCESS) {
                TRACE_LOG("get torrent provider in process: %s",
                        infohash_torrent.message.c_str());
                sleep(2);
                continue;
            } else if (infohash_torrent.torrentStatus == TorrentStatus::STATUS_OK) {
                DEBUG_LOG("get torrent from provider success: %s",
                        infohash_torrent.message.c_str());
                success = 0;
                break;
            } else {
                WARNING_LOG("get torrent provider failed: %s",
                        infohash_torrent.message.c_str());
                success = 1;
                break;
            }
        }
        try {
            transport->close();
        } catch (TException &tx) {
            WARNING_LOG("[close provider:%s:%d][fail:%s]",
                    it->first.c_str(), it->second, tx.what());
        }
    }

    if (success != 0) {
        return false;
    }

    string data;
    if (!snappy::Uncompress(infohash_torrent.torrentZipCode.data(),
            infohash_torrent.torrentZipCode.length(), &data)) {
        WARNING_LOG("uncompress torrent file from torrent provider failed!");
        return false;
    }
    buffer->clear();
    std::copy(data.begin(), data.end(), std::back_inserter(*buffer));
    source->assign(infohash_torrent.source);
    return true;
}

} // namespace bbts
