/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   task_manager.cpp
 *
 * @author liuming03
 * @date   2013-1-14
 */

#include "bbts/agent/task_manager.h"

#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/escape_string.hpp>
#include <libtorrent/extensions/metadata_transfer.hpp>
#include <libtorrent/session.hpp>

#include "bbts/agent/task.h"
#include "bbts/agent/task_db.h"
#include "bbts/torrent_plugin.h"
#include "bbts/config.h"
#include "bbts/error_category.h"
#include "bbts/file.h"
#include "bbts/lazy_singleton.hpp"
#include "bbts/log.h"
#include "bbts/path.h"
#include "bbts/process_info.h"
#include "bbts/string_util.h"
#include "bbts/encode.h"

using std::deque;
using std::make_pair;
using std::string;
using std::stringstream;
using std::vector;

using boost::any_cast;
using boost::asio::io_service;
using boost::bind;
using boost::intrusive_ptr;
using boost::scoped_ptr;
using boost::shared_ptr;
using boost::weak_ptr;
using boost::scoped_array;
using boost::system::error_code;
using boost::asio::local::stream_protocol;
using libtorrent::session;
using libtorrent::session_settings;
using libtorrent::torrent_handle;
using libtorrent::alert;
using libtorrent::torrent_info;
using libtorrent::alert;

