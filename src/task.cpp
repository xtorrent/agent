/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   task.cpp
 *
 * @author liuming03
 * @date   2013-1-16
 */

#include "bbts/agent/task.h"

#include <fstream>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/system/error_code.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/session.hpp>

#include "bbts/agent/task_db.h"
#include "bbts/agent/task_manager.h"
#include "bbts/encode.h"
#include "bbts/error_category.h"
#include "bbts/lazy_singleton.hpp"
#include "bbts/log.h"
#include "bbts/path.h"
#include "bbts/process_info.h"
#include "bbts/string_util.h"
#include "bbts/timer_util.h"
#include "configure.pb.h"

using std::pair;
using std::string;
using std::stringstream;
using std::vector;
using std::make_pair;
using boost::scoped_ptr;
using boost::scoped_array;
using boost::shared_ptr;
using boost::shared_array;
using boost::intrusive_ptr;
using boost::system::error_code;
using boost::posix_time::seconds;
using libtorrent::torrent_info;
using libtorrent::add_torrent_params;
using libtorrent::torrent_handle;
using libtorrent::torrent_status;
using libtorrent::entry;
using libtorrent::lazy_entry;
using libtorrent::sha1_hash;
using bbts::message::AgentConfigure;

namespace bbts {
namespace agent {

Task::Task(int64_t id) :
        _flags(0),
        _id(id),
        _start_time(time(NULL)),
        _type(message::NOCHECK_TASK),
        _seeding_time(0),
        _seeding_timer(g_task_manager->get_io_service()) {
    _stat.hostname = g_process_info->hostname();
    error_code ec;
    _stat.ip = g_process_info->ip();
    _stat.port = g_task_manager->get_listen_port();
    _stat.start_time = time(NULL);
    _stat.progress_ppm = 1000000;
}

Task::~Task() {
    if (_flags & TASK_FLAG_NOT_NEW) {
        update_status(true);
        if (!(_flags & TASK_FLAG_PERSIST)) {
            delete_resume_file();
            NOTICE_LOG("task(%ld) has been delete.", _id);
        }
        remove_torrent();
    }
}

void Task::remove_torrent() {
    if (_torrent.is_valid()) {
        _stat.payload_uploaded = _torrent.status(0).total_payload_upload;
        _stat.end_time = time(NULL);
        _stat.time_for_seeding = _stat.end_time - _stat.start_time;
        print_task_statistics(_stat, g_agent_configure->task_stat_file());
        try {
            g_task_manager->get_session()->remove_torrent(_torrent);
        } catch (libtorrent::libtorrent_exception &e) {
            WARNING_LOG("task(%ld) %s", _id, e.what());
        }
        /* to-do
        _bbts_stat.set_task(_stat);
        if (_bbts_stat.send_stat() != 0) {
            WARNING_LOG("send statistic info failed!");
        }
        */
    }
}

void Task::cancel() {
    _flags |= TASK_FLAG_CANCELED;
}

void Task::persist() {
    _flags |= TASK_FLAG_PERSIST;
}

shared_ptr<Task> Task::create(int64_t taskid) {
    return shared_ptr<Task>(new Task(taskid));
}

shared_ptr<Task> Task::create(
        const message::AddTask &message,
        const intrusive_ptr<torrent_info> &ti,
        error_code &ec) {
    shared_ptr<Task> task(new Task(-1));
    const message::Task &task_info = message.task();
    task->_infohash = task_info.infohash();
    task->_cred.uid = task_info.uid();
    task->_cred.gid = task_info.gid();
    task->_cmd = task_info.cmd();
    task->_save_path = task_info.save_path();
    task->_new_name = task_info.new_name();
    task->_seeding_time = task_info.seeding_time();
    task->_type = task_info.type();

    if (!task->gen_task_id()) {
        ec.assign(bbts::errors::GENERATE_TASK_ID_FAILED, get_error_category());
        return shared_ptr<Task>();
    }

    add_torrent_params params;
    params.ti = ti;
    params.save_path = task_info.save_path();
    if (task_info.trackers_size() != 0) {
        for (int i = 0; i < task_info.trackers_size(); ++i) {
            const message::Server &tracker = task_info.trackers(i);
            task->_trackers.push_back(make_pair(tracker.host(), tracker.port()));
        }
        params.thrift_trackers = task->_trackers;
    } else {
        g_task_manager->get_thrift_tracker().get_nodes(task->_infohash, &params.thrift_trackers);
    }

    params.flags |= add_torrent_params::flag_paused | add_torrent_params::flag_duplicate_is_error;
    params.flags &= ~add_torrent_params::flag_auto_managed;
    switch (task_info.type()) {
    case message::SEEDING_TASK:
        params.flags |= add_torrent_params::flag_real_seed | add_torrent_params::flag_seed_mode;
        break;

    case message::NOCHECK_TASK:
        params.flags |= add_torrent_params::flag_seed_mode;
        break;

    case message::TRANSFER_TASK:
        params.flags |= add_torrent_params::flag_transfer | add_torrent_params::flag_seed_mode;
        break;

    default:
        assert(false);
        break;
    }

    const message::TaskOptions& options = message.options();
    if (options.has_upload_limit()) {
        params.upload_limit = options.upload_limit();
        task->_stat.upload_limit = options.upload_limit();
    }
    if (options.has_max_connections()) {
        params.max_connections = options.max_connections();
    }
    torrent_handle th = g_task_manager->get_session()->add_torrent(params, ec);
    if (ec) {
        WARNING_LOG("add torrent(%s) failed, %s.", task->_infohash.c_str(), ec.message().c_str());
        return shared_ptr<Task>();
    }
    task->_torrent = th;

    task->_flags |= TASK_FLAG_NOT_NEW;
    task->generate_resume_file();  //生成恢复数据文件失败，应不影响正常任务执行

    task->_stat.infohash = task->_infohash;
    task->_stat.product = task_info.product_tag();
    task->_stat.total_size = ti->total_size();
    task->_stat.num_files = ti->num_files();
    task->_stat.num_paths = ti->get_paths().size();
    task->_stat.num_symlinks = ti->files().get_symlink_file().size();
    task->_stat.num_pieces = ti->num_pieces();
    task->resume();  //创建和启动的cmd可分离
    return task;
}

shared_ptr<Task> Task::create_from_resume_file(const string &resume_file, int64_t file_size) {
    std::ifstream ifs(resume_file.c_str(), std::ios::binary);
    if (!ifs) {
        WARNING_LOG("can't open resume file: %s", resume_file.c_str());
        return shared_ptr<Task>();
    }
    scoped_array<char> buffer(new char[file_size]);
    ifs.read(buffer.get(), file_size);
    ifs.close();
    entry resume_data = libtorrent::bdecode(buffer.get(), buffer.get() + file_size);
    if (resume_data.type() != entry::dictionary_t) {
        WARNING_LOG("bdecode resume file failed: %s, type: %d",
                resume_file.c_str(),
                resume_data.type());
        return shared_ptr<Task>();
    }

    shared_ptr<Task> task(new Task(-1));
    time_t last_start_time = resume_data["start_time"].integer();
    int last_seeding_time = resume_data["seeding_time"].integer();
    if (last_seeding_time != -1 && last_start_time > 0) {
        task->_seeding_time = last_start_time + last_seeding_time - task->_start_time;
        if (task->_seeding_time <= 0) {
            return shared_ptr<Task>();
        }
    } else {
        task->_seeding_time = last_seeding_time;
    }

    task->_id = resume_data["id"].integer();
    entry &cred_e = resume_data["cred"];
    task->_cred.pid = cred_e["pid"].integer();
    task->_cred.uid = cred_e["uid"].integer();
    task->_cred.gid = cred_e["gid"].integer();
    task->_cmd = resume_data["cmd"].string();
    task->_new_name = resume_data["new_name"].string();
    task->_save_path = resume_data["save_path"].string();
    task->_type = static_cast<message::TaskType>(resume_data["type"].integer());

    error_code ec;
    add_torrent_params params;
    if (resume_data.find_key("info_hash") != NULL) {
        task->_infohash = resume_data["info_hash"].string();
        string bytes;
        if (task->_infohash.length() != 40 || !hex_decode(task->_infohash, &bytes)) {
            WARNING_LOG("task(%ld) info hash(%s) not correct.", task->_id, task->_infohash.c_str());
            return shared_ptr<Task>();
        }
        params.ti = new torrent_info(sha1_hash(bytes), 0, task->_new_name);
        entry *meta_entry = resume_data.find_key("metadata");
        if (meta_entry) {
            string &meta_buf = meta_entry->string();
            lazy_entry metadata;
            int ret = libtorrent::lazy_bdecode(
                    meta_buf.c_str(),
                    meta_buf.c_str() + meta_buf.length(),
                    metadata,
                    ec);
            if (ret != 0 || !params.ti->parse_info_section(metadata, ec, 0)) {
                WARNING_LOG("Parse metadata from resume file(%) failed: %s.",
                        resume_file.c_str(), ec.message().c_str());
                return shared_ptr<Task>();
            }
        } else {
            WARNING_LOG("Resume file(%s) not found metadata!", resume_file.c_str());
            return shared_ptr<Task>();
        }
    } else {
        WARNING_LOG("Resume file(%s) not found infohash!", resume_file.c_str());
        return shared_ptr<Task>();
    }

    if (resume_data.find_key("trackers") != NULL) {
        entry::list_type &trackers_e = resume_data["trackers"].list();
        for (entry::list_type::iterator it = trackers_e.begin(); it != trackers_e.end(); ++it) {
            task->_trackers.push_back(make_pair((*it)["host"].string(), (*it)["port"].integer()));
        }
        params.thrift_trackers = task->_trackers;
    } else {
        g_task_manager->get_thrift_tracker().get_nodes(task->_infohash, &params.thrift_trackers);
    }

    params.save_path = task->_save_path;
    params.flags |= add_torrent_params::flag_paused | add_torrent_params::flag_duplicate_is_error;
    params.flags &= ~add_torrent_params::flag_auto_managed;
    switch (task->_type) {
    case message::SEEDING_TASK:
        params.flags |= add_torrent_params::flag_real_seed | add_torrent_params::flag_seed_mode;
        break;

    case message::NOCHECK_TASK:
        params.flags |= add_torrent_params::flag_seed_mode;
        break;

    case message::TRANSFER_TASK:
        params.flags |= add_torrent_params::flag_transfer | add_torrent_params::flag_seed_mode;
        break;

    default:
        assert(false);
        break;
    }

    torrent_handle th = g_task_manager->get_session()->add_torrent(params, ec);
    if (ec) {
        WARNING_LOG("add torrent(%s) failed, %s.", task->_infohash.c_str(), ec.message().c_str());
        return shared_ptr<Task>();
    }
    task->_torrent = th;
    task->_torrent.set_max_connections(resume_data["max_connections"].integer());
    task->_torrent.set_upload_limit(resume_data["upload_limit"].integer());
    task->_flags |= TASK_FLAG_NOT_NEW;
    task->generate_resume_file();
    task->_stat.infohash = task->_infohash;
    task->_stat.total_size = params.ti->total_size();
    task->_stat.piece_length = params.ti->piece_length();
    task->_stat.num_files = params.ti->num_files();
    task->_stat.num_paths = params.ti->get_paths().size();
    task->_stat.num_symlinks = params.ti->files().get_symlink_file().size();
    task->_stat.num_pieces = params.ti->num_pieces();
    task->_stat.upload_limit = resume_data["upload_limit"].integer();
    task->resume();
    return task;
}

static int insert_task_cb(void *arg, int argc, char **argv, char **col_name) {
    *static_cast<int64_t *>(arg) = atol(argv[0]);
    return 0;
}

bool Task::gen_task_id() {
    stringstream sql;
    // 原子操作，为了获取自增id
    sql << "begin transaction; "
            "insert into task(infohash, save_path, status, uid, gid, cmd, start_time) values('"
            << _infohash << "', '" << get_data_path() << "', '"
            << message::TaskStatus_status_t_Name(message::TaskStatus_status_t_DOWNLOAD) << "', '"
            << _cred.uid << "', '" << _cred.gid << "', '" << _cmd
            << "', datetime('now', 'localtime'))"
            << "; select last_insert_rowid(); commit transaction";

    for (int i = 0; i < 3; ++i) {
        TaskDB *db = g_task_manager->get_task_db();
        if (!db->excute(sql.str(), &insert_task_cb, &_id) || _id < 0) {
            WARNING_LOG("excute sql failed(%s), reconnect and try again(%d)", sql.str().c_str(), i);
            db->reconnect();
            continue;
        }
        break;
    }
    return _id >= 0;
}

bool Task::check_cred(const struct ucred &cred) const {
    //root用户或启动agent的用户或任务发起者
    if (!(_flags & TASK_FLAG_NOT_NEW) || cred.uid == 0 || cred.uid == g_process_info->uid()
            || cred.uid == _cred.uid) {
        return true;
    }
    return false;
}

void Task::resume() const {
    _torrent.resume();
    _torrent.auto_managed(true);
}

void Task::pause() const {
    _torrent.auto_managed(false);
    _torrent.pause();
}

void Task::set_options(const message::TaskOptions &task_opt) const {
    bool modify = false;
    if (task_opt.has_upload_limit()) {
        _torrent.set_upload_limit(task_opt.upload_limit());
        modify = true;
    }
    if (task_opt.has_max_connections()) {
        _torrent.set_max_connections(task_opt.max_connections());
        modify = true;
    }

    NOTICE_LOG("Task(%ld) setopt uplimit: %s/s, maxconn: %d",
            _id, StringUtil::bytes_to_readable(_torrent.upload_limit()).c_str(),
            _torrent.max_connections());
    if (modify) {
        generate_resume_file();
    }
}

void Task::get_options(message::TaskOptions *task_opt) const {
    if (task_opt->has_upload_limit()) {
        task_opt->set_upload_limit(_torrent.upload_limit());
    }
    if (task_opt->has_max_connections()) {
        task_opt->set_max_connections(_torrent.max_connections());
    }
}

message::TaskStatus::status_t Task::get_status_code(
        const torrent_status &ts, bool is_finish) const {
    if (!ts.error.empty() || !_error.empty()) {
        return message::TaskStatus_status_t_ERROR;
    }

    if (_flags & TASK_FLAG_CANCELED) {
        return message::TaskStatus_status_t_CANCELED;
    } else if (ts.paused) {
        return message::TaskStatus_status_t_PAUSED;
    } else if (ts.is_finished) {
        if (!is_finish) {
            return message::TaskStatus_status_t_SEEDING;
        }
        return message::TaskStatus_status_t_FINISHED;
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

static int get_status_cb(void *arg, int argc, char **argv, char **col_name) {
    message::TaskStatus *task_status = (message::TaskStatus *)(arg);
    message::Task *task_info = task_status->mutable_task();
    task_info->set_infohash(string(argv[1]));
    string data_path = string(argv[2]);
    task_info->set_save_path(Path::parent_dir(data_path));
    task_info->set_new_name(Path::short_name(data_path));
    task_info->set_uid(atoi(argv[6]));
    task_info->set_gid(atoi(argv[7]));
    task_info->set_cmd(string(argv[8]));

    message::TaskStatus::status_t status;
    if (!message::TaskStatus::status_t_Parse(string(argv[3]), &status)) {
        status = message::TaskStatus_status_t_UNKNOW;
    }
    task_status->set_status(status);
    task_status->set_progress(atoi(argv[4]));
    task_status->set_total_upload(atol(argv[5]));
    task_status->set_error(string(argv[9]));
    return 0;
}

void Task::get_status(message::TaskStatus *task_status) const {
    message::Task *task_info = task_status->mutable_task();
    task_info->set_taskid(_id);
    if (_flags & TASK_FLAG_NOT_NEW) {  //从本地获取状态信息
        task_info->set_cmd(_cmd);
        task_info->set_uid(_cred.uid);
        task_info->set_gid(_cred.gid);
        task_info->set_infohash(_infohash);
        task_info->set_save_path(_save_path);
        task_info->set_new_name(_new_name);

        torrent_status ts = _torrent.status(0);
        task_status->set_status(get_status_code(ts, false));
        task_status->set_error(ts.error.empty() ? _error : ts.error);
        task_status->set_progress(ts.progress_ppm);
        task_status->set_num_peers(ts.num_peers);
        task_status->set_num_seeds(ts.num_seeds);
        task_status->set_upload_rate(ts.upload_rate);
        task_status->set_total_upload(ts.total_payload_upload);
        return;
    } else {  // 从taskdb获取task状态信息
        stringstream sql;
        sql << "select id, infohash, save_path, status, progress, upload, uid, gid, cmd, error "
                "from task where id = " << _id;
        bool excute_success = false;
        TaskDB *db = g_task_manager->get_task_db();
        for (int i = 0; i < 3; ++i) {
            if (!db->excute(sql.str(), get_status_cb, task_status)) {
                db->reconnect();
                WARNING_LOG("excute sql(%s) failed %d.", sql.str().c_str(), i);
                continue;
            }
            excute_success = true;
            break;
        }

        if (!excute_success) {
            task_status->set_status(message::TaskStatus::UNKNOW);
            task_status->set_error("select from db error");
            return;
        }

        if (!task_info->has_infohash()) {
            task_status->set_status(message::TaskStatus::UNKNOW);
            task_status->set_error("task not found");
            WARNING_LOG("not find task %ld", _id);
            return;
        }
    }
}

void Task::to_seeding() {
    update_status(false);
    if (!_error.empty()) {
        _seeding_time = 0;
    }

    if (_seeding_time == -1) {
        NOTICE_LOG("task(%ld) seeding for infinite", _id, _seeding_time);
        return;
    }
    NOTICE_LOG("Task(%ld) seeding for %d seconds", _id, _seeding_time);
    timer_run_once("seeding timer", _seeding_timer, seconds(_seeding_time),
            boost::bind(&TaskManager::seeding_timer_callback, g_task_manager, _id));
}

void Task::update_status(bool is_finish) const {
    if (!(_flags & TASK_FLAG_NOT_NEW)) {
        return;
    }

    /*
     * status字段在数据库上直接用字符串表示比数字表示的好处是：
     * agent如果改变了相应数字，只要对应的字符串不变就可以，这样数据库中的数据无须跟着进行调整
     */
    string status;
    torrent_status ts = _torrent.status(0);
    status = message::TaskStatus_status_t_Name(get_status_code(ts, is_finish));
    string error = ts.error.empty() ? _error : ts.error;

    stringstream sql;
    sql << "update task set infohash = '" << _infohash.c_str() << "', status = '" << status
            << "', upload = "
            << ts.total_payload_upload << ", download = " << ts.total_payload_download
            << ", progress = " << ts.progress_ppm
            << ", error = '" << error << "'";
    if (is_finish) {
        sql << ", end_time = datetime('now', 'localtime')";
    }
    sql << " where id = " << _id;
    TaskDB *db = g_task_manager->get_task_db();
    if (!db->excute(sql.str())) {
        WARNING_LOG("excute sql(%s) failed.", sql.str().c_str());
        db->reconnect();
        return;
    }

    DEBUG_LOG("update task(%ld) status success.", _id);
}

void Task::generate_resume_file() const {
    if (!(_flags & TASK_FLAG_NOT_NEW)) {
        return;
    }

    entry resume_data;
    entry &cred_e = resume_data["cred"];
    cred_e["pid"] = _cred.pid;
    cred_e["uid"] = _cred.uid;
    cred_e["gid"] = _cred.gid;
    resume_data["id"] = _id;
    resume_data["start_time"] = _start_time;
    resume_data["cmd"] = _cmd;
    resume_data["seeding_time"] = _seeding_time;
    resume_data["save_path"] = _save_path;
    resume_data["new_name"] = _new_name;
    resume_data["upload_limit"] = _torrent.upload_limit();
    resume_data["download_limit"] = _torrent.download_limit();
    resume_data["max_connections"] = _torrent.max_connections();
    resume_data["type"] = _type;
    resume_data["info_hash"] = _infohash;
    if (!_trackers.empty()) {
        entry &trackers_e = resume_data["trackers"];
        for (vector<pair<string, int> >::const_iterator it = _trackers.begin();
                it != _trackers.end(); ++it) {
            entry tracker_e;
            tracker_e["host"] = it->first;
            tracker_e["port"] = it->second;
            trackers_e.list().push_back(tracker_e);
        }
    }

    boost::intrusive_ptr<torrent_info const> ti = _torrent.torrent_file();
    if (ti->info_hash() == sha1_hash(0)) {
        WARNING_LOG("task(%ld) have no metadata to resume file, skip.", _id);
        return;
    }
    shared_array<char> metadata = ti->metadata();
    int metadata_size = ti->metadata_size();
    resume_data["metadata"] = string(metadata.get(), metadata.get() + metadata_size);

    stringstream strm;
    strm << g_agent_configure->resume_dir() << "/" << _id << "." << "resume";
    std::ofstream ofs(strm.str().c_str(), std::ios::binary);
    if (!ofs) {
        WARNING_LOG("open file failed: %s.", strm.str().c_str());
        return;
    }

    libtorrent::bencode(std::ostream_iterator<char>(ofs), resume_data);
    ofs.close();
    TRACE_LOG("generate resume data to file: %s", strm.str().c_str());
    return;
}

void Task::delete_resume_file() const {
    if (!(_flags & TASK_FLAG_NOT_NEW)) {
        return;
    }
    stringstream strm;
    strm << g_agent_configure->resume_dir() << "/" << _id << "."
            << "resume";
    unlink(strm.str().c_str());
}

}  // namespace agent
}  // namespace bbts
