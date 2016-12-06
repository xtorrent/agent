/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   bbts_client.cpp
 *
 * @author liuming03
 * @date   2014-1-17
 * @brief 
 */

#include "bbts/tool/bbts_client.h"

#include <vector>

#include <boost/array.hpp>

#include "bbts/encode.h"
#include "bbts/config.h"
#include "bbts/log.h"
#include "bbts/path.h"
#include "bbts/unix_socket_client.h"

using std::string;
using std::vector;
using boost::scoped_ptr;
using boost::shared_ptr;
using boost::array;
using boost::system::error_code;
using google::protobuf::Message;

namespace bbts {
namespace tool {

UnixSocketConnection::EndPoint BBTSClient::_s_default_endpoint;

BBTSClient::BBTSClient() :
        _endpoint(_s_default_endpoint),
        _send_ucred(true),
        _max_retry_times(1) {}

BBTSClient::BBTSClient(const UnixSocketConnection::EndPoint &endpoint) :
        _endpoint(endpoint),
        _send_ucred(true),
        _max_retry_times(1) {}

BBTSClient::~BBTSClient() {}

int BBTSClient::create_task(const message::AddTask &params, int64_t *id) {
    const message::Task &task_info = params.task();
    if (task_info.has_infohash() && task_info.infohash().length() != 40) {
        WARNING_LOG("invalid infohash[sha-1 hex]: %s", task_info.infohash().c_str());
        return 1;
    }

    if (task_info.seeding_time() < -1) {
        WARNING_LOG("seeding time[>=-1] invalid: %d", task_info.seeding_time());
        return 1;
    }

    if (task_info.save_path().empty()) {
        WARNING_LOG("save path invalid: %s", task_info.save_path().c_str());
        return 1;
    }

    if (task_info.data().empty() && task_info.infohash().empty()
            && task_info.uri().empty()) {
        WARNING_LOG("not spec torrent or infohash or uri or torrent data");
        return 1;
    }

    const message::TaskOptions &options = params.options();
    if (options.upload_limit() < 0 || options.upload_limit() > 500 * 1024 * 1024) {
        WARNING_LOG("upload limit[0-500MB/s] invalid: %d", options.upload_limit());
        return 1;
    }

    if (options.max_connections() < 0 || options.max_connections() > 65535) {
        WARNING_LOG("connection limit[0-65535] invalid: %d", options.max_connections());
        exit(1);
    }

    if (task_info.type() == message::SEEDING_TASK) {
        set_max_retry_times(5);
    }

    uint32_t type = REQ_ADD_TASK;
    scoped_ptr<google::protobuf::Message> res;
    if (!tell_server_with_retry(&params, &type, &res)) {
        WARNING_LOG("Tell agent failed.");
        return 4;
    }

    if (type == RES_BASE) {
        message::BaseRes *base_res = static_cast<message::BaseRes *>(res.get());
        WARNING_LOG("Add new task failed, ret(%d): %s.",
                base_res->ret_code(), base_res->fail_msg().c_str());
        return 5;
    } else {
        *id = static_cast<message::TaskRes *>(res.get())->taskid();
    }
    return 0;
}

int BBTSClient::list_tasks(const vector<int64_t> &taskids, message::BatchListRes *list_res) {
    assert(list_res);
    message::BatchCtrl req;
    if (taskids.empty()) {
        req.set_ctrl_all(true);
    } else {
        req.set_ctrl_all(false);
        for (vector<int64_t>::const_iterator it = taskids.begin(); it != taskids.end(); ++it) {
            int64_t taskid = *it;
            if (taskid < 0) {
                WARNING_LOG("task id[>=0] invalid: %ld", taskid);
                return 1;
            }
            req.add_taskids(taskid);
        }
    }
    req.set_ctrl_type(message::BatchCtrl::LIST);
    uint32_t type = REQ_BATCH_CTRL;
    scoped_ptr<google::protobuf::Message> res;
    if (!tell_server_with_retry(&req, &type, &res)) {
        WARNING_LOG("tell agent failed.");
        return 1;
    }

    if (type == RES_BASE) {
        message::BaseRes *base_res = static_cast<message::BaseRes *>(res.get());
        WARNING_LOG("list failed, ret(%d): %s", base_res->ret_code(), base_res->fail_msg().c_str());
        return 2;
    } else {
        assert(type == RES_BATCH_LIST);
        list_res->CopyFrom(*(static_cast<message::BatchListRes *>(res.get())));
    }
    return 0;
}

int BBTSClient::get_status(message::Status *status) {
    assert(status);
    uint32_t type = REQ_STATUS;
    scoped_ptr<google::protobuf::Message> res;
    if (!tell_server_with_retry(NULL, &type, &res)) {
        WARNING_LOG("Tell agent failed.");
        return 2;
    }

    if (type == RES_BASE) {
        message::BaseRes *base_res = static_cast<message::BaseRes *>(res.get());
        WARNING_LOG("get status failed, ret(%d): %s",
                base_res->ret_code(), base_res->fail_msg().c_str());
        return 3;
    } else {
        assert(type == RES_STATUS);
        status->CopyFrom(*(static_cast<message::Status *>(res.get())));
    }
    return 0;
}

int BBTSClient::batch_control(
        const vector<int64_t> &taskids,
        const vector<string> &infohashs,
        message::BatchCtrl::ctrl_t ctrl_type,
        message::BatchCtrlRes *ctrl_res) {
    assert(ctrl_res);

    message::BatchCtrl req;
    if (taskids.empty() && infohashs.empty()) {
        req.set_ctrl_all(true);
    } else {
        req.set_ctrl_all(false);
        for (vector<int64_t>::const_iterator it = taskids.begin(); it != taskids.end(); ++it) {
            int64_t taskid = *it;
            if (taskid < 0) {
                WARNING_LOG("task id[>=0] invalid: %ld", taskid);
                return 1;
            }
            req.add_taskids(taskid);
        }
        
        for (vector<string>::const_iterator it = infohashs.begin(); it != infohashs.end(); ++it) {
            string bytes;
            const string &infohash = *it;
            if (infohash.length() != 40 || !hex_decode(infohash, &bytes)) {
                WARNING_LOG("infohash %s invalid, please check!", infohash.c_str());
                return 1;
            }
            req.add_infohashs(infohash);
        }
    } 
        
    req.set_ctrl_type(ctrl_type);
    uint32_t type = REQ_BATCH_CTRL;
    scoped_ptr<google::protobuf::Message> res;
    if (!tell_server_with_retry(&req, &type, &res)) {
        WARNING_LOG("tell agent failed.");
        return 1;
    }

    if (type == RES_BASE) {
        message::BaseRes *base_res = static_cast<message::BaseRes *>(res.get());
        WARNING_LOG("batch ctrl failed, ret(%d): %s",
                base_res->ret_code(), base_res->fail_msg().c_str());
        return 2;
    } else {
        assert(type == RES_BATCH_CTRL);
        ctrl_res->CopyFrom(*(static_cast<message::BatchCtrlRes *>(res.get())));
    }
    return 0;
}

int BBTSClient::set_agent_options(const message::AgentOptions &options) {
    if (options.upload_limit() < 0 || options.upload_limit() > 500 * 1024 * 1024) {
        WARNING_LOG("upload limit[0-500MB/s] invalid: %d", options.upload_limit());
        return 1;
    }

    if (options.max_connections() < 0 || options.max_connections() > 65535) {
        WARNING_LOG("connection limit[0-65535] invalid: %d", options.max_connections());
        return 1;
    }

    uint32_t type = REQ_AGENT_SETOPT;
    scoped_ptr<google::protobuf::Message> res;
    if (!tell_server_with_retry(&options, &type, &res)) {
        WARNING_LOG("Tell agent failed.");
        return 1;
    }

    assert(type == RES_BASE);
    message::BaseRes *base_res = static_cast<message::BaseRes *>(res.get());
    if (base_res->ret_code() != 0) {
        WARNING_LOG("agent setopt failed, ret(%d): %s",
                base_res->ret_code(), base_res->fail_msg().c_str());
        return 3;
    }
    return 0;
}

int BBTSClient::get_agent_options(message::AgentOptions *options) {
    assert(options);
    uint32_t type = REQ_AGENT_GETOPT;
    scoped_ptr<google::protobuf::Message> res;
    if (!tell_server_with_retry(options, &type, &res)) {
        WARNING_LOG("Tell agent failed.");
        return 2;
    }

    if (type == RES_BASE) {
        message::BaseRes *base_res = static_cast<message::BaseRes *>(res.get());
        WARNING_LOG("agent setopt failed, ret(%d): %s",
                base_res->ret_code(), base_res->fail_msg().c_str());
        return 3;
    } else {
        assert(type == RES_AGENT_GETOPT);
        options->CopyFrom(*(static_cast<message::TaskOptions *>(res.get())));
    }
    return 0;
}

int BBTSClient::add_metadata(message::Metadata *metadata) {
    uint32_t type = REQ_ADD_METADATA;
    scoped_ptr<google::protobuf::Message> res;
    if (!tell_server_with_retry(metadata, &type, &res)) {
        TRACE_LOG("send metadata to agent failed.");
        return 1;
    }
    return 0;
}

int BBTSClient::set_task_options(const message::TaskOptions &options) {
    if (!options.has_taskid()) {
        WARNING_LOG("not spec taskid");
        return 1;
    }

    if (options.upload_limit() < 0 || options.upload_limit() > 500 * 1024 * 1024) {
        WARNING_LOG("upload limit[0-500MB/s] invalid: %d", options.upload_limit());
        return 1;
    }

    if (options.max_connections() < 0 || options.max_connections() > 65535) {
        WARNING_LOG("connection limit[0-65535] invalid: %d", options.max_connections());
        return 1;
    }

    uint32_t type = REQ_TASK_SETOPT;
    scoped_ptr<Message> response;
    if (!tell_server_with_retry(&options, &type, &response)) {
        WARNING_LOG("Tell unix server failed.");
        return 2;
    }

    assert(type == RES_BASE);
    message::BaseRes *base_res = static_cast<message::BaseRes *>(response.get());
    if (base_res->ret_code() != 0) {
        WARNING_LOG("task(%d) setopt failed, ret(%d): %s", options.taskid(),
                base_res->ret_code(), base_res->fail_msg().c_str());
        return 3;
    }
    return 0;
}

int BBTSClient::get_task_options(message::TaskOptions *options) {
    assert(options);
    if (!options->has_taskid()) {
        WARNING_LOG("not spec taskid");
        return 1;
    }

    uint32_t type = REQ_TASK_GETOPT;
    scoped_ptr<google::protobuf::Message> response;
    if (!tell_server_with_retry(options, &type, &response)) {
        WARNING_LOG("Tell unix server failed.");
        return 2;
    }

    if (type == RES_BASE) {
        message::BaseRes *base_res = static_cast<message::BaseRes *>(response.get());
        WARNING_LOG("task(%d) setopt failed, ret(%d): %s",
                options->taskid(), base_res->ret_code(), base_res->fail_msg().c_str());
        return 3;
    } else {
        assert(type == RES_TASK_GETOPT);
        options->CopyFrom(*(static_cast<message::TaskOptions *>(response.get())));
    }
    return 0;
}

bool BBTSClient::tell_server(
        const Message *const request, uint32_t *type, scoped_ptr<Message> *response) {
    boost::asio::io_service ios;
    SyncUnixSocketClient client(ios);
    if (!client.connect(_endpoint)) {
        WARNING_LOG("connect to server(%s) failed.", _endpoint.path().c_str());
        return false;
    }

    if (_send_ucred) {
        if (!send_cred(client.get_socket().native_handle())) {
            WARNING_LOG("can't send ucred to agent.");
            return false;
        }
    }

    shared_ptr<vector<char> > data;
    if (request) {
        data.reset(new vector<char>(request->ByteSize() + sizeof(*type)));
        if (!request->SerializeToArray(&(*data)[0 + sizeof(*type)], request->ByteSize())) {
            WARNING_LOG("result msg serialize to array failed.");
            return false;
        }
    } else {
        data.reset(new vector<char>(sizeof(*type)));
    }
    char* ptr = &(*data)[0];
    *reinterpret_cast<uint32_t *>(ptr) = *type;
    if (!client.write_data(boost::const_pointer_cast<const vector<char> >(data))) {
        WARNING_LOG("sycnc write message failed");
        return false;
    }
    data.reset();
    if (!client.read_data(&data)) {
        WARNING_LOG("read message failed, : %s.");
        return false;
    }
    client.close();

    ptr = &(*data)[0];
    *type = *reinterpret_cast<const uint32_t *>(ptr);
    switch (*type) {
    case RES_BASE:
        response->reset(new message::BaseRes());
        break;

    case RES_TASK:
        response->reset(new message::TaskRes);
        break;

    case RES_TASK_STATUS:
        response->reset(new message::TaskStatus);
        break;

    case RES_BATCH_CTRL:
        response->reset(new message::BatchCtrlRes);
        break;

    case RES_BATCH_LIST:
        response->reset(new message::BatchListRes);
        break;

    case RES_TASK_GETOPT:
        response->reset(new message::TaskOptions);
        break;

    case RES_AGENT_GETOPT:
        response->reset(new message::AgentOptions);
        break;

    case RES_STATUS:
        response->reset(new message::Status);
        break;

    default:
        WARNING_LOG("not support this response type");
        return false;
    }

    if (!(*response)->ParseFromArray(ptr + sizeof(*type), data->size() - sizeof(*type))) {
        WARNING_LOG("parse message failed.");
        return false;
    }
    return true;
}

bool BBTSClient::tell_server_with_retry(
        const Message *const request, uint32_t *type, scoped_ptr<Message> *response) {
    for (int i = 0; i < _max_retry_times;) {
        sleep(i * i);
        if (tell_server(request, type, response)) {
            return true;
        }
        WARNING_LOG("tell unix server failed, will retry %d.", ++i);
    }
    return false;
}

}  // namespace tool
}  // namespace bbts
