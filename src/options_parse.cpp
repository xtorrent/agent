/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   options_parse.cpp
 *
 * @author liuming03
 * @date   2015-1-17
 * @brief 
 */

#include "bbts/options_parse.h"

#include <grp.h>
#include <pwd.h>
#include <stdlib.h>

#include <map>
#include <vector>

#include <boost/assign.hpp>
#include <boost/system/error_code.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/torrent_info.hpp>

#include "bbts/log.h"
#include "bbts/string_util.h"

using std::make_pair;
using std::map;
using std::pair;
using std::string;
using std::stringstream;
using std::vector;

using boost::assign::list_of;
using boost::assign::map_list_of;
using boost::system::error_code;
using libtorrent::address;
using libtorrent::ip_filter;
using libtorrent::cluster_config_entry;

namespace bbts {

string get_cmd_string(int argc, char* argv[]) {
    string cmd;
    for (int i = 0; i < argc; ++i) {
        cmd.append(argv[i]).append(" ");
    }
    return cmd;
}

message::SourceURI convert(const cluster_config_entry &cluster_config) {
    message::SourceURI source_uri;
    source_uri.set_protocol(cluster_config.protocol);
    source_uri.set_host(cluster_config.host);
    source_uri.set_port(cluster_config.port);
    source_uri.set_user(cluster_config.user);
    source_uri.set_passwd(cluster_config.passwd);
    source_uri.set_path(cluster_config.prefix_path);
    return source_uri;
}

string to_string(const message::SourceURI &source_uri) {
    stringstream strm;
    strm << source_uri.protocol() << "://";
    if (!source_uri.user().empty() && !source_uri.passwd().empty()) {
        strm << source_uri.user() << ':' << source_uri.passwd() << '@';
    } else if (!source_uri.user().empty()) {
        strm << source_uri.user() << '@';
    }
    strm << source_uri.host();
    if (source_uri.port() != 0) {
        strm  << ':' << source_uri.port();
    }
    strm << source_uri.path();
    return strm.str();
}

bool parse_uri_entry(const string &uri, message::SourceURI *source_uri) {
    string::size_type pos_protocol;
    string::size_type pos_first_colon;
    string::size_type pos_at;
    string::size_type pos_second_colon;
    string::size_type pos_slash;
    pos_protocol = uri.find("://");
    if (pos_protocol == string::npos) {
        source_uri->set_protocol("file");
        source_uri->set_path(uri);
        return true;
    }
    source_uri->set_protocol(uri.substr(0, pos_protocol));
    pos_protocol += 3;
    pos_slash = uri.find('/', pos_protocol);
    if (pos_slash == string::npos) {
        return false;
    }
    source_uri->set_path(uri.substr(pos_slash));

    pos_at = uri.rfind('@', pos_slash);
    if (pos_at != string::npos) {
        pos_first_colon = uri.find(':', pos_protocol);
        if (pos_first_colon != string::npos && pos_first_colon < pos_at) {
            source_uri->set_user(uri.substr(pos_protocol, pos_first_colon - pos_protocol));
            source_uri->set_passwd(uri.substr(pos_first_colon + 1, pos_at - pos_first_colon - 1));
        } else {
            source_uri->set_user(uri.substr(pos_protocol, pos_at - pos_protocol));
        }
        pos_at += 1;
    } else {
        pos_at = pos_protocol;
    }

    pos_second_colon = uri.find(':', pos_at);
    if (pos_second_colon != string::npos && pos_second_colon < pos_slash) {
        source_uri->set_host(uri.substr(pos_at, pos_second_colon - pos_at));
        source_uri->set_port(
                atoi(uri.substr(pos_second_colon + 1, pos_slash - pos_second_colon - 1).c_str()));
    } else {
        source_uri->set_host(uri.substr(pos_at, pos_slash - pos_at));
    }
    DEBUG_LOG("cluster uri user: %s, passwd: %s, host: %s, port: %d, path: %s", 
            source_uri->user().c_str(), 
            source_uri->passwd().c_str(), 
            source_uri->host().c_str(), 
            source_uri->port(), 
            source_uri->path().c_str());
    return true;
}

static bool add_ip_filter_rule(const string &ip_range, int flags, ip_filter *filter) {
    vector<string> v;
    StringUtil::slipt(ip_range, "-", &v);
    size_t size = v.size();
    if (size == 0 || size > 2) {
        return false;
    }

    error_code ec;
    address start_address;
    start_address = address::from_string(v[0], ec);
    if (ec) {
        return false;
    }

    address last_address;
    if (size == 2) {
        last_address = address::from_string(v[1], ec);
        if (ec) {
            return false;
        }
    } else {
        last_address = start_address;
    }
    filter->add_rule(start_address, last_address, flags);
    return true;
}

bool parse_ip_filter(const string &filter_string, int flags, ip_filter *filter) {
    vector<string> ip_range_vector;
    StringUtil::slipt(filter_string, ",", &ip_range_vector);
    for (vector<string>::iterator it = ip_range_vector.begin(); it != ip_range_vector.end(); ++it) {
        if (!add_ip_filter_rule(*it, flags, filter)) {
            return false;
        }
    }
    return true;
}

bool parse_subnet_mask(const string &ip, const string &mask, ip_filter *filter) {
    using boost::asio::ip::address;
    using boost::system::error_code;
    error_code ec;
    address ip_address = address::from_string(ip, ec);
    if (ec) {
        return false;
    }
    address mask_address = address::from_string(mask, ec);
    if (ec) {
        return false;
    }

    unsigned mask_value = static_cast<unsigned>(mask_address.to_v4().to_ulong());
    int i = 31;
    for (; i >= 0; --i) {
        if (((mask_value >> i) & 0x01) == 0) {
            break;
        }
    }

    ++i;
    unsigned long ip_value = ip_address.to_v4().to_ulong();
    unsigned start = static_cast<unsigned>((ip_value >> i) << i);
    unsigned end = static_cast<unsigned>(ip_value | ((1UL << i) - 1));
    unsigned char *p = (unsigned char *)(&start);
    unsigned char *q = (unsigned char *)(&end);
    char buf[32];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u-%u.%u.%u.%u",
            p[3], p[2], p[1], p[0], q[3], q[2], q[1], q[0]);
    if (!add_ip_filter_rule(buf, 0, filter)) {
        return false;
    }
    return true;
}