namespace bbts {
namespace agent {

typedef boost::mutex::scoped_lock scoped_lock;

TaskManager *g_task_manager = LazySingleton<TaskManager>::instance();
message::AgentConfigure *g_agent_configure = LazySingleton<message::AgentConfigure>::instance();

#define ADD_TASK_TO_MAP(task) \
do { \
    _tasks_map[(task)->get_id()] = task; \
    _infohash_map[(task)->get_infohash()] = task; \
    _data_path_map[(task)->get_data_path()] = task; \
    _torrent_map[(task)->get_torrent()] = task; \
} while (0)

#define ERASE_TASK_FROM_MAP(task) \
do { \
    _torrent_map.erase((task)->get_torrent()); \
    _infohash_map.erase((task)->get_infohash()); \
    _data_path_map.erase((task)->get_data_path()); \
    _tasks_map.erase((task)->get_id()); \
} while (0)

static void write_base_message(
        const shared_ptr<UnixSocketConnection> &connection,
        const error_code &ec) {
    bbts::message::BaseRes response;
    response.set_fail_msg(ec.message());
    response.set_ret_code(ec.value());
    write_message(connection, RES_BASE, response);
}

static int get_agent_config_cb(void *arg, int argc, char **argv, char **col_name) {
    message::AgentOptions *options = static_cast<message::AgentOptions *>(arg);
    options->set_upload_limit(atoi(argv[1]));
    options->set_max_connections(atoi(argv[2]));
    return 0;
}

TaskManager::TaskManager() :
        _worker_pool("BBTS AGENT WORKER THREAD"),
        _thrift_tracker_pool("THRIFT TRACKER THREAD"),
        _thrift_tracker(_thrift_tracker_pool.get_io_service()),
        _current_metas_total_size(0) {
    _control_server.set_heartbeat_recv_cycle(120);
    _control_server.set_accept_callback(bind(&TaskManager::on_accept, this, _1));
    _control_server.set_read_callback(bind(&TaskManager::on_read_message, this, _1, _2));
}

TaskManager::~TaskManager() {}

shared_ptr<TaskManager::Metadata> TaskManager::get_metadata_by_taskinfo(
        const message::Task &taskinfo) {
    shared_ptr<Metadata> meta;
    scoped_lock lock(_metas_lock);

    // 找到meta信息就删掉了，防止存太多占地儿，所以只能通过infohash 和uri地址add一次
    for (MetadataList::iterator it = _metas.begin(); it != _metas.end(); ++it) {
        if ((*it)->infohash == taskinfo.infohash()
                || (!taskinfo.uri().empty() && (*it)->uri == taskinfo.uri())) {
            meta = *it;
            _current_metas_total_size -= meta->metadata.length();
            _metas.erase(it);
            break;
        }
    }
    return meta;
}

void TaskManager::persist_tasks() {
    shared_ptr<Task> task;
    scoped_lock lock(_tasks_lock);
    TasksMap::iterator it;
    for (it = _tasks_map.begin(); it != _tasks_map.end();) {
        task = it->second;
        ++it;
        ERASE_TASK_FROM_MAP(task);
        task->persist();
    }
}

void TaskManager::add_resume_task() {
    string path = g_agent_configure->resume_dir();
    DIR* dir = opendir(const_cast<char *>(path.c_str()));
    if (NULL == dir) {
        WARNING_LOG("can not open dir: %s", path.c_str());
        return;
    }

    NOTICE_LOG("will add resume task...");
    struct dirent entry, *entptr;
    scoped_lock lock(_tasks_lock);
    for (readdir_r(dir, &entry, &entptr); entptr != NULL; readdir_r(dir, &entry, &entptr)) {
        if ((strcmp(entptr->d_name, ".") == 0) || (strcmp(entptr->d_name, "..") == 0)) {
            continue;
        }
        string file_name = path + "/" + entptr->d_name;
        struct stat statbuf;
        if (0 != ::stat(const_cast<char *>(file_name.c_str()), &statbuf)) {
            WARNING_LOG("stat failed: %s", file_name.c_str());
            continue;
        }
        if (!(statbuf.st_mode & S_IFREG)) {
            continue;
        }

        shared_ptr<Task> task = Task::create_from_resume_file(file_name, statbuf.st_size);
        if (!task) {
            unlink(file_name.c_str());
            continue;
        }
        ADD_TASK_TO_MAP(task);
    }
    closedir(dir);
}

bool TaskManager::get_agent_options_from_db(message::AgentOptions *options) {
    assert(options);
    options->set_upload_limit(-1);

    bool if_has_agent_config = false;
    string sql = "select bind_port, upload_limit, connection_limit from agent_config limit 1";
    for (int i = 0; i < 3; ++i) {
        if (!_task_db->excute(sql, &get_agent_config_cb, options)) {
            _task_db->reconnect();
            WARNING_LOG("execute sql(%s) failed:%d.", sql.c_str(), i);
            continue;
        }
        if_has_agent_config = true;
        break;
    }

    if (options->upload_limit() == -1) {
        // this means empty table or some error happens
        if_has_agent_config = false;
        stringstream s;
        s << "insert into agent_config(bind_port, upload_limit, connection_limit) values("
                << g_agent_configure->listen_port() << ","
                << g_agent_configure->upload_limit() / 1024 / 1024  << ","
                << g_agent_configure->connection_limit() << ")";
        for (int i = 0; i < 3; ++i) {
            if (!_task_db->excute(s.str())) {
                _task_db->reconnect();
                WARNING_LOG("execute sql(%s) failed:%d", s.str().c_str(), i);
                continue;
            }
            DEBUG_LOG("insert one record into agent_config:%s", s.str().c_str());
            break;
        }
    }

    return if_has_agent_config;
}

bool TaskManager::start() {
    if (!_peer_stat.open(g_agent_configure->peer_stat_file())) {
        WARNING_LOG("Open peer stat file %s failed", g_agent_configure->peer_stat_file().c_str());
    }

    const string &machine_room = g_process_info->machine_room();
    if (!_thrift_tracker.load_conf(g_agent_configure->routing_conf_file(), machine_room)) {
        if (!_thrift_tracker.load_conf(
                g_process_info->root_dir() + "/conf/routing.conf", machine_room)) {
            FATAL_LOG("load thrift trackers fail\n");
            return false;
        }
    }

    _control_server.set_endpoint(UnixSocketConnection::EndPoint(g_agent_configure->socket_file()));
    if (!_control_server.start(0777)) {
        FATAL_LOG("control server start failed.");
        return false;
    }

    _task_db.reset(new TaskDB(g_agent_configure->database_file(),
            _worker_pool.get_io_service(), g_agent_configure->db_del_interval()));
    _session.reset(new session(libtorrent::fingerprint("GK", 3, 0, 0, 0),
            make_pair(g_agent_configure->listen_port(), g_agent_configure->listen_port()),
            "0.0.0.0", 0,
            alert::error_notification | alert::peer_notification | alert::tracker_notification |
            alert::storage_notification | alert::status_notification | alert::performance_warning |
            alert::ip_block_notification | alert::debug_notification));

    error_code ec;
    int listen_port = g_agent_configure->listen_port();
    _session->listen_on(make_pair(listen_port, listen_port),
            ec, "0.0.0.0", session::listen_no_system_port);
    if (ec) {
        FATAL_LOG("listen on port %d failed.", listen_port);
        return false;
    }
    int upload_rate_limit = -1;
    int connections_limit = -1;
    {
        // get agent config from sqlite or configure file
        message::AgentOptions agent_options;
        if (get_agent_options_from_db(&agent_options)) {
            upload_rate_limit = agent_options.upload_limit() * 1024 * 1024;
            connections_limit = agent_options.max_connections();
        } else {
            upload_rate_limit = g_agent_configure->upload_limit();
            connections_limit = g_agent_configure->connection_limit();
        }
        if (upload_rate_limit > 500 * 1024 * 1024) {
            upload_rate_limit = 500 * 1024 * 1024;
        }
        NOTICE_LOG("agent upload rate limit:%d, connection limit:%d",
                upload_rate_limit,
                connections_limit);

        session_settings settings = libtorrent::high_performance_seed();
        settings.use_php_tracker = false;
        settings.use_c_tracker = true;
        settings.stop_tracker_timeout = 1;
        settings.no_connect_privileged_ports = false;
        settings.listen_queue_size = 200;
        settings.alert_queue_size = 50000;
        settings.num_want = g_agent_configure->peers_num_want();
        if (g_agent_configure->disable_os_cache()) {
            settings.disk_io_read_mode = session_settings::disable_os_cache;
            settings.disk_io_write_mode = session_settings::disable_os_cache;
        } else {
            settings.disk_io_read_mode = session_settings::enable_os_cache;
            settings.disk_io_write_mode = session_settings::enable_os_cache;
        }
        settings.max_metadata_size = g_agent_configure->max_metadata_size();
        settings.min_announce_interval = g_agent_configure->seeding_announce_interval();
        settings.min_reconnect_time = g_agent_configure->min_reconnect_time();
        settings.peer_connect_timeout = g_agent_configure->peer_connect_timeout();
        settings.upload_rate_limit = upload_rate_limit;
        settings.max_queued_disk_bytes = g_agent_configure->max_queued_disk_bytes();
        settings.max_out_request_queue = g_agent_configure->max_out_request_queue();
        settings.max_allowed_in_request_queue = g_agent_configure->max_allowed_in_request_queue();
        settings.whole_pieces_threshold = g_agent_configure->whole_pieces_threshold();
        settings.request_queue_time = g_agent_configure->request_queue_time();
        settings.cache_size = g_agent_configure->cache_size();
        settings.cache_expiry = g_agent_configure->cache_expiry();
        settings.read_cache_line_size = g_agent_configure->read_cache_line_size();
        settings.write_cache_line_size = g_agent_configure->write_cache_line_size();
        settings.file_pool_size = g_agent_configure->file_pool_size();
        settings.send_buffer_watermark = g_agent_configure->send_buffer_watermark();
        settings.send_buffer_low_watermark = g_agent_configure->send_buffer_low_watermark();
        settings.send_socket_buffer_size = g_agent_configure->send_socket_buffer_size();
        settings.recv_socket_buffer_size = g_agent_configure->recv_socket_buffer_size();
        settings.dont_count_slow_torrents = true;
        settings.active_seeds = g_agent_configure->active_seeds();
        settings.active_limit = g_agent_configure->active_seeds();
        settings.connections_limit = connections_limit;
        settings.max_peerlist_size = connections_limit;
        settings.ignore_limits_on_local_network = false;
        settings.enable_outgoing_utp = false;
        settings.enable_incoming_utp = false;
        settings.auto_manage_prefer_seeds = true;
        settings.incoming_starts_queued_torrents = true;
        settings.seeding_outgoing_connections = true;
        settings.choking_algorithm = session_settings::auto_expand_choker;
        settings.seed_choking_algorithm = session_settings::round_robin;
        settings.unchoke_slots_limit = 1000;
        settings.anonymous_mode = false;
        settings.handshake_timeout = 10;
        if (g_agent_configure->suggest_mode()) {
            settings.explicit_read_cache = true;
            settings.suggest_mode = session_settings::suggest_read_cache;
        } else {
            settings.explicit_read_cache = false;
            settings.suggest_mode = session_settings::no_piece_suggestions;
        }
        _session->set_settings(settings);
    }

    shared_ptr<libtorrent::plugin> plugin(
            new LibtorrentPlugin(g_agent_configure->release_files_interval()));
    _session->add_extension(plugin);
    _session->add_extension(&libtorrent::create_metadata_plugin);
    _session->add_extension(&create_torrent_stat_plugin);
    _session->add_extension(&create_piece_hash_plugin);
    _listen_port = _session->listen_port();
    _thrift_tracker.set_peerid(_session->id().to_string());
    _session->add_ex_announce_func(bind(&ThriftTracker::announce, &_thrift_tracker, _1, _2));
    _worker_pool.get_io_service().post(bind(&TaskManager::process_alert, this));
    add_resume_task();
    _worker_pool.start(g_agent_configure->worker_thread_num());
    _thrift_tracker_pool.start(g_agent_configure->worker_thread_num());

    // start uri_agent
    if (g_agent_configure->tcp_server_port() == 0) {
        return true;
    }
    _uri_agent.set_tcp_port(g_agent_configure->tcp_server_port());
    _uri_agent.set_max_connection(connections_limit);
    _uri_agent.set_total_upload_limit(upload_rate_limit);
    _uri_agent.set_max_cache_size(g_agent_configure->cache_size());
    _uri_agent.set_cache_expire_time(g_agent_configure->cache_expiry());
    _uri_agent.set_read_line_size(g_agent_configure->uri_agent_read_line_size());
    _uri_agent.set_disk_thread_number(g_agent_configure->uri_agent_disk_thread_number());
    _uri_agent.set_default_upload_limit_for_data(g_agent_configure->default_upload_limit_for_data());
    return _uri_agent.start();
}

void TaskManager::join() {
    _control_server.join();
    _worker_pool.join_all();
    if (g_agent_configure->tcp_server_port() != 0) {
        _uri_agent.join();
    }
    _session.reset();
}

void TaskManager::stop() {
    _control_server.stop();
    persist_tasks();
    usleep(100000);  //  wait for task quit announce 100ms
    _worker_pool.stop();
    _thrift_tracker_pool.stop();
    if (g_agent_configure->tcp_server_port() != 0) {
        _uri_agent.stop();
    }
}

void TaskManager::add_task(
        const shared_ptr<UnixSocketConnection> &connection,
        message::AddTask &message,
        error_code &ec) {
    message::Task *task_info = message.mutable_task();
    {
        struct ucred cred = any_cast<struct ucred>(connection->get_user_argument());
        task_info->set_uid(cred.uid);
        task_info->set_gid(cred.gid);
    }

    intrusive_ptr<torrent_info> ti;
    if (task_info->has_data()) {
        const string &data = task_info->data();
        ti.reset(new torrent_info(data.data(), data.length(), ec, 0, task_info->new_name()));
        if (ec) {
            WARNING_LOG("parse torrent failed, ret(%d): %s.", ec.value(), ec.message().c_str());
            return;
        }
        task_info->set_infohash(libtorrent::to_hex(ti->info_hash().to_string()));
    } else if (task_info->has_infohash() || task_info->has_uri()) {
        shared_ptr<Metadata> meta = get_metadata_by_taskinfo(*task_info);
        if (!meta) {
            WARNING_LOG("[%s%s]: no metadata to find",
                    task_info->infohash().c_str(), task_info->uri().c_str());
            ec.assign(bbts::errors::NO_METADATA, get_error_category());
            return;
        }
        task_info->set_infohash(meta->infohash);

        libtorrent::lazy_entry metadata;
        libtorrent::lazy_bdecode(meta->metadata.c_str(),
                meta->metadata.c_str() + meta->metadata.length(), metadata, ec);
        if (ec) {
            WARNING_LOG("bdecode metadata(%s) failed: %s.",
                    meta->infohash.c_str(), ec.message().c_str());
            return;
        }

        string bytes;
        hex_decode(meta->infohash, &bytes);
        ti.reset(new torrent_info(libtorrent::sha1_hash(bytes), 0, task_info->new_name()));
        if (!ti->parse_info_section(metadata, ec, 0)) {
            WARNING_LOG("parse metadata(%s) failed: %s",
                    meta->infohash.c_str(), ec.message().c_str());
                return;
        }
        DEBUG_LOG("get metadata(%s) success", meta->infohash.c_str());
    } else {
        WARNING_LOG("not set info_hash or torrent_path or torrent_url");
        ec.assign(bbts::errors::MSG_ARGS_ERROR, get_error_category());
        return;
    }

    if (task_info->new_name().empty()) {
        task_info->set_new_name(ti->name());
    }

    {
        shared_ptr<Task> dup_task;
        string data_path = task_info->save_path() + '/' + task_info->new_name();
        scoped_lock lock(_tasks_lock);
        InfohashMap::iterator infohash_it = _infohash_map.find(task_info->infohash());
        DataPathMap::iterator data_path_it = _data_path_map.find(data_path);
        if (infohash_it != _infohash_map.end()) {
            dup_task = infohash_it->second.lock();
        } else if (data_path_it != _data_path_map.end()) {
            dup_task = data_path_it->second.lock();
        }
        if (dup_task) {
            DEBUG_LOG("dup task type: %d, new task type: %d",
                    dup_task->get_type(), task_info->type());
            if (dup_task->get_type() == message::NOCHECK_TASK
                    || task_info->type() == message::SEEDING_TASK) {
                WARNING_LOG("new task conflict with task(%ld), it will quit.", dup_task->get_id());
                ERASE_TASK_FROM_MAP(dup_task);
                dup_task->remove_torrent();
                dup_task->cancel();
            } else {
                WARNING_LOG(
                        "add torrent(%s) failed: new nocheck task(%s:%s) conflict with seed task.",
                        task_info->infohash().c_str(),
                        data_path.c_str());
                ec.assign(bbts::errors::DUP_TASK_ERROR, get_error_category());
                return;
            }
        }
    }

    // 如果task 数目达到active_seeds上限，删掉起始时间+seeing time 最少的10% 任务
    {
        scoped_lock lock(_tasks_lock);
        if (static_cast<int32_t>(_tasks_map.size()) >= g_agent_configure->active_seeds()) {
            TRACE_LOG("tasks num is %ld, too many, more than config set %ld", _tasks_map.size(), 
                    g_agent_configure->active_seeds());
            std::multimap<int64_t, int64_t> delete_task_map;
            // 计算task起始时间+ seeding time , 利用map自身有序
            for (TasksMap::iterator it = _tasks_map.begin(); it != _tasks_map.end(); ++it) {
                int64_t end_time = it->second->get_taskstat().start_time; 
                if (it->second->get_seeding_time() == -1) {
                    // 10 * 365 * 24 * 60 * 60 十年
                    end_time += 315360000L; 
                } else {
                    end_time += it->second->get_seeding_time();
                }
                TRACE_LOG("[task id: %ld] seeding time: %d, calc time: %ld", 
                        it->first, it->second->get_seeding_time(), end_time);
                delete_task_map.insert(std::pair<int64_t, int64_t>(end_time, it->first));
            } 
            // 删除最早1% 的任务
            int32_t active_now = static_cast<int32_t>(g_agent_configure->active_seeds() * 0.99);
            std::multimap<int64_t, int64_t>::iterator it = delete_task_map.begin();
            while (static_cast<int32_t>(_tasks_map.size()) > active_now) {
                TasksMap::iterator im;
                im = _tasks_map.find(it->second);
                if (im == _tasks_map.end()) {
                    WARNING_LOG("delete task occur error, task id %ld not found!", it->second);
                    continue;
                } 
                shared_ptr<Task> task = im->second;
                TRACE_LOG("delete old task id: %ld, infohash: %s, path: %s", it->second, 
                        task->get_infohash().c_str(), task->get_data_path().c_str());
                ERASE_TASK_FROM_MAP(task);
                task->remove_torrent();
                task->cancel();
                ++it;
            } 
        }
    }

    shared_ptr<Task> task;
    {
        scoped_lock lock(_tasks_lock);
        task = Task::create(message, ti, ec);
        if (ec) {
            return;
        }
        ADD_TASK_TO_MAP(task);
    }

    NOTICE_LOG("add task(%ld) success", task->get_id());
    message::TaskRes response;
    response.set_taskid(task->get_id());
    write_message(connection, RES_TASK, response);
    return;
}

void TaskManager::ctrl_task_by_id(
        int64_t taskid,
        const TasksMap::iterator &it,
        const struct ucred &cred,
        message::BatchCtrl::ctrl_t type,
        message::BatchCtrlRes &response,
        bool skip) {
    message::TaskRes task_res;
    message::BaseRes *base_res = task_res.mutable_res();
    task_res.set_taskid(taskid);
    if (it == _tasks_map.end()) {
        base_res->set_ret_code(bbts::errors::TASK_NOT_FOUND);
        base_res->set_fail_msg("task not found");
        response.add_tasks()->CopyFrom(task_res);
        return;
    }

    shared_ptr<Task> task = it->second;
    if (!task->check_cred(cred)) {
        if (!skip) {
            base_res->set_ret_code(bbts::errors::CHECK_CRED_FAILED);
            base_res->set_fail_msg("check cred fail");
            response.add_tasks()->CopyFrom(task_res);
        }
        return;
    }

    switch (type) {
    case message::BatchCtrl::CANCEL:
        ERASE_TASK_FROM_MAP(task);
        task->cancel();
        break;
    case message::BatchCtrl::PAUSE:
        task->pause();
        break;
    case message::BatchCtrl::RESUME:
        task->resume();
        break;
    default:
        break;        //not reach here
    }
    base_res->set_ret_code(bbts::errors::NO_ERROR);
    base_res->set_fail_msg("No error");
    response.add_tasks()->CopyFrom(task_res);
}

void TaskManager::ctrl_task_by_infohash(
        const InfohashMap::iterator &it,
        const struct ucred &cred,
        message::BatchCtrl::ctrl_t type,
        message::BatchCtrlRes &response,
        bool skip) {
    message::TaskRes task_res;
    message::BaseRes *base_res = task_res.mutable_res();
    
    task_res.set_taskid(-1);
    if (it == _infohash_map.end()) {
        base_res->set_ret_code(bbts::errors::TASK_NOT_FOUND);
        base_res->set_fail_msg("task not found depand on infohash");
        response.add_tasks()->CopyFrom(task_res);
        return;
    }
    shared_ptr<Task> task = it->second.lock();
    if (!task) {
        base_res->set_ret_code(bbts::errors::TASK_NOT_FOUND);
        base_res->set_fail_msg("task not found depand on infohash");
        response.add_tasks()->CopyFrom(task_res);
        return;
    }
    task_res.set_taskid(task->get_id());
    
    if (!task->check_cred(cred)) {
        if (!skip) {
            base_res->set_ret_code(bbts::errors::CHECK_CRED_FAILED);
            base_res->set_fail_msg("check cred fail");
            response.add_tasks()->CopyFrom(task_res);
        }
        return;
    }

    switch (type) {
    case message::BatchCtrl::CANCEL:
        ERASE_TASK_FROM_MAP(task);
        task->cancel();
        break;
    case message::BatchCtrl::PAUSE:
        task->pause();
        break;
    case message::BatchCtrl::RESUME:
        task->resume();
        break;
    default:
        break;        //not reach here
    }
    base_res->set_ret_code(bbts::errors::NO_ERROR);
    base_res->set_fail_msg("No error");
    response.add_tasks()->CopyFrom(task_res);
}

void TaskManager::batch_ctrl_tasks(
        const shared_ptr<UnixSocketConnection> &connection,
        const message::BatchCtrl &message) {
    struct ucred cred = any_cast<struct ucred>(connection->get_user_argument());
    message::BatchCtrlRes response;
    TasksMap::iterator it;
    InfohashMap::iterator it_info;
    if (message.ctrl_all()) {
        scoped_lock lock(_tasks_lock);
        for (it = _tasks_map.begin(); it != _tasks_map.end();) {
            TasksMap::iterator curit = it++;
            ctrl_task_by_id(curit->first, curit, cred, message.ctrl_type(), response, true);
        }
    } else {
        scoped_lock lock(_tasks_lock);
        for (int i = 0; i < message.taskids_size(); ++i) {
            int64_t taskid = message.taskids(i);
            it = _tasks_map.find(taskid);
            ctrl_task_by_id(taskid, it, cred, message.ctrl_type(), response, false);
        }
        for (int i = 0; i < message.infohashs_size(); ++i) {
            it_info = _infohash_map.find(message.infohashs(i));
            ctrl_task_by_infohash(it_info, cred, message.ctrl_type(), response, false);
        }
    }
    write_message(connection, RES_BATCH_CTRL, response);
}

void TaskManager::list_task(
        int64_t taskid,
        const TasksMap::iterator &it,
        message::BatchListRes &response) {
    shared_ptr<Task> task;
    if (it == _tasks_map.end()) {
        task = Task::create(taskid);
    } else {
        task = it->second;
    }
    message::TaskStatus *task_status = response.add_status();
    task->get_status(task_status);
}

void TaskManager::list_tasks(
        const shared_ptr<UnixSocketConnection> &connection,
        const message::BatchCtrl &message) {
    message::BatchListRes response;
    TasksMap::iterator it;
    if (message.ctrl_all()) {
        scoped_lock lock(_tasks_lock);
        for (it = _tasks_map.begin(); it != _tasks_map.end(); ++it) {
            list_task(it->first, it, response);
        }
    } else {
        scoped_lock lock(_tasks_lock);
        for (int i = 0; i < message.taskids_size(); ++i) {
            int64_t taskid = message.taskids(i);
            it = _tasks_map.find(taskid);
            list_task(taskid, it, response);
        }
    }
    write_message(connection, RES_BATCH_LIST, response);
}

void TaskManager::set_task_options(
        const shared_ptr<UnixSocketConnection> &connection,
        const message::TaskOptions &options,
        error_code &ec) {
    if (options.has_infohash()) {
        // uri_agent
        if (!_uri_agent.set_upload_limit(options.infohash(), options.upload_limit())) {
            ec.assign(bbts::errors::URI_AGENT_SET_OPTION_ERROR, get_error_category());
        }
        write_base_message(connection, ec);
        return;
    }

    shared_ptr<Task> task;
    {
        scoped_lock lock(_tasks_lock);
        TasksMap::iterator it = _tasks_map.find(options.taskid());
        if (it == _tasks_map.end()) {
            WARNING_LOG("task(%ld) not found in running tasks", options.taskid());
            ec.assign(bbts::errors::TASK_NOT_FOUND, get_error_category());
            return;
        }
        task = it->second;
        if (!task->check_cred(any_cast<struct ucred>(connection->get_user_argument()))) {
            ec.assign(bbts::errors::CHECK_CRED_FAILED, get_error_category());
            return;
        }
    }
    task->set_options(options);
    write_base_message(connection, ec);
}

void TaskManager::get_task_options(
        const shared_ptr<UnixSocketConnection> &connection,
        message::TaskOptions *options,
        error_code &ec) {
    shared_ptr<Task> task;
    {
        scoped_lock lock(_tasks_lock);
        TasksMap::iterator it = _tasks_map.find(options->taskid());
        if (it == _tasks_map.end()) {
            WARNING_LOG("task(%ld) not found in running task", options->taskid());
            ec.assign(bbts::errors::TASK_NOT_FOUND, get_error_category());
            return;
        }
        task = it->second;
    }
    task->get_options(options);
    write_message(connection, RES_TASK_GETOPT, *options);
}
void TaskManager::set_agent_options(
        const shared_ptr<UnixSocketConnection> &connection,
        const message::AgentOptions &options,
        error_code &ec) {
    struct ucred cred = any_cast<struct ucred>(connection->get_user_argument());
    if (cred.uid != 0 && cred.uid != g_process_info->uid()) {
        NOTICE_LOG("uid %d have no cred for change agent opt.", cred.uid);
        ec.assign(bbts::errors::CHECK_CRED_FAILED, get_error_category());
        return;
    }
    session_settings settings = _session->settings();
    if (options.has_max_connections()) {
        settings.connections_limit = options.max_connections();
        _uri_agent.set_max_connection(options.max_connections());
    }
    if (options.has_upload_limit()) {
        settings.upload_rate_limit = options.upload_limit();
        _uri_agent.set_total_upload_limit(options.upload_limit());
    }
    _session->set_settings(settings);

    // update sqlite db
    int upload_rate_limit = static_cast<int>(settings.upload_rate_limit / 1024 / 1024);
    stringstream sql;
    sql << "update agent_config set connection_limit = " << settings.connections_limit
            << ", upload_limit = " << upload_rate_limit;
    bool is_update_success = false;
    for (int i = 0; i < 3; ++i) {
        if (!_task_db->excute(sql.str())) {
            WARNING_LOG("update agent options in sqlite(%s) failed:%d", sql.str().c_str(), i);
            _task_db->reconnect();
            continue;
        }
        is_update_success = true;
    }

    if (!is_update_success) {
        WARNING_LOG("update agent options failed!");
    }

    NOTICE_LOG("Agent setopt uplimit: %s/s, maxconn: %d",
            StringUtil::bytes_to_readable(settings.upload_rate_limit).c_str(),
            settings.connections_limit);
    write_base_message(connection, ec);
}

void TaskManager::get_agent_options(
        const shared_ptr<UnixSocketConnection> &connection,
        message::AgentOptions *options) {
    session_settings settings = _session->settings();
    options->set_bind_port(_session->listen_port());
    if (options->has_max_connections()) {
        options->set_max_connections(settings.connections_limit);
    }
    if (options->has_upload_limit()) {
        options->set_upload_limit(settings.upload_rate_limit);
    }
    write_message(connection, RES_AGENT_GETOPT, *options);
}

void TaskManager::get_status(const boost::shared_ptr<UnixSocketConnection> &connection) {
    libtorrent::session_status session_status = _session->status();
    message::Status status;
    status.set_upload_rate(session_status.upload_rate);
    status.set_download_rate(session_status.download_rate);
    status.set_total_download(session_status.total_download);
    status.set_total_upload(session_status.total_upload);
    status.set_payload_download_rate(session_status.payload_download_rate);
    status.set_payload_upload_rate(session_status.payload_upload_rate);
    status.set_total_payload_download(session_status.total_payload_download);
    status.set_total_payload_upload(session_status.total_payload_upload);
    status.set_disk_read_queue(session_status.disk_read_queue);
    status.set_disk_write_queue(session_status.disk_write_queue);
    status.set_down_bandwidth_bytes_queue(session_status.down_bandwidth_bytes_queue);
    status.set_down_bandwidth_queue(session_status.down_bandwidth_queue);
    status.set_up_bandwidth_bytes_queue(session_status.up_bandwidth_bytes_queue);
    status.set_up_bandwidth_queue(session_status.up_bandwidth_queue);
    status.set_num_peers(session_status.num_peers);
    status.set_peerlist_size(session_status.peerlist_size);
    status.set_num_unchoked(session_status.num_unchoked);
    write_message(connection, RES_STATUS, status);
}

void TaskManager::add_metadata(const message::Metadata &message) {
    int32_t metadata_length = message.data().length();

    int max_total_meta_size = g_agent_configure->max_total_meta_size();
    // meta too big or small
    if (metadata_length < 1 || metadata_length > max_total_meta_size) {
        return;
    }

    scoped_lock lock(_metas_lock);
    int metas_size = _metas.size();
    while (metas_size > 0) {
        if ((metadata_length + _current_metas_total_size > max_total_meta_size) ||
                (metas_size + 1 > g_agent_configure->max_total_meta_num())) {
            _current_metas_total_size -= _metas.front()->metadata.length();
            _metas.pop_front();
            --metas_size;
        } else {
            break;
        }
    }

    shared_ptr<Metadata> meta(new Metadata());
    meta->infohash = message.infohash();
    meta->uri = message.uri();
    meta->metadata = message.data();
    meta->add_time = time(NULL);
    _metas.push_back(meta);
    _current_metas_total_size += metadata_length;
    TRACE_LOG("add metadata success: %s", meta->infohash.c_str());
}

#define PARSE_MSG(msg) \
do {\
    if (!message.ParseFromArray(ptr + sizeof(type), data->size() - sizeof(type))) {\
      WARNING_LOG("parse message " msg " failed, type(%u)", type);\
      ec.assign(bbts::errors::PROTOBUF_PARSE_ERROR, get_error_category());\
      break;\
    }\
} while (0)

void TaskManager::process_message(
        const shared_ptr<UnixSocketConnection> &connection,
        const shared_ptr<const vector<char> > &data) {
    const char *ptr = &(*data)[0];
    uint32_t type = *reinterpret_cast<const uint32_t *>(ptr);
    DEBUG_LOG("control handler, type: %u", type);
    error_code ec;
    switch (type) {
    case REQ_BATCH_CTRL: {
        message::BatchCtrl message;
        PARSE_MSG("REQ_BATCH_CTRL");
        if (message.ctrl_type() == message.LIST) {
            list_tasks(connection, message);
        } else {
            batch_ctrl_tasks(connection, message);
        }
        break;
    }

    case REQ_ADD_TASK: {
        message::AddTask message;
        PARSE_MSG("REQ_ADD_TASK");
        add_task(connection, message, ec);
        break;
    }

    case REQ_ADD_METADATA: {
        message::Metadata message;
        PARSE_MSG("REQ_ADD_METADATA");
        add_metadata(message);
        write_base_message(connection, ec);
        break;
    }

    case REQ_TASK_SETOPT: {
        message::TaskOptions message;
        PARSE_MSG("REQ_TASK_GETOPT");
        set_task_options(connection, message, ec);
        break;
    }

    case REQ_TASK_GETOPT: {
        message::TaskOptions message;
        PARSE_MSG("REQ_TASK_GETOPT");
        get_task_options(connection, &message, ec);
        break;
    }

    case REQ_AGENT_SETOPT: {
        message::AgentOptions message;
        PARSE_MSG("REQ_AGENT_SETOPT");
        set_agent_options(connection, message, ec);
        break;
    }

    case REQ_AGENT_GETOPT: {
        message::AgentOptions message;
        PARSE_MSG("REQ_AGENT_GETOPT");
        get_agent_options(connection, &message);
        break;
    }

    case REQ_STATUS: {
        get_status(connection);
        break;
    }

    default:
        WARNING_LOG("message type (%d) not support.", type);
        ec.assign(bbts::errors::MSG_ARGS_ERROR, get_error_category());
        break;
    }

    if (ec) {
        write_base_message(connection, ec);
    }
}

void TaskManager::on_read_message(
        const shared_ptr<UnixSocketConnection> &connection,
        const shared_ptr<const vector<char> > &data) {
    _worker_pool.get_io_service().post(bind(&TaskManager::process_message, this, connection, data));
}

void TaskManager::on_accept(const shared_ptr<UnixSocketConnection> &connection) {
    int fd = connection->get_socket().native_handle();
    DEBUG_LOG("process message fd: %d", fd);
    struct ucred cred;
    if (!bbts::recv_cred(fd, &cred)) {
        WARNING_LOG("read cred failed.");
        write_base_message(connection, error_code(bbts::errors::READ_CRED_FAILED, get_error_category()));
        return;
    }
    connection->set_user_argument(cred);
}

void TaskManager::seeding_timer_callback(int taskid) {
    TasksMap::iterator it;
    scoped_lock lock(_tasks_lock);
    it = _tasks_map.find(taskid);
    if (it != _tasks_map.end()) {
        shared_ptr<Task> task = it->second;
        NOTICE_LOG("task(%ld) finished, will quit.", task->get_id());
        ERASE_TASK_FROM_MAP(task);
    }
}

void TaskManager::remove_task_by_error(shared_ptr<Task> task, const string &error) {
    task->set_error(error);
    WARNING_LOG("task(%ld) failed, will quit. error: %s.", task->get_id(), error.c_str());
    ERASE_TASK_FROM_MAP(task);
}

void TaskManager::process_task_by_torrent(torrent_handle torrent,
        boost::function<void(shared_ptr<Task>)> cb) {
    scoped_lock lock(_tasks_lock);
    if (!torrent.is_valid()) {
        return;
    }
    TorrentMap::iterator it = _torrent_map.find(torrent);
    if (it != _torrent_map.end()) {
        shared_ptr<Task> task = it->second.lock();
        if (task) {
            cb(task);
        }
    }
}

void TaskManager::on_torrent_finished(const libtorrent::torrent_finished_alert *alert) {
    NOTICE_LOG("%s", alert->message().c_str());
    process_task_by_torrent(alert->handle, bind(&Task::to_seeding, _1));
}

void TaskManager::on_listen_failed(const libtorrent::listen_failed_alert *alert) {
    const libtorrent::tcp::endpoint &ep = alert->endpoint;
    if (alert->sock_type == libtorrent::listen_failed_alert::tcp
            && ep.address().is_v4()
            && ep.port() == g_agent_configure->listen_port()) {
        FATAL_LOG("%s.", alert->message().c_str());
        stop();
    }
}

void TaskManager::on_peer_disconnect(const libtorrent::peer_disconnected_alert *alert) {
    error_code ec = alert->error;
    if (ec.value() == boost::asio::error::eof ||  // End of file
            ec.value() == libtorrent::errors::upload_upload_connection ||
            ec.value() == libtorrent::errors::torrent_aborted) {
        return;
    }
}

void TaskManager::on_torrent_error(const libtorrent::torrent_error_alert *alert) {
    process_task_by_torrent(alert->handle,
            bind(&TaskManager::remove_task_by_error, this, _1, alert->error.message()));
}

void TaskManager::process_alert() {
    if (NULL == _session->wait_for_alert(libtorrent::milliseconds(200))) {
        _worker_pool.get_io_service().post(bind(&TaskManager::process_alert, this));
        return;
    }

    deque<alert *> alerts;
    _session->pop_alerts(&alerts);
    // or put in case with {}
    // string peer_message; 
    for (deque<alert *>::iterator it = alerts.begin(); it != alerts.end(); ++it) {
        switch ((*it)->type()) {
        case libtorrent::listen_failed_alert::alert_type:
            on_listen_failed((libtorrent::listen_failed_alert *)(*it));
            break;

        case libtorrent::torrent_finished_alert::alert_type:
            on_torrent_finished((libtorrent::torrent_finished_alert *)(*it));
            break;

        case libtorrent::torrent_error_alert::alert_type:
            on_torrent_error((libtorrent::torrent_error_alert *)(*it));
            break;

        case PeerStatAlert::ALERT_TYPE:
            _peer_stat.print(*static_cast<PeerStatAlert *>(*it));
            /* to-do
            if (!peer_message.empty()) {
                size_t pos = peer_message.find(",");
                if (pos != string::npos) {
                    InfohashMap::iterator it = _infohash_map.find(peer_message.substr(0, pos));
                    if (it != _infohash_map.end()) {
                        DEBUG_LOG("add task_manager peer info: %s", peer_message.c_str());
                        it->second.lock()->add_peer_stat(peer_message); 
                    } else {
                        DEBUG_LOG("not found infohash in map");
                    }
                } else {
                    DEBUG_LOG("peer message %s can't find ','", peer_message.c_str());
                }
            }
            */
            break;

        case libtorrent::listen_succeeded_alert::alert_type:
        case libtorrent::add_torrent_alert::alert_type:
        case libtorrent::torrent_added_alert::alert_type:
        case libtorrent::torrent_removed_alert::alert_type:
        case libtorrent::torrent_deleted_alert::alert_type:
        case libtorrent::url_seed_alert::alert_type:
        case libtorrent::peer_blocked_alert::alert_type:
        case libtorrent::peer_ban_alert::alert_type:
        case libtorrent::state_changed_alert::alert_type:
        case libtorrent::torrent_resumed_alert::alert_type:
        case libtorrent::torrent_paused_alert::alert_type:
            NOTICE_LOG("%s", (*it)->message().c_str());
            break;

        case libtorrent::peer_error_alert::alert_type:
        case libtorrent::hash_failed_alert::alert_type:
        case libtorrent::torrent_delete_failed_alert::alert_type:
        case libtorrent::metadata_failed_alert::alert_type:
        case libtorrent::tracker_warning_alert::alert_type:
        case libtorrent::tracker_error_alert::alert_type:
        case libtorrent::file_error_alert::alert_type:
        case libtorrent::metadata_received_alert::alert_type:
            WARNING_LOG("%s", (*it)->message().c_str());
            break;

        case libtorrent::tracker_reply_alert::alert_type:
        case libtorrent::tracker_announce_alert::alert_type:
            TRACE_LOG("%s", (*it)->message().c_str());
            break;

        case libtorrent::peer_disconnected_alert::alert_type:
            DEBUG_LOG("%s", (*it)->message().c_str());
            //on_peer_disconnect(static_cast<libtorrent::peer_disconnected_alert *>(*it));
            break;

        case libtorrent::performance_alert::alert_type:
        case libtorrent::peer_connect_alert::alert_type:
        case libtorrent::incoming_connection_alert::alert_type:
            DEBUG_LOG("%s", (*it)->message().c_str());
            break;

        default:
            DEBUG_LOG("%s", (*it)->message().c_str());
            break;
        }

        delete *it;
    }
    alerts.clear();
    _worker_pool.get_io_service().post(bind(&TaskManager::process_alert, this));
}

}  // namespace agent
}  // namespace bbts
