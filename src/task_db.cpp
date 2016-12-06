/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file    task_db.cpp
 *
 *  @author liuming03
 *  @date   2013-5-6
 */

#include "bbts/agent/task_db.h"

#include <boost/bind.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/thread.hpp>
#include <../sqlite/sqlite3.h>

#include <bbts/log.h>
#include <bbts/timer_util.h>

namespace bbts {
namespace agent {

using std::string;
using boost::asio::io_service;
using boost::shared_ptr;
using boost::system::error_code;
using boost::posix_time::hours;
typedef boost::mutex::scoped_lock scoped_lock;

const int TaskDB::DEL_DATA_TIMER_INTERVAL = 24;

TaskDB::TaskDB(const string &db_file_name, io_service &io_service, int delete_data_interval) :
        _db_file_name(db_file_name),
        _delete_data_interval(delete_data_interval),
        _reconnecting(false),
        _del_data_timer(io_service) {
    reconnect();
    timer_run_cycle("delete data from task.db timer", _del_data_timer,
            hours(DEL_DATA_TIMER_INTERVAL), boost::bind(&TaskDB::delete_data, this));
}

TaskDB::~TaskDB() {}

bool TaskDB::reconnect() {
    {
        scoped_lock lock(_reconnecting_mutex);
        if (_reconnecting) {
            while (_reconnecting) {
                _reconnecting_cond.wait(lock);
            }
            return true;
        }
        _reconnecting = true;
    }

    sqlite3 *sqlite_db = NULL;
    if (sqlite3_open(_db_file_name.c_str(), &sqlite_db) != 0) {
        WARNING_LOG("Can't open db file %s: %s.", _db_file_name.c_str(), sqlite3_errmsg(sqlite_db));
        sqlite3_close(sqlite_db);
        return false;
    }
    NOTICE_LOG("open db file %s success.", _db_file_name.c_str());
    {
        scoped_lock lock(_task_db_mutex);
        _task_db.reset(sqlite_db, boost::bind(&sqlite3_close, _1));
    }

    {
        scoped_lock lock(_reconnecting_mutex);
        _reconnecting = false;
    }
    _reconnecting_cond.notify_all();

    if (!init_table()) {
        WARNING_LOG("init table failed.");
        return false;
    }
    return true;
}

bool TaskDB::excute(const string &sql, callback_t callback, void *userdata) {
    shared_ptr<sqlite3> db;
    {
        scoped_lock lock(_task_db_mutex);
        db = _task_db;
    }

    char *err = NULL;
    if (sqlite3_exec(db.get(), sql.c_str(), callback, userdata, &err) != SQLITE_OK) {
        WARNING_LOG("SQL error: %s", err);
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool TaskDB::init_table() {
    string sql = "CREATE TABLE IF NOT EXISTS task ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT"
            ", infohash CHAR(40) DEFAULT ''"
            ", save_path VARCHAR(1024) DEFAULT ''"
            ", cmd VARCHAR(4096) DEFAULT ''"
            ", status VARCHAR(20) DEFAULT ''"
            ", uid INT(10) DEFAULT -1"
            ", gid INT(10) DEFAULT -1"
            ", start_time TIMESTAMP DEFAULT ''"
            ", end_time TIMESTAMP DEFAULT ''"
            ", upload INT(20) DEFAULT 0"
            ", download INT(20) DEFAULT 0"
            ", progress INT(5) DEFAULT 0"
            ", error varchar(1024) DEFAULT ''"
            ")";

    string sql_config = "CREATE TABLE IF NOT EXISTS agent_config ("
            "bind_port INT(10) DEFAULT 422"
            ", upload_limit INT(10) DEFAULT 1000"
            ", connection_limit INT(10) DEFAULT 50000"
            ")";
    if (!this->excute(sql_config)) {
        WARNING_LOG("create table agent_config failed");
    }

    return this->excute(sql);
}

void TaskDB::delete_data() {
    std::stringstream sql;
    sql << "DELETE FROM task WHERE end_time != '' and end_time < datetime('now', 'localtime', '-"
            << _delete_data_interval << " day')";
    this->excute(sql.str());
}

}  // namespace agent
}  // namespace bbts