pair<int, int> parse_port_range(const string &port_range) {
    int start = 0;
    int end = 0;
    string::size_type pos = port_range.find_first_of(",; \t");
    if (pos != string::npos) {
        start = atoi(port_range.substr(0, pos).c_str());
        end = atoi(port_range.substr(pos + 1).c_str());
    } else {
        start = atoi(port_range.c_str());
        end = start + 1000;
        if (end > 65535) {
            end = 65535;
        }
    }
    return make_pair(start, end);
}

void parse_tracker(const string &str, message::Server *tracker) {
    vector<string> v;
    StringUtil::slipt(str, ":", &v);
    if (v.size() != 2) {
        tracker->set_host(str);
        return;
    }
    tracker->set_host(v[0]);
    tracker->set_port(atoi(v[1].c_str()));
}

/*********************************** attributes **************************************************/

static map<string, int> attr_name_map = map_list_of<string, int>("taskid", 0)("status", 1)
        ("progress", 2)("infohash", 3)("user", 4)("group", 5)("cmd", 6)("error", 7)("download", 8)
        ("upload", 9)("downrate", 10)("uprate", 11)("peers", 12)("seeds", 13)("savepath", 14);

static vector<string> attr_set = list_of("taskid")("status")("progress")("infohash")("user")("cmd");

void parse_attributes(const string &attr_str) {
    attr_set.clear();
    vector<string> v;
    StringUtil::slipt(attr_str, ",;: \t\n", &v);
    for (vector<string>::iterator i = v.begin(); i != v.end(); ++i) {
        if (attr_name_map.find(*i) == attr_name_map.end()) {
            continue;
        }
        bool finded = false;
        for (vector<string>::iterator it = attr_set.begin(); it != attr_set.end(); ++it) {
            if (*it == *i) {
                finded = true;
                break;
            }
        }
        if (!finded) {
            attr_set.push_back(*i);
        }
    }
}

