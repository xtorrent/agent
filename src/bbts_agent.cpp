/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   bbts_agent.cpp
 *
 * @author liuming03
 * @date   2013-1-9
 */
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>

#include <string>

#include "bbts/agent/task_manager.h"
#include "bbts/config.h"
#include "bbts/file.h"
#include "bbts/log.h"
#include "bbts/path.h"
#include "bbts/pbconf.hpp"
#include "bbts/process_info.h"
#include "bbts/timer_util.h"

using std::string;
using bbts::message::AgentConfigure;
using boost::posix_time::hours;

// 默认的日志配置文件
#define LOG_CONF_DIR  "/conf"
#define LOG_CONF_FILE "log.conf"

namespace bbts {

extern bool get_user_conf_file(int argc, char* argv[], std::string *conf_file);

namespace agent {

static bool init_agent_running_path() {
    if (!Path::mkdir(g_agent_configure->working_dir(), 0755)) {
        FATAL_LOG("mkdir %s failed.", g_agent_configure->working_dir().c_str());
        return false;
    }

    if (chdir(g_agent_configure->working_dir().c_str()) != 0) {
        FATAL_LOG("chdir to working path %s failed", g_agent_configure->working_dir().c_str());
        return false;
    }

    if (!Path::mkdir(g_agent_configure->resume_dir(), 0755)) {
        FATAL_LOG("mkdir %s failed", g_agent_configure->resume_dir().c_str());
        return false;
    }

    // for download task stat log file
    mode_t mode = umask(0);
    int fd = open(g_agent_configure->task_stat_file().c_str(), O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (fd < 0) {
        FATAL_LOG("open %s failed: %d", g_agent_configure->task_stat_file().c_str(), errno);
        return false;
    }
    close(fd);
    chmod(g_agent_configure->task_stat_file().c_str(), 0666);

    fd = open(g_agent_configure->peer_stat_file().c_str(), O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (fd < 0) {
        FATAL_LOG("open %s failed: %d", g_agent_configure->peer_stat_file().c_str(), errno);
        return false;
    }
    close(fd);
    chmod(g_agent_configure->peer_stat_file().c_str(), 0666);

    fd = open(g_agent_configure->download_log_file().c_str(), O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (fd < 0) {
        FATAL_LOG("open %s failed: %d", g_agent_configure->download_log_file().c_str(), errno);
        return false;
    }
    close(fd);
    chmod(g_agent_configure->download_log_file().c_str(), 0666);
    umask(mode);
    return true;
}

static void handle_segv(int sig) {
    NOTICE_LOG("catch sigal %d!", sig);
    g_task_manager->stop();
}

static void handle_sigpipe(int sig) {
    NOTICE_LOG("catch sigal %d!", sig);
}

static void termsig_handler(int sig, siginfo_t * sig_info, void *) {
    NOTICE_LOG("receive terminal signal, sig_num=%d, sending_process_pid=%d, uid=%d, status=%d",
            sig, sig_info->si_pid, sig_info->si_uid, sig_info->si_status);

    time_t now_time = time(NULL);
    string cmdline = ProcessInfo::get_process_cmdline_by_pid(sig_info->si_pid);
    string exe = ProcessInfo::get_link_info_by_pid_and_type(sig_info->si_pid, "exe");
    string cwd = ProcessInfo::get_link_info_by_pid_and_type(sig_info->si_pid, "cwd");
    NOTICE_LOG("terminal time: %ld, process cmdline=%s, exe=%s, cwd=%s", 
            now_time, cmdline.c_str(), exe.c_str(), cwd.c_str());

    g_task_manager->stop();
}

static void set_signal_action() {
    struct sigaction sa;
    sa.sa_flags = SA_RESETHAND;
    sa.sa_handler = handle_segv;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    struct sigaction act;
    act.sa_sigaction = termsig_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGTERM, &act, NULL);

    struct sigaction sigpipe;
    sigpipe.sa_flags = 0;
    sigpipe.sa_handler = handle_sigpipe;
    sigemptyset(&sigpipe.sa_mask);
    sigaction(SIGPIPE, &sigpipe, NULL);
}

static void check_stat_file() {
    struct stat statbuf;
    if (::stat(g_agent_configure->task_stat_file().c_str(), &statbuf) == 0) {
        if (statbuf.st_size > 10 * 1024 * 1024) {
            if (truncate(g_agent_configure->task_stat_file().c_str(), 0) != 0) {
                WARNING_LOG("truncate file %s size to 0 failed: %d",
                        g_agent_configure->task_stat_file().c_str(), errno);
            }
        }
    } else {
        WARNING_LOG("can't stat file %s: %d", g_agent_configure->task_stat_file().c_str(), errno);
    }

    if (::stat(g_agent_configure->peer_stat_file().c_str(), &statbuf) == 0) {
        if (statbuf.st_size > 10 * 1024 * 1024) {
            if (truncate(g_agent_configure->peer_stat_file().c_str(), 0) != 0) {
                WARNING_LOG("truncate file %s size to 0 failed: %d",
                        g_agent_configure->peer_stat_file().c_str(), errno);
            }
        }
    } else {
        WARNING_LOG("can't stat file %s: %d", g_agent_configure->peer_stat_file().c_str(), errno);
    }
}

int bbts_agent(int argc, char* argv[]) {
    string conf_file(g_process_info->root_dir() + "/conf/agent.conf");
    string user_conf;
    if (get_user_conf_file(argc, argv, &user_conf)) {
        conf_file.assign(user_conf);
    }

    if (!load_pbconf(conf_file, g_agent_configure)) {
        return 1;
    }

    string log_conf_parent_dir = g_process_info->root_dir() + LOG_CONF_DIR;
    string log_conf_name = LOG_CONF_FILE;
    if (!g_agent_configure->log_conf().empty()) {
        if (!Path::slipt(Path::trim(Path::absolute(g_agent_configure->log_conf())), 
                    &log_conf_parent_dir, &log_conf_name)) {
            return 1;
        }
    }
    if (!load_log_by_configure(log_conf_parent_dir, log_conf_name)) {
        return 1;
    }
    
    NOTICE_LOG("bbts agent version: %s", GINGKO_VERSION);

    if (!init_agent_running_path()) {
        return 1;
    }

    boost::system::error_code ec;
    if (!File::lock(g_agent_configure->lock_file(), ec)) {
        FATAL_LOG("lock file %s failed: [%d]%s", g_agent_configure->lock_file().c_str(),
                ec.value(), ec.message().c_str());
        return 1;
    }

    set_signal_action();

    GOOGLE_PROTOBUF_VERIFY_VERSION;
    if (!g_task_manager->start()) {
        return 1;
    }

    boost::asio::deadline_timer timer(g_task_manager->get_io_service());
    timer_run_cycle("check stat file", timer, hours(1), boost::bind(&check_stat_file));
    g_task_manager->join();
    google::protobuf::ShutdownProtobufLibrary();
    CLOSE_LOG();
    return 0;
}

} // namespace detail
} // namespace bbts
