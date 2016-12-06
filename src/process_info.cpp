/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   process_info.cpp
 *
 * @author liuming03
 * @date   2014年10月21日
 * @brief 
 */
#include "bbts/process_info.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#include <sys/time.h>

#include <sstream>

#include "bbts/lazy_singleton.hpp"
#include "bbts/log.h"
#include "bbts/path.h"

using std::string;

namespace bbts {

namespace detail {

string get_program_path() {
    char program[Path::MAX_PATH_LEN] = { 0 };
    if (readlink("/proc/self/exe", program, sizeof(program)) < 0) {
        return string();
    }
    return string(program);
}

bool get_ip_addr_by_hostname(string hostname, struct in_addr* addr) {
    int h_err = 0;
    char buf[8096] = {0};
    struct hostent ent;
    struct hostent *ptr = NULL;
    int ret = gethostbyname_r(hostname.c_str(), &ent, buf, sizeof(buf), &ptr, &h_err);
    if (ret != 0 || h_err != 0) {
        WARNING_LOG("get host by name(%s) failed: %s", hostname.c_str(), hstrerror(h_err));
        return false;
    }
    *addr = *(struct in_addr *)(ent.h_addr);
    return true;
}

}

ProcessInfo *g_process_info = LazySingleton<ProcessInfo>::instance();

ProcessInfo::ProcessInfo() {
    // notice critical area for not threaded safety
    char buffer[256] = {0};
    if (gethostname(buffer, sizeof(buffer)) == 0) {
        _hostname = buffer;
        _machine_room = get_machine_room(_hostname, false);
        _machine_room_without_digit = get_machine_room_without_digit(_machine_room);
    }

    detail::get_ip_addr_by_hostname(_hostname, &_in_addr);
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &_in_addr, ip_str, INET_ADDRSTRLEN);
    _ip.assign(ip_str);

    _uid = getuid();
    _pid = getpid();
    _program = detail::get_program_path();
    _root_dir = Path::parent_dir(Path::parent_dir(_program));
}

string ProcessInfo::get_machine_room(const string &name, bool is_hdfs) {
    string machine_room;
    string hostname = name;
    if (hostname.empty()) {
        return machine_room;
    }

    string::size_type index = hostname.rfind(".baidu.com");
    if (index != string::npos) {
        hostname.erase(index);
    }

    index = hostname.rfind(".");
    if (index == string::npos) {
        return machine_room;
    }

    machine_room.assign(hostname, index + 1, hostname.length() - index - 1);
    if (machine_room != "vm" && !is_hdfs) {
        return machine_room;
    }

    machine_room.clear();
    index = hostname.find("-");
    if (index != string::npos) {
        machine_room.assign(hostname, 0, index);
    }
    return machine_room;
}

string ProcessInfo::get_machine_room_without_digit(const string &machine_room) {
    string machine_room_without_digit = machine_room;
    string::size_type index;
    for (index = machine_room_without_digit.size() - 1; index != string::npos; --index) {
        char &letter = machine_room_without_digit.at(index);
        if (letter < '0' || letter > '9') {
            break;
        }
    }
    ++index;
    if (index < machine_room_without_digit.size() - 1) {
        machine_room_without_digit.erase(index);
    }
    return machine_room_without_digit;
}

pid_t ProcessInfo::thread_id() const {
    pid_t tid = -1;
#if defined(__linux__)
    tid = syscall(SYS_gettid);
#endif
    return tid;
}

int ProcessInfo::random() const {
    unsigned *seed = _rand_seed.get();
    if (seed == NULL) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        unsigned init_seed_value = _in_addr.s_addr ^ _pid ^ thread_id() ^ tv.tv_sec ^ tv.tv_usec;
        seed = new unsigned(init_seed_value);
        _rand_seed.reset(seed);
    }
    return rand_r(seed);
}

bool ProcessInfo::set_evn(const string &key, const string &value) const {
    if (setenv(key.c_str(), value.c_str(), 1) != 0) {
        WARNING_LOG("setevn %s=%s failed.", key.c_str(), value.c_str());
        return false;
    }
    DEBUG_LOG("setevn %s=%s", key.c_str(), value.c_str());
    return true;
}

string ProcessInfo::get_evn(const string &key) const {
    return string(getenv(key.c_str()));
}

string ProcessInfo::get_link_info_by_pid_and_type(pid_t pid, const string &type) {
    std::stringstream link_info_file;
    link_info_file << "/proc/" << pid << "/" << type;

    char buf[2048];
    int n = 0;
    string link_info = "";
    n = readlink(link_info_file.str().c_str(), buf, sizeof(buf));
    if (n >= 0 and n < 2048) {
        buf[n] = '\0';
        link_info = string(buf);
    } else {
        WARNING_LOG("fail to readlink for file %s, error_info: %s\n",
                link_info_file.str().c_str(), strerror(errno));
    }
    return link_info;
}

string ProcessInfo::get_process_cmdline_by_pid(pid_t pid) {
    std::stringstream filename;
    filename << "/proc/" << pid << "/cmdline";
    
    string process_name = "";
    
    FILE *fp = fopen(filename.str().c_str(), "rb");
    if (fp == NULL) {
        WARNING_LOG("open %s error", filename.str().c_str());
        return process_name;
    }
    char ch = '\0';
    while ((ch = fgetc(fp)) != EOF) {
        if (ch == '\0') {
            ch = ' ';
        }
        process_name += ch;
    }
    fclose(fp);
    return process_name;
}

}  // namespace bbts