void print_task_status_header() {
    vector<string>::const_iterator it;
    for (it = attr_set.begin(); it != attr_set.end(); ++it) {
        switch (attr_name_map[*it]) {
        case 0:  //taskid
            fprintf(stdout, "%12s ", it->c_str());
            break;

        case 1:  //status
            fprintf(stdout, "%8s ", it->c_str());
            break;

        case 2:  //progress
            fprintf(stdout, "%8s ", it->c_str());
            break;

        case 3:  //infohash
            fprintf(stdout, "%-40s ", it->c_str());
            break;

        case 4:  //user
            fprintf(stdout, "%-20s ", it->c_str());
            break;

        case 5:  //group
            fprintf(stdout, "%-20s ", it->c_str());
            break;

        case 6:  //cmd
            fprintf(stdout, "%-10s ", it->c_str());
            break;

        case 7:  //error
            fprintf(stdout, "%-5s ", it->c_str());
            break;

        case 8:  //download
            fprintf(stdout, "%11s ", it->c_str());
            break;

        case 9:  //upload
            fprintf(stdout, "%11s ", it->c_str());
            break;

        case 10:  //downrate
            fprintf(stdout, "%11s ", it->c_str());
            break;

        case 11:  //uprate
            fprintf(stdout, "%11s ", it->c_str());
            break;

        case 12:  //peers
            fprintf(stdout, "%5s ", it->c_str());
            break;

        case 13:  //seeds
            fprintf(stdout, "%5s ", it->c_str());
            break;

        case 14:  //save_path
            fprintf(stdout, "%-10s ", it->c_str());
            break;

        default:
            break;
        }
    }
    fprintf(stdout, "\n");
}

void print_task_status(const message::TaskStatus &task_status) {
    /* 获取passwd字段的缓存大小 */
    const int PASSWD_BUF_SIZE = 16384;
    const message::Task &task_info = task_status.task();
    vector<string>::const_iterator it;
    for (it = attr_set.begin(); it != attr_set.end(); ++it) {
        switch (attr_name_map[*it]) {
        case 0:  //taskid
            fprintf(stdout, "%12ld ", task_info.taskid());
            break;

        case 1:  //status
            fprintf(stdout, "%8s ",
                    message::TaskStatus::status_t_Name(task_status.status()).c_str());
            break;

        case 2:  //progress
            fprintf(stdout, "%7.3f%% ", task_status.progress() / 10000.0f);
            break;

        case 3:  //infohash
            fprintf(stdout, "%-40s ", task_info.infohash().c_str());
            break;

        case 4: {  //user
            struct passwd pwd;
            struct passwd *result = NULL;
            char buf[PASSWD_BUF_SIZE] = { 0 };
            int ret = getpwuid_r(task_info.uid(), &pwd, buf, sizeof(buf), &result);
            if (ret == 0 && result != NULL) {
                fprintf(stdout, "%-20s ", pwd.pw_name);
            } else {
                fprintf(stdout, "%-20s ", "unknow");
            }
            break;
        }

        case 5: {  //group
            struct group grp;
            struct group *result = NULL;
            char buf[PASSWD_BUF_SIZE] = { 0 };
            int ret = getgrgid_r(task_info.gid(), &grp, buf, sizeof(buf), &result);
            if (ret == 0 && result != NULL) {
                fprintf(stdout, "%-20s ", grp.gr_name);
            } else {
                fprintf(stdout, "%-20s ", "unknow");
            }
            break;
        }

        case 6:  //cmd
            fprintf(stdout, "%-10s ", task_info.cmd().c_str());
            break;

        case 7:  //error
            if (task_status.error().empty()) {
                fprintf(stdout, "%-5s ", "N/A");
            } else {
                fprintf(stdout, "%-5s ", task_status.error().c_str());
            }
            break;

        case 8:  //download
            fprintf(stdout, "%11s ",
                    StringUtil::bytes_to_readable(task_status.total_download()).c_str());
            break;

        case 9:  //upload
            fprintf(stdout, "%11s ",
                    StringUtil::bytes_to_readable(task_status.total_upload()).c_str());
            break;

        case 10:  //downrate
            fprintf(stdout, "%9s/s ",
                    StringUtil::bytes_to_readable(task_status.download_rate()).c_str());
            break;

        case 11:  //uprate
            fprintf(stdout, "%9s/s ",
                    StringUtil::bytes_to_readable(task_status.upload_rate()).c_str());
            break;

        case 12:  //peers
            fprintf(stdout, "%5d ", task_status.num_peers());
            break;

        case 13:  //seeds
            fprintf(stdout, "%5d ", task_status.num_seeds());
            break;

        case 14:  //save_path
            fprintf(stdout, "%-10s ", (task_info.save_path() + '/' + task_info.new_name()).c_str());
            break;

        default:
            break;
        }
    }
    fprintf(stdout, "\n");
}

