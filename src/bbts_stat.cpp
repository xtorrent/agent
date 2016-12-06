/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   bbts_stat.cpp
 *
 * @author yanghanlin 
 * @date   2015-11-26
 * @brief 
 */
#include "bbts/bbts_stat.h"

#include <stdio.h>

#include <boost/shared_ptr.hpp>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/Thrift.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TSocket.h>

#include "bbts/log.h"
#include "bbts/process_info.h"

using ::apache::thrift::protocol::TProtocol;
using ::apache::thrift::protocol::TBinaryProtocol;
using ::apache::thrift::transport::TFramedTransport;
using ::apache::thrift::transport::TSocket;
using ::apache::thrift::transport::TTransport;
using ::apache::thrift::TException;

namespace bbts {

BbtsStat::BbtsStat() : _peer_file(NULL) {
    set_service_name("bbts_stat");
}

BbtsStat::~BbtsStat() {
    if (_peer_file) {
        fclose(_peer_file);
    }
}
   
void BbtsStat::add_to_peer_vector(const PeerStatAlert& alert) {
    if (alert.uploaded() == 0 && alert.downloaded() == 0) {
        return;
    }
    stat::PeerInfo peer_info;

    peer_info.__set_infohash(alert.infohash());
    peer_info.__set_local_host(alert.local_host());
    peer_info.__set_remote_host(alert.remote_host());
    peer_info.__set_start_time(alert.start_time());
    peer_info.__set_end_time(alert.end_time());
    peer_info.__set_uploaded(alert.uploaded());
    peer_info.__set_downloaded(alert.downloaded());
    peer_info.__set_is_cluster(alert.is_cluster());

    _peer_info_vector.push_back(peer_info);
    
    print_peer_statistics(alert);
}

stat::TaskInfo& BbtsStat::get_task_info() {
    return _task_info;
}

void BbtsStat::send_stat() {
    std::string machine_room = g_process_info->machine_room();
    if (!load_conf(routing_conf(), machine_room)) {
        if (!load_conf(g_process_info->root_dir() + "/conf/routing.conf", machine_room)) {
            WARNING_LOG("load stat conf(%s/conf/routing.conf) failed",
                    g_process_info->root_dir().c_str());
            return;
        }
    }
    int max_retry_times = 1;
    bool success = false;
    NodeVector nodes;
    get_nodes(_task_info.infohash, &nodes);
    for (NodeVector::const_iterator it = nodes.begin(); it != nodes.end() && !success; ++it) {
        boost::shared_ptr<TSocket> socket(new TSocket(it->first, it->second));
        socket->setConnTimeout(3000);
        socket->setSendTimeout(3000);
        socket->setRecvTimeout(5000);
        boost::shared_ptr<TTransport> transport(new TFramedTransport(socket));
        boost::shared_ptr<TBinaryProtocol> protocol(new TBinaryProtocol(transport));

        stat::StatAnnounceClient client(protocol);

        int retry = 0;
        while (retry <= max_retry_times) {
            try {
                transport->open();
                DEBUG_LOG("[open stat server: %s:%d][success]", it->first.c_str(), it->second);

                if (client.report_stat(_peer_info_vector, _task_info)) {
                    DEBUG_LOG("[stat server: %s:%d] report success!",
                            it->first.c_str(), it->second);
                    DEBUG_LOG("infohash is %s", _task_info.infohash.c_str());
                    DEBUG_LOG("download is %d", _task_info.payload_downloaded);
                    DEBUG_LOG("upload is %d", _task_info.payload_uploaded);
                    success = true;
                    transport->close();
                    break;
                }
                transport->close();
                ++retry;
                DEBUG_LOG("[stat server: %s:%d] report fail %d times!", 
                        it->first.c_str(), it->second, retry);

            } catch (TException &e) {
                transport->close();
                ++retry;
                DEBUG_LOG("[send to stat server: %s:%d][fail %d times: %s]", 
                        it->first.c_str(), it->second, retry, e.what());
                sleep(1);
            }
        }  // while
    } // for
}

void BbtsStat::print_task_statistics(const std::string &filename) {
    FILE *fp = fopen(filename.c_str(), "a");
    if (!fp) {
        DEBUG_LOG("open task stat log failed!");
        return; 
    }
    fprintf(fp,
            "%s,%s,%d,%s,%ld,%d,%d,%d,%d,%d,%d,%d,%ld,%ld,%d,%ld,%ld,%d,%d,%d,%d,%d,%d,%ld,%s,%s\n",
            _task_info.host_name.c_str(),
            _task_info.ip.c_str(),
            _task_info.port,
            _task_info.infohash.c_str(),
            _task_info.total_size,
            _task_info.piece_length,
            _task_info.num_pieces,
            _task_info.num_files,
            _task_info.num_paths,
            _task_info.num_symlinks,
            _task_info.download_limit,
            _task_info.upload_limit,
            _task_info.payload_downloaded,
            _task_info.payload_uploaded,
            _task_info.progress_ppm,
            _task_info.start_time,
            _task_info.end_time,
            _task_info.retval,
            _task_info.time_for_download_metadata,
            _task_info.time_for_check_files,
            _task_info.time_for_downloaded,
            _task_info.time_for_seeding,
            _task_info.is_hdfs_download ? 1 : 0,
            _task_info.downloaded_from_hdfs,
            _task_info.hdfs_address.c_str(),
            _task_info.product_tag.c_str());

    fclose(fp);
}

bool BbtsStat::open_peer_file(const std::string &filename) {
    _peer_file = fopen(filename.c_str(), "a");
    if (!_peer_file) {
        return false;
    }
    return true;
}

void BbtsStat::print_peer_statistics(const PeerStatAlert &alert) {
    if (_peer_file && (alert.uploaded() != 0 || alert.downloaded() != 0)) {
        fprintf(_peer_file, "%s\n", alert.message().c_str());
        fflush(_peer_file);
    }
}

} // namespace bbts
