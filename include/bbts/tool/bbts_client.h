/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   bbts_clinet.h
 *
 * @author liuming03
 * @date   2014-1-17
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_BBTS_CLIENT_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_BBTS_CLIENT_H

#include <string>

#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

#include "bbts/unix_socket_connection.h"
#include "message.pb.h"

namespace bbts {
namespace tool {

/**
 * @brief
 */
class BBTSClient : public boost::noncopyable {
public:
    BBTSClient();

    BBTSClient(const UnixSocketConnection::EndPoint &endpoint);

    ~BBTSClient();

    int create_task(const message::AddTask &params, int64_t *id);

    int set_task_options(const message::TaskOptions &options);

    int get_task_options(message::TaskOptions *opt);

    int set_agent_options(const message::AgentOptions &options);

    int get_agent_options(message::AgentOptions *options);

    int list_tasks(const std::vector<int64_t> &taskids, message::BatchListRes *res);

    int get_status(message::Status *status);

    int batch_control(
            const std::vector<int64_t> &taskids,
            const std::vector<std::string> &infohashs,
            message::BatchCtrl::ctrl_t,
            message::BatchCtrlRes *res);

    int add_metadata(message::Metadata *metadata);

    void set_endpoint(const UnixSocketConnection::EndPoint &endpoint) {
        _endpoint = endpoint;
    }

    void no_send_cred() {
        _send_ucred = false;
    }

    void set_max_retry_times(int max_retry_times) {
        _max_retry_times = max_retry_times;
    }

    static void set_default_endpoint(const UnixSocketConnection::EndPoint &default_endpoint) {
        _s_default_endpoint = default_endpoint;
    }

private:
    bool tell_server(
            const google::protobuf::Message *const request,
            uint32_t *type,
            boost::scoped_ptr<google::protobuf::Message> *response);

    bool tell_server_with_retry(
            const google::protobuf::Message *const request,
            uint32_t *type,
            boost::scoped_ptr<google::protobuf::Message> *response);

    UnixSocketConnection::EndPoint _endpoint;
    bool _send_ucred;
    int _max_retry_times;

    static UnixSocketConnection::EndPoint _s_default_endpoint;
};

}  // namespace tool
}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_BBTS_CLIENT_H