void print_status(const message::Status &status, bool debug) {
    fprintf(stdout, "               upload rate: %s/s\n",
            StringUtil::bytes_to_readable(status.upload_rate()).c_str());
    fprintf(stdout, "       payload upload rate: %s/s\n",
            StringUtil::bytes_to_readable(status.payload_upload_rate()).c_str());
    fprintf(stdout, "              total upload: %s\n",
            StringUtil::bytes_to_readable(status.total_upload()).c_str());
    fprintf(stdout, "      total payload upload: %s\n",
            StringUtil::bytes_to_readable(status.total_payload_upload()).c_str());
    fprintf(stdout, "                 num peers: %d\n", status.num_peers());
    fprintf(stdout, "             peerlist size: %d\n", status.peerlist_size());
    if (debug) {
        fprintf(stdout, "             download rate: %s/s\n",
                StringUtil::bytes_to_readable(status.download_rate()).c_str());
        fprintf(stdout, "     payload download rate: %s/s\n",
                StringUtil::bytes_to_readable(status.payload_download_rate()).c_str());
        fprintf(stdout, "            total download: %s\n",
                StringUtil::bytes_to_readable(status.total_download()).c_str());
        fprintf(stdout, "    total payload download: %s\n",
                StringUtil::bytes_to_readable(status.total_payload_download()).c_str());
        fprintf(stdout, "         up bandwith queue: %d\n", status.up_bandwidth_queue());
        fprintf(stdout, "   up bandwith bytes queue: %s\n",
                StringUtil::bytes_to_readable(status.up_bandwidth_bytes_queue()).c_str());
        fprintf(stdout, "      down bandwidth queue: %d\n", status.down_bandwidth_queue());
        fprintf(stdout, "down bandwidth bytes queue: %s\n",
                StringUtil::bytes_to_readable(status.down_bandwidth_bytes_queue()).c_str());
        fprintf(stdout, "           disk read queue: %d\n", status.disk_read_queue());
        fprintf(stdout, "          disk write queue: %d\n", status.disk_write_queue());
        fprintf(stdout, "              num unchoked: %d\n", status.num_unchoked());
    }
}

/************************************** check configures ******************************************/

bool check_cluster_uri(const std::string &uri) {
    message::SourceURI cluster_uri;
    if (!parse_uri_entry(uri, &cluster_uri)) {
        FATAL_LOG("can't parse cluster uri: %s", uri.c_str());
        return false;
    }
    if (cluster_uri.protocol() != "hdfs" && cluster_uri.protocol() != "nfs") {
        FATAL_LOG("protocol %s not support, only nfs and hdfs", cluster_uri.protocol().c_str());
        return false;
    }
    if (cluster_uri.protocol() == "nfs" && cluster_uri.path().find(":/") == string::npos) {
        FATAL_LOG("nfs format: %s", "protocol://user:passwd@host:port/mount_point:/path");
        return false;
    }
    if (cluster_uri.user().empty() || cluster_uri.passwd().empty() || cluster_uri.host().empty()
            || cluster_uri.port() == 0 || cluster_uri.path().empty()) {
        FATAL_LOG("cluster format not correct: protocol://user:passwd@host:port/path");
        return false;
    }
    return true;
}

