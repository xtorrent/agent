/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   control_server.cpp
 *
 * @author liuming03
 * @date   2015-1-19
 * @brief 
 */
#include "bbts/tool/control_server.h"

#include <boost/system/error_code.hpp>

#include "bbts/config.h"
#include "bbts/error_category.h"
#include "bbts/log.h"
#include "message.pb.h"

using std::vector;

using boost::shared_ptr;
using boost::system::error_code;
using libtorrent::torrent_status;

namespace bbts {
namespace tool {

ControlServer::ControlServer(message::DownloadConfigure *configure, boost::asio::io_service &ios) :
        _configure(configure),
        _server(ios) {
    _server.set_endpoint(UnixSocketConnection::EndPoint(_configure->control_path()));
    _server.set_heartbeat_recv_cycle(60);
    _server.set_read_callback(boost::bind(&ControlServer::control_handler, this, _1, _2));
}

ControlServer::~ControlServer() {
    stop();
}

bool ControlServer::start(const libtorrent::torrent_handle &torrent) {
    _torrent = torrent;
    if (_configure->control_path().empty()) {
        return true;
    }
    return _server.serve(0700);
}

void ControlServer::stop() {
    _server.close();
}

static void write_base_message(const shared_ptr<UnixSocketConnection> &connection,
        const error_code &ec) {
    bbts::message::BaseRes response;
    response.set_fail_msg(ec.message());
    response.set_ret_code(ec.value());
    write_message(connection, RES_BASE, response);
}

static message::TaskStatus::status_t get_status_code(const torrent_status &ts) {
    if (!ts.error.empty()) {
        return message::TaskStatus_status_t_ERROR;
    }

    if (ts.paused) {
        return message::TaskStatus_status_t_PAUSED;
    } else if (ts.is_finished) {
        return message::TaskStatus_status_t_SEEDING;
    } else {
        switch (ts.state) {
        case torrent_status::downloading_metadata:
            return message::TaskStatus_status_t_DTORRENT;
        case torrent_status::checking_resume_data:
            case torrent_status::queued_for_checking:
            return message::TaskStatus_status_t_CHECKQ;
        case torrent_status::checking_files:
            return message::TaskStatus_status_t_CHECKING;
        case torrent_status::downloading:
            default:
            return message::TaskStatus_status_t_DOWNLOAD;
        }
    }
}

void ControlServer::list_task(const shared_ptr<UnixSocketConnection> &connection) {
    message::BatchListRes response;
    message::TaskStatus *task_status = response.add_status();
    message::Task *task_info = task_status->mutable_task();
    task_info->set_taskid(-1);
    task_info->set_cmd(_configure->cmd());
    task_info->set_uid(getuid());
    task_info->set_gid(getgid());
    task_info->set_infohash(_configure->infohash());
    task_info->set_save_path(_configure->save_path());
    task_info->set_new_name(_configure->new_name());
    torrent_status ts = _torrent.status(0);
    task_status->set_status(get_status_code(ts));
    task_status->set_error(ts.error);
    task_status->set_progress(ts.progress_ppm);
    task_status->set_num_peers(ts.num_peers);
    task_status->set_num_seeds(ts.num_seeds);
    task_status->set_upload_rate(ts.upload_rate);
    task_status->set_download_rate(ts.download_rate);
    task_status->set_total_upload(ts.total_payload_upload);
    task_status->set_total_download(ts.total_payload_download);
    write_message(connection, RES_BATCH_LIST, response);
}

#define PARSE_MSG(msg) \
do {\
    if (!message.ParseFromArray(ptr + sizeof(type), data->size() - sizeof(type))) {\
      WARNING_LOG("parse message " msg " failed, type(%u)", type);\
      ec.assign(bbts::errors::PROTOBUF_PARSE_ERROR, get_error_category());\
      break;\
    }\
} while (0)

void ControlServer::control_handler(const shared_ptr<UnixSocketConnection> &connection,
        const shared_ptr<const vector<char> > &data) {
    const char *ptr = &(*data)[0];
    uint32_t type = *reinterpret_cast<const uint32_t *>(ptr);
    DEBUG_LOG("control handler, type: %u", type);
    error_code ec;
    switch (type) {
    case REQ_TASK_SETOPT: {
        message::TaskOptions message;
        PARSE_MSG("REQ_TASK_GETOPT");
        if (ec) {
            break;
        }
        if (message.has_download_limit()) {
            _configure->set_download_limit(message.download_limit());
            _torrent.set_download_limit(_configure->download_limit());
        }
        if (message.has_upload_limit()) {
            _configure->set_upload_limit(message.upload_limit());
            _torrent.set_upload_limit(_configure->upload_limit());
        }
        if (message.has_max_connections()) {
            _configure->set_connection_limit(message.max_connections());
            _torrent.set_max_connections(_configure->connection_limit());
        }
        write_base_message(connection, ec);
        break;
    }

    case REQ_TASK_GETOPT: {
        message::TaskOptions message;
        PARSE_MSG("REQ_TASK_GETOPT");
        if (ec) {
            break;
        }
        if (message.has_download_limit()) {
            message.set_download_limit(_configure->download_limit());
        }
        if (message.has_upload_limit()) {
            message.set_upload_limit(_configure->upload_limit());
        }
        if (message.has_max_connections()) {
            message.set_max_connections(_configure->connection_limit());
        }
        write_message(connection, RES_TASK_GETOPT, message);
        break;
    }

    case REQ_BATCH_CTRL: {
        message::BatchCtrl message;
        PARSE_MSG("REQ_BATCH_CTRL");
        if (message.ctrl_type() == message.LIST) {
            list_task(connection);
        } else {
            WARNING_LOG("ctrl type type (%u) not support.", message.ctrl_type());
            ec.assign(bbts::errors::MSG_ARGS_ERROR, get_error_category());
        }
        break;
    }

    default:
        WARNING_LOG("message type (%u) not support.", type);
        ec.assign(bbts::errors::MSG_ARGS_ERROR, get_error_category());
        break;
    }

    if (ec) {
        write_base_message(connection, ec);
    }
}

} // namespace tool
} // namespace bbts
