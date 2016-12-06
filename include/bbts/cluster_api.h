/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   cluster_api.h
 *
 * @author liuming03
 * @date   2013-05-30
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_CLUSTER_API_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_CLUSTER_API_H

#include <map>
#include <string>
#include <vector>

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>

#include "configure.pb.h"

namespace bbts {

class BTInfoInterface;

class ClusterAPI : public boost::noncopyable {
public:
    typedef void* (*ConnectClusterFunc)(
            const std::string &host,
            const int port,
            const std::string &user,
            const std::string &passwd,
            const std::string &prefix_path);

    typedef int (*CloseClusterFunc)(void*);

    typedef int (*ReadPieceContentFunc)(
            const void* fs,
            const BTInfoInterface* info,
            int piece_index,
            char* data,
            std::string &errstr);

    typedef int (*ReadFileFunc)(void *fs, const std::string &filename, std::vector<char> *buffer);

    typedef std::map<std::string, boost::shared_ptr<ClusterAPI> > ClusterAPIMap;

    typedef std::map<std::string, boost::shared_ptr<void> > ConnectionMap;

    bool support_cluster() {
        return _support_cluster;
    }

    void* connect_cluster_with_timeout(const message::SourceURI &source, int timeout);

    static ClusterAPI* get_cluster_api(const std::string &library);

    ~ClusterAPI();

    ReadPieceContentFunc read_piece_content;
    ReadFileFunc read_file;

private:
    ClusterAPI();

    bool parse_library(const std::string &library);
    bool set_classpath_env();

    void connect_thread_func(const message::SourceURI &source, boost::mutex *mut,
            boost::condition_variable *cond, bool *complete, void** fs);

    void close_cluster(void *fs);

    std::string _library_name;
    void* _cluster_library;
    bool _support_cluster;
    ConnectionMap _connection_map;

    ConnectClusterFunc _connect_cluster;
    CloseClusterFunc _close_cluster;

    static ClusterAPIMap _s_api_map;
};

}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_CLUSTER_API_H
