/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   path.cpp
 *
 * @author liuming03
 * @date   2013-4-9
 */

#include "bbts/path.h"

#include <assert.h>
#include <errno.h>
#include <ftw.h>
#include <sys/stat.h>

#include <string>
#include <vector>

#include <boost/thread/tss.hpp>

#include "bbts/log.h"

using std::string;

namespace bbts {

string Path::trim_back_slash(const string &path) {
    string::size_type pos = path.length() - 1;
    while (pos != string::npos && path[pos] == '/') {
        --pos;
    }
    return path.substr(0, pos + 1);
}

string Path::trim_front_slash(const string &path) {
    string::size_type pos = 0;
    while (pos < path.length() && path[pos] == '/') {
        ++pos;
    }
    return path.substr(pos);
}

bool Path::slipt(const string &path, string *parent_dir, string *name) {
    assert(parent_dir && name);
    string tmp_path = trim_back_slash(path);
    string::size_type pos = tmp_path.rfind('/');
    if (pos != string::npos) {
        *parent_dir = 0 == pos ? "/" : tmp_path.substr(0, pos);
        name->assign(tmp_path.substr(pos + 1));
        return true;
    }
    return false;
}

string Path::parent_dir(const string &path) {
    string tmp_path = trim_back_slash(path);
    string::size_type pos = tmp_path.rfind('/');
    if (pos != string::npos) {
        return 0 == pos ? "/" : tmp_path.substr(0, pos);
    }
    return string();
}

string Path::short_name(const string &path) {
    string tmp_path = trim_back_slash(path);
    string::size_type pos = tmp_path.rfind('/');
    if (pos != string::npos) {
        return tmp_path.substr(pos + 1);
    }
    return tmp_path;
}

string Path::subpath(const string &path) {
    string tmp_path = trim_front_slash(path);
    string::size_type pos = tmp_path.find('/');
    if (pos != string::npos) {
        return tmp_path.substr(pos + 1);
    }
    return string();
}

string Path::current_working_dir() {
    char path[MAX_PATH_LEN] = { 0 };
    char *ptr = ::getcwd(path, MAX_PATH_LEN);
    if (!ptr) {
        FATAL_LOG("get current working dir failed: %s", strerror(errno));
        exit(11);
    }
    return string(ptr);
}

string Path::home_dir() {
    return string(getenv("HOME"));
}

string Path::absolute(const string &path) {
    string full_path = path;
    if (path.empty() || path[0] != '/') {
        full_path = current_working_dir() + '/' + full_path;
    }
    return full_path;
}

string Path::regex_replace(const string &path, const string &chars) {
    string tmp = path;
    string::size_type pos = tmp.find_first_of(chars);
    while (string::npos != pos) {
        tmp.replace(pos, 1, "\\" + tmp.substr(pos, 1));
        pos += 2;
        pos = tmp.find_first_of(chars, pos); 
    }
    return tmp;
}

bool Path::is_absolute(const string &path) {
    if (path.empty()) {
        return false;
    }
    return path[0] == '/';
}

string Path::trim(const string &path) {
    if (path.empty()) {
        return path;
    }
    std::vector<string> v;
    if (path[0] == '/') {
        v.push_back(string("/"));
    }
    string tmp;
    string::size_type start = 0;
    string::size_type pos;
    do {
        pos = path.find('/', start);
        tmp = pos == string::npos ? path.substr(start) : path.substr(start, pos - start);
        if (tmp.empty() || tmp == ".") {
        } else if (tmp == "..") {
            if (v.size() <= 1) {
                return string();
            }
            v.pop_back();
        } else {
            v.push_back(tmp);
        }
        start = pos + 1;
    } while (pos != string::npos);
    if (v.empty()) {
        v.push_back("./");
    }
    tmp = v[0] == "/" ? "" : v[0];
    for (size_t i = 1; i != v.size(); ++i) {
        tmp.append("/").append(v[i]);
    }
    return tmp;
}

bool Path::mkdir(const string &dir, mode_t mode) {
    struct stat statbuf;
    if (stat(dir.c_str(), &statbuf) != 0) {  // 没有该路径则创建
        if (::mkdir(dir.c_str(), mode) != 0) {
            WARNING_LOG("mkdir %s failed: %s(%d)", dir.c_str(), strerror(errno), errno);
            return false;
        }
        return true;
    }

    if (statbuf.st_mode & S_IFDIR) {
        return true;
    }
    WARNING_LOG("mkdir %s failed: not a dir", dir.c_str());
    return false;
}

bool Path::mkdirs(const string &path, mode_t mode) {
    string parent = parent_dir(path);
    if (!parent.empty()) {
        struct stat statbuf;
        if (stat(parent.c_str(), &statbuf) != 0) {
            if (!mkdirs(parent, mode)) {
                return false;
            }
        } else if (!(statbuf.st_mode & S_IFDIR)) {
            return false;
        }
    }
    return mkdir(path, mode);
}

bool Path::is_dir(const string &path) {
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) != 0) {
        return false;
    }
    return statbuf.st_mode & S_IFDIR;
}

bool Path::is_file(const string &path) {
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) != 0) {
        return false;
    }
    return statbuf.st_mode & S_IFREG;
}

bool Path::exist(const string &path) {
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) != 0) {
        return false;
    }
    return true;
}

namespace detail {

boost::thread_specific_ptr<Path::NftwCallback> g_user_nftw_cb;

int nftw_cb(const char* path, const struct stat* statbuf, int type, struct FTW* ftw_info) {
    Path::NftwCallback *user_cb = detail::g_user_nftw_cb.get();
    return (*user_cb)(path, statbuf, type, ftw_info);
}
}

bool Path::nftw(const std::string &dir, NftwCallback cb, int depth, int flag) {
    NftwCallback *user_cb = detail::g_user_nftw_cb.get();
    if (user_cb == NULL) {
        user_cb = new NftwCallback(cb);
        detail::g_user_nftw_cb.reset(user_cb);
    } else {
        *user_cb = cb;
    }
    if (::nftw(dir.c_str(), detail::nftw_cb, depth, flag) != 0) {
        WARNING_LOG("nftw %s failed: %d", dir.c_str(), errno);
        return false;
    }
    return true;
}

}  // namespace bbts