bool check_download_configure(const message::DownloadConfigure &configure) {
    TRACE_LOG("%s", configure.cmd().c_str());

    if (configure.save_path().empty() && configure.muti_save_paths_size() == 0) {
        FATAL_LOG("no save path or muti save paths");
        return false;
    }

    if (!configure.infohash().empty() && configure.infohash().length() != 40) {
        FATAL_LOG("infohash[sha1 hex] invalid: %s", configure.infohash().c_str());
        return false;
    }

    if (configure.seeding_time() < -2) {
        FATAL_LOG("seeding time[>=-1] invalid: %d", configure.seeding_time());
        return false;
    }

    if (configure.download_limit() < 0 || configure.download_limit() > 500 * 1024 * 1024) {
        FATAL_LOG("download limit[0-500MB/s] invalid: %d", configure.download_limit());
        return false;
    }

    if (configure.upload_limit() < 0 || configure.upload_limit() > 500 * 1024 * 1024) {
        FATAL_LOG("upload limit[0-500MB/s] invalid: %d", configure.upload_limit());
        return false;
    }

    if (configure.dynamic_allocate_limit() < 0
            || configure.dynamic_allocate_limit() > 1000 * 1024 * 1024) {
        FATAL_LOG("upload limit[0-1000MB/s] invalid: %d", configure.dynamic_allocate_limit());
        return false;
    }

    if (configure.connection_limit() < 0 || configure.connection_limit() > 10000) {
        FATAL_LOG("connections limit[0-10000] invalid: %d", configure.connection_limit());
        return false;
    }

    if (configure.progress_interval() < 1) {
        FATAL_LOG("progress interval[>=1] invalid: %d", configure.progress_interval());
        return false;
    }

    if (configure.hang_timeout() < 0) {
        FATAL_LOG("hang check timeout[>=0] invalid: %d", configure.hang_timeout());
        return false;
    }

    if (configure.send_socket_buffer_size() < 0 || configure.send_socket_buffer_size() > 4194304) {
        FATAL_LOG("send buffer size[0-4MB] invalid: %d", configure.send_socket_buffer_size());
        return false;
    }

    if (configure.recv_socket_buffer_size() < 0 || configure.recv_socket_buffer_size() > 4194304) {
        FATAL_LOG("recv buffer size[0-4MB] invalid: %d", configure.recv_socket_buffer_size());
        return false;
    }

    if (configure.cluster_thread_num() < 0 || configure.cluster_thread_num() > 32) {
        FATAL_LOG("hdfs thread num[0-32] invalid: %d", configure.cluster_thread_num());
        return false;
    }

    if (configure.peers_num_want() < 0 || configure.peers_num_want() > 10000) {
        FATAL_LOG("peers num want[0-10000] invalid: %d", configure.peers_num_want());
        return false;
    }

    if (configure.max_announce_interval() < 10 || configure.max_announce_interval() > 3600) {
        FATAL_LOG("max announce interval[10-3600] invalid: %d", configure.max_announce_interval());
        return false;
    }

    if (configure.dynamic_allocate() < -1 || configure.dynamic_allocate() > 300) {
        FATAL_LOG("dynamic allocate[-1-300] invalid: %d", configure.dynamic_allocate());
        return false;
    }

    if (configure.mem_limit() != 0 && configure.mem_limit() < 50) {
        FATAL_LOG("memery limit[>=50M] invalid: %d", configure.mem_limit());
        return false;
    }

    if (configure.write_cache_line_size() < 0 || configure.write_cache_line_size() > 1024) {
        FATAL_LOG("write cache line size[0-1024] invalid: %d", configure.write_cache_line_size());
        return false;
    }

    if (configure.read_cache_line_size() < 0 || configure.read_cache_line_size() > 1024) {
        FATAL_LOG("read cache line size[0-1024] invalid: %d", configure.read_cache_line_size());
        return false;
    }

    if (configure.timeout() < -1) {
        FATAL_LOG("timeout[>=-1] invalid: %d", configure.timeout());
        return false;
    }

    if (configure.max_hdfs_cache_pieces() < 1) {
        FATAL_LOG("max hdfs cache pieces[>=1] invalid: %d", configure.max_hdfs_cache_pieces());
        return false;
    }

    if (configure.cache_size() < 0) {
        FATAL_LOG("cache pieces[>=0] invalid: %d", configure.cache_size());
        return false;
    }

    if (configure.listen_port_start() < 1025 || configure.listen_port_start() > 65535
            || configure.listen_port_end() < 1025 || configure.listen_port_end() > 65535) {
        FATAL_LOG("listen port range[1025-65535] invalid: %d-%d",
                configure.listen_port_start(), configure.listen_port_end());
        return false;
    }

    if (configure.disable_hash_check() && configure.enable_dynamic_hash_check()) {
        FATAL_LOG("disbale-hash-check and enable-dynamic-hash-check can't use at the same time");
        return false;
    }

    if (!configure.cluster_uri().empty() && !check_cluster_uri(configure.cluster_uri())) {
        return false;
    }
    return true;
}

} // namespace bbts
