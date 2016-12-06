/****************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   gko3.cpp
 *
 * @author liuming03
 * @date   2013-2-9
 */

#include <getopt.h>

#include <boost/assign.hpp>

#include "bbts/file.h"
#include "bbts/lazy_singleton.hpp"
#include "bbts/options_parse.h"
#include "bbts/path.h"
#include "bbts/pbconf.hpp"
#include "bbts/process_info.h"
#include "bbts/string_util.h"
#include "bbts/tool/bbts_client.h"
#include "bbts/torrent_file_util.h"
#include "bbts/tool/downloader.h"
#include "configure.pb.h"

using std::make_pair;
using std::map;
using std::pair;
using std::string;
using std::vector;

using boost::assign::map_list_of;
using boost::bind;
using boost::system::error_code;
using bbts::message::DownloadConfigure;
using bbts::LazySingleton;
using bbts::tool::Downloader;
using bbts::tool::BBTSClient;

namespace bbts {

extern int print_help(int argc, char *argv[]);
extern int print_version(int argc, char *argv[]);

namespace agent {
extern int bbts_agent(int argc, char* argv[]);
}

namespace group {
extern int bbts_group(int argc, char* argv[]);
}

bool get_user_conf_file(int argc, char* argv[], std::string *conf_file) {
    if (conf_file == NULL) {
        return false;
    }

    conf_file->clear();
    for (int i = 0; i < argc; ++i) {
        if (strncmp(argv[i], "--user-conf", strlen("--user-conf")) == 0) {
            if (i + 1 == argc) {
                fprintf(stderr, "use --user-conf, but conf file is empty!\n");
                return false;
            }
            conf_file->assign(Path::trim(Path::absolute(argv[i + 1])));
            return true;
        }
    }

    return false;
}

static int process_mkseed(int argc, char* argv[]) {
    int option = -1;
    int index = -1;
    const struct option long_opt[] = {
            { "torrent", required_argument, NULL, 'r' },
            { "cluster", required_argument, NULL, 'C' },
            { "webseed", required_argument, NULL, 'W' },
            { "path", required_argument, NULL, 'p' },
            { "size", required_argument, NULL, 's' },
            { "link", no_argument, NULL, 'l' },
            { "hash", no_argument, NULL, 'H' },
            { "besthash", no_argument, NULL, 1 },
            { "include-files", required_argument, NULL, 2 },
            { "exclude-files", required_argument, NULL, 3 },
            { "no-piece-hash", no_argument, NULL, 4 },
            { "creator", required_argument, NULL, 5 },
            { "comment", required_argument, NULL, 6 },
            { "upload", no_argument, NULL, 7 },
            { NULL, no_argument, NULL, 0 }
    };

    string torrent_file;
    MakeTorrentArgs make_torrent_args;
    while ((option = getopt_long(argc - 1, &argv[1], "r:C:W:p:s:lH", long_opt, &index)) != -1) {
        switch (option) {
        case 'r':
            torrent_file = Path::trim(Path::absolute(optarg));
            break;

        case 'C':
            make_torrent_args.cluster_config = optarg;
            break;

        case 'W':
            make_torrent_args.web_seeds.push_back(optarg);
            break;

        case 'p':
            make_torrent_args.path = Path::trim(Path::absolute(optarg));
            break;

        case 's':
            make_torrent_args.piece_size = atoi(optarg) * 1024;
            break;

        case 'l':
            make_torrent_args.flags |= MakeTorrentArgs::SYMLINKS;
            break;

        case 'H':
            make_torrent_args.flags |= MakeTorrentArgs::FILE_HASH;
            break;

        case 1:
            make_torrent_args.flags |= MakeTorrentArgs::BEST_HASH;
            break;

        case 2:
            make_torrent_args.include_regex.push_back(optarg);
            break;

        case 3:
            make_torrent_args.exclude_regex.push_back(optarg);
            break;

        case 4:
            make_torrent_args.flags &= ~MakeTorrentArgs::PIECE_HASH;
            break;

        case 5:
            make_torrent_args.creator = optarg;
            break;

        case 6:
            make_torrent_args.comment = optarg;
            break;

        case 7:
            make_torrent_args.flags |= MakeTorrentArgs::UPLOAD_TORRENT;
            break;

        case ':':
        case '?':
        default:
            return 1;
        }
    }


    string infohash;
    vector<char> torrent;
    string error_message;
    if (!make_torrent(make_torrent_args, &infohash, &torrent, &error_message)) {
        return 2;
    }

    if (torrent_file.empty()) {
        if (make_torrent_args.path.find('*') == string::npos) {
            torrent_file = Path::absolute(Path::short_name(make_torrent_args.path) + ".torrent");
        } else {
            torrent_file = Path::absolute(
                    Path::short_name(Path::parent_dir(make_torrent_args.path)) + ".torrent");
        }
    }
    error_code ec;
    File::write(torrent_file, torrent, ec);
    if (ec) {
        fprintf(stderr, "write %s failed: %s.\n", torrent_file.c_str(), ec.message().c_str());
        return 3;
    }
    fprintf(stdout, "%s\n", torrent_file.c_str());
    fprintf(stdout, "%s\n", infohash.c_str());
    fprintf(stdout, "infohash:%s\n", infohash.c_str());
    return 0;
}

static int process_dump(int argc, char* argv[]) {
    int option = -1;
    int index = -1;
    const struct option long_opt[] = {
            { "torrent", required_argument, NULL, 'r' },
            { "debug", no_argument, NULL,  1 },
            { NULL, no_argument, NULL,  0 }
    };

    string torrent_file;
    bool debug = false;
    while ((option = getopt_long(argc - 1, &argv[1], "r:", long_opt, &index)) != -1) {
        switch (option) {
        case 'r':
            torrent_file = Path::trim(Path::absolute(optarg));
            break;

        case 1:
            debug = true;
            break;

        case ':':
        case '?':
        default:
            return 1;
        }
    }

    if (!dump_torrent(torrent_file, debug)) {
        return 1;
    }
    return 0;
}

static int process_add(int argc, char* argv[]) {
    DownloadConfigure *configure = LazySingleton<DownloadConfigure>::instance();
    message::AddTask add_task_params;
    message::Task *task_info = add_task_params.mutable_task();
    task_info->set_type(message::NOCHECK_TASK);
    task_info->set_cmd(get_cmd_string(argc, argv));
    task_info->set_product_tag(configure->product_tag());
    message::TaskOptions *options = add_task_params.mutable_options();
    options->set_upload_limit(configure->upload_limit());
    options->set_max_connections(configure->connection_limit());

    int option = -1;
    int index = -1;
    const struct option long_opt[] = {
            { "torrent", required_argument, NULL, 'r' },
            { "infohash", required_argument, NULL, 'i' },
            { "path", required_argument, NULL, 'p' },
            { "fullpath", required_argument, NULL, 'n' },
            { "seedtime", required_argument, NULL, 'S' },
            { "uplimit", required_argument, NULL, 'u' },
            { "connlimit", required_argument, NULL, 'c' },
            { "cluster", required_argument, NULL, 'C' },
            { "seed", no_argument, NULL, 1 },
            { "nocheck", no_argument, NULL, 2 },
            { "tracker", required_argument, NULL, 3 },
            { "transfer", no_argument, NULL, 4 },
            { "product", required_argument, NULL, 5 },
            { "source", required_argument, NULL, 6 },
            { "classpath", required_argument, NULL, 7 },
            { NULL, no_argument, NULL, 0 }
    };

    while ((option = getopt_long(argc - 1, &argv[1], "r:i:p:n:S:u:c:C:", long_opt, &index)) != -1) {
        switch (option) {
        case 'r':
            task_info->set_torrent_path(optarg);
            break;

        case 'i':
            task_info->set_infohash(optarg);
            break;

        case 'p':
            if (!task_info->has_new_name()) {
                task_info->set_save_path(Path::trim(Path::absolute(optarg)));
            }
            break;

        case 'n': {
            string save_path;
            string new_name;
            Path::slipt(Path::trim(Path::absolute(optarg)), &save_path, &new_name);
            task_info->set_save_path(save_path);
            task_info->set_new_name(new_name);
            break;
        }

        case 'S':
            task_info->set_seeding_time(atoi(optarg));
            break;

        case 'u':
            options->set_upload_limit(StringUtil::readable_to_bytes(optarg));
            break;

        case 'c':
            options->set_max_connections(atoi(optarg));
            break;

        case 'C':
            task_info->set_uri(optarg);
            break;

        case 1:
            task_info->set_type(message::SEEDING_TASK);
            break;

        case 2:
            task_info->set_type(message::NOCHECK_TASK);
            break;

        case 3: {
            message::Server *tracker = task_info->add_trackers();
            parse_tracker(optarg, tracker);
            break;
        }

        case 4: 
            task_info->set_type(message::TRANSFER_TASK);
            break;

        case 5:
            task_info->set_product_tag(optarg);
            break;

        case 6: {
            message::SourceURI source_uri;
            if (!parse_uri_entry(optarg, &source_uri)) {
                fprintf(stderr, "parse source uri failed: %s\n", optarg);
                return 1;
            }
            task_info->set_uri(to_string(source_uri));
            break;
        }

        case 7:
            configure->set_class_path(optarg);
            break;

        case ':':
        case '?':
        default:
            return 1;
        }
    }

    // 如果-r 指定了地址，则在这里读取内容，然后设置data，将path release
    if (task_info->has_torrent_path()) {
        vector<char> buffer;
        error_code ec;
        int size = File::read(task_info->torrent_path(), &buffer, ec, 10 * 1024 * 1024);
        if (size <= 0) {
            fprintf(stderr, "read %s failed: %s.\n", task_info->torrent_path().c_str(), 
                    ec.message().c_str());
            return -28;
        }
        task_info->set_data(&buffer[0], buffer.size());
        task_info->release_torrent_path();
    }

    int64_t taskid = -1;
    BBTSClient client;
    if (client.create_task(add_task_params, &taskid) != 0) {
        fprintf(stderr, "create task failed\n");
        return 2;
    }
    fprintf(stdout, "%ld\n", taskid);
    fprintf(stdout, "taskid:%ld\n", taskid);
    return 0;
}

static int process_serve(int argc, char* argv[]) {
    DownloadConfigure *configure = LazySingleton<DownloadConfigure>::instance();
    message::AddTask add_task_params;
    message::Task *task_info = add_task_params.mutable_task();
    task_info->set_type(message::SEEDING_TASK);
    task_info->set_cmd(get_cmd_string(argc, argv));
    task_info->set_product_tag(configure->product_tag());
    message::TaskOptions *options = add_task_params.mutable_options();
    options->set_upload_limit(configure->upload_limit());
    options->set_max_connections(configure->connection_limit());

    int option = -1;
    int index = -1;
    const struct option long_opt[] = {
            { "torrent", required_argument, NULL, 'r' },
            { "path", required_argument, NULL, 'p' },
            { "seedtime", required_argument, NULL, 'S' },
            { "uplimit", required_argument, NULL, 'u' },
            { "connlimit", required_argument, NULL, 'c' },
            { "size", required_argument, NULL, 's' },
            { "link", no_argument, NULL, 'l' },
            { "hash", no_argument, NULL, 'H' },
            { "seed", no_argument, NULL, 1 },
            { "nocheck", no_argument, NULL, 2 },
            { "besthash", no_argument, NULL, 3 },
            { "include-files", required_argument, NULL, 4 },
            { "exclude-files", required_argument, NULL, 5 },
            { "tracker", required_argument, NULL, 6 },
            { "upload", no_argument, NULL, 7 },
            { "transfer", no_argument, NULL, 8},
            { "product", required_argument, NULL, 9 },
            { NULL, no_argument, NULL, 0 }
    };

    string torrent_file;
    MakeTorrentArgs make_torrent_args;
    while ((option = getopt_long(argc - 1, &argv[1], "r:p:S:u:c:s:lH", long_opt, &index)) != -1) {
        switch (option) {
        case 'r':
            torrent_file = Path::trim(Path::absolute(optarg));
            break;

        case 'p':
            make_torrent_args.path = Path::trim(Path::absolute(optarg));
            break;

        case 'S':
            task_info->set_seeding_time(atoi(optarg));
            break;

        case 'u':
            options->set_upload_limit(StringUtil::readable_to_bytes(string(optarg)));
            break;

        case 'c':
            options->set_max_connections(atoi(optarg));
            break;

        case 's':
            make_torrent_args.piece_size = atoi(optarg) * 1024;
            break;

        case 'l':
            make_torrent_args.flags |= MakeTorrentArgs::SYMLINKS;
            break;

        case 'H':
            make_torrent_args.flags |= MakeTorrentArgs::FILE_HASH;
            break;

        case 1:
            task_info->set_type(message::SEEDING_TASK);
            break;

        case 2:
            task_info->set_type(message::NOCHECK_TASK);
            break;

        case 3:
            make_torrent_args.flags |= MakeTorrentArgs::BEST_HASH;
            break;

        case 4:
            make_torrent_args.include_regex.push_back(optarg);
            break;

        case 5:
            make_torrent_args.exclude_regex.push_back(optarg);
            break;

        case 6: {
            message::Server *tracker = task_info->add_trackers();
            parse_tracker(optarg, tracker);
            break;
        }

        case 7:
            make_torrent_args.flags |= MakeTorrentArgs::UPLOAD_TORRENT;
            break;

        case 8:
            task_info->set_type(message::TRANSFER_TASK);
            break;

        case 9:
            task_info->set_product_tag(optarg);
            break;

        case ':':
        case '?':
        default:
            return 1;
        }
    }

    string infohash;
    vector<char> torrent;
    string error_message;
    if (!make_torrent(make_torrent_args, &infohash, &torrent, &error_message)) {
        return 2;
    }
    fprintf(stdout, "%s\n", torrent_file.c_str());
    fprintf(stdout, "%s\n", infohash.c_str());

    // set data and pass to agent
    task_info->set_data(&torrent[0], torrent.size());

    // if -r then save torrent 
    if (!torrent_file.empty()) {
        error_code ec;
        File::write(torrent_file, torrent, ec);
        if (ec) {
            fprintf(stderr, "write %s failed: %s\n",  torrent_file.c_str(), ec.message().c_str());
            return 3;
        }
    }
    string parent_dir;
    string filename;
    Path::slipt(make_torrent_args.path, &parent_dir, &filename);
    string::size_type pos = filename.find('*');
    if (pos != string::npos) {
        parent_dir = Path::parent_dir(parent_dir);
    }
    task_info->set_save_path(parent_dir);

    int64_t taskid = -1;
    BBTSClient client;
    if (client.create_task(add_task_params, &taskid) != 0) {
        fprintf(stderr, "create task failed.\n");
        return 3;
    }
    fprintf(stdout, "%ld\n", taskid);
    fprintf(stdout, "infohash:%s\n", infohash.c_str());
    fprintf(stdout, "taskid:%ld\n", taskid);
    return 0;
}

static int process_download(int argc, char* argv[]) {
    DownloadConfigure *configure = LazySingleton<DownloadConfigure>::instance();
    configure->set_cmd(get_cmd_string(argc, argv));
    configure->set_listen_port_start(45000 + g_process_info->random() % 500);

    int option = -1;
    int index = -1;
    const struct option long_opt[] = {
            { "torrent", required_argument, NULL, 'r' },
            { "infohash", required_argument, NULL, 'i' },
            { "url", required_argument, NULL, 'U' },
            { "cluster", required_argument, NULL, 'C' },
            { "webseed", required_argument, NULL, 'W' },
            { "path", required_argument, NULL, 'p' },
            { "fullpath", required_argument, NULL, 'n' },
            { "seedtime", required_argument, NULL, 'S' },
            { "port", required_argument, NULL, 'P' },
            { "downlimit", required_argument, NULL, 'd' },
            { "uplimit", required_argument, NULL, 'u' },
            { "connlimit", required_argument, NULL, 'c' },
            { "timeout", required_argument, NULL, 'O' },
            { "offline", no_argument, NULL, 1 },
            { "debug", no_argument, NULL, 2 },
            { "log-level", required_argument, NULL, 3 },
            { "progress-interval", required_argument, NULL, 4},
            { "hang-timeout", required_argument, NULL, 5 },
            { "include-files", required_argument, NULL, 6 },
            { "exclude-files", required_argument, NULL, 7 },
            { "continue", no_argument, NULL, 8 },
            { "tmp-files", no_argument, NULL, 9 },
            { "unix-socket", required_argument, NULL, 10 },
            { "save-torrent", required_argument, NULL, 11 },
            { "sndbuf", required_argument, NULL, 12 },
            { "rcvbuf", required_argument, NULL, 13 },
            { "hdfs-thread", required_argument, NULL, 14 },
            { "ip-white", required_argument, NULL, 15 },
            { "ip-black", required_argument, NULL, 16 },
            { "mask", required_argument, NULL, 17 },
            { "numwant", required_argument, NULL, 18 },
            { "ainterval", required_argument, NULL, 19 },
            { "tracker", required_argument, NULL, 20 },
            { "tracker-failed-quit", no_argument, NULL, 21 },
            { "classpath", required_argument, NULL, 22 },
            { "pre-allocate", no_argument, NULL, 23 },
            { "use-dio-read", no_argument, NULL, 24 },
            { "use-dio-write", no_argument, NULL, 25 },
            { "d-allocate", required_argument, NULL, 26 },
            { "progress", no_argument, NULL, 27 },
            { "ignore-hdfs-error", no_argument, NULL, 28 },
            { "mem-limit", required_argument, NULL, 29 },
            { "hdfs-opts", required_argument, NULL, 31 },
            { "write-cache-line-size", required_argument, NULL, 32 },
            { "read-cache-line-size", required_argument, NULL, 33 },
            { "disable-hash-check", no_argument, NULL, 34 },
            { "source", required_argument, NULL, 35 },
            { "muti-save-paths", required_argument, NULL, 36 },
            { "d-allocate-limit", required_argument, NULL, 37 },
            { "product", required_argument, NULL, 38 },
            { "jobid", required_argument, NULL, 39},
            { "tk-master", required_argument, NULL, 40},
            { "max-hdfs-cache-pieces", required_argument, NULL, 41},
            { "cache-size", required_argument, NULL, 42 },
            { "dont-del-resume-file", no_argument, NULL, 43},
            { "get-torrent", no_argument, NULL, 44},
            { "transfer", no_argument, NULL, 45},
            { "no-check-when-allocated", no_argument, NULL, 46},
            { "force-transfer", required_argument, NULL, 47},
            { "ld-library-path", required_argument, NULL, 48 },
            { "no-p2p", no_argument, NULL, 49 },
            { "enable-dynamic-hash-check", no_argument, NULL, 50 },
            { "user-conf", required_argument, NULL, 51 },
            { "dump-infohash", required_argument, NULL, 52 },
            { "auto-add-time", required_argument, NULL, 53 },
            { NULL, no_argument, NULL, 0 }
    };

    while ((option = getopt_long(
            argc - 1, &argv[1], "r:i:U:C:W:p:n:S:P:d:u:c:O:", long_opt, &index)) != -1) {
        switch (option) {
        case 'r':
            configure->set_torrent_path(optarg);
            break;

        case 'i':
            configure->set_infohash(optarg);
            break;

        case 'U':
            configure->set_torrent_url(optarg);
            break;

        case 'C':
            configure->set_cluster_uri(optarg);
            break;

        case 'W':
            configure->add_web_seed_url(optarg);
            break;

        case 'p':
            if (!configure->has_new_name()) {
                configure->set_save_path(Path::trim(Path::absolute(optarg)));
            }
            break;

        case 'n': {
            string save_path;
            string new_name;
            Path::slipt(Path::trim(Path::absolute(optarg)), &save_path, &new_name);
            configure->set_save_path(save_path);
            configure->set_new_name(new_name);
            break;
        }

        case 'P': {
            pair<int, int> listen_port_range = parse_port_range(optarg);
            configure->set_listen_port_start(listen_port_range.first);
            configure->set_listen_port_end(listen_port_range.second);
            break;
        }

        case 'S':
            configure->set_seeding_time(atoi(optarg));
            break;

        case 'd':
            configure->set_download_limit(StringUtil::readable_to_bytes(optarg));
            break;

        case 'u':
            configure->set_upload_limit(StringUtil::readable_to_bytes(string(optarg)));
            break;

        case 'c':
            configure->set_connection_limit(atoi(optarg));
            break;

        case 'O':
            configure->set_timeout(atoi(optarg));
            break;

        case 1:
            configure->set_listen_port_start(12000 + g_process_info->random() % 1500);
            configure->set_listen_port_end(13999);
            break;

        case 2:
            configure->set_log_level(LOG_LEVEL_DEBUG);
            break;

        case 3:
            configure->set_log_level(static_cast<LogLevel>(atoi(optarg)));
            break;

        case 4:
            configure->set_progress_interval(atoi(optarg));
            break;

        case 5:
            configure->set_hang_timeout(atoi(optarg));
            break;

        case 6:
            configure->add_include_regex(optarg);
            break;

        case 7:
            configure->add_exclude_regex(optarg);
            break;

        case 8:
            configure->set_need_resume(true);
            break;

        case 9:
            configure->set_need_down_to_tmp_first(true);
            break;

        case 10:
            configure->set_control_path(optarg);
            break;

        case 11:
            configure->set_save_torrent_path(Path::trim(Path::absolute(optarg)));
            break;

        case 12:
            configure->set_send_socket_buffer_size(atoi(optarg) * 1024);
            break;

        case 13:
            configure->set_recv_socket_buffer_size(atoi(optarg) * 1024);
            break;

        case 14:
            configure->set_cluster_thread_num(atoi(optarg));
            break;

        case 15:
            if (configure->is_black_ip_filter()) {
                configure->clear_ip_filter();
                configure->set_is_black_ip_filter(false);
            }
            configure->add_ip_filter(optarg);
            break;

        case 16:
            if (!configure->is_black_ip_filter()) {
                configure->clear_ip_filter();
                configure->set_is_black_ip_filter(true);
            }
            configure->add_ip_filter(optarg);
            break;

        case 17:
            configure->add_subnet_mask(optarg);
            break;

        case 18:
            configure->set_peers_num_want(atoi(optarg));
            break;

        case 19:
            configure->set_max_announce_interval(atoi(optarg));
            break;

        case 20: {
            message::Server *tracker = configure->add_tracker();
            parse_tracker(optarg, tracker);
            break;
        }

        case 21:
            configure->set_quit_by_tracker_failed(true);
            break;

        case 22:
            configure->set_class_path(optarg);
            break;

        case 23:
            configure->set_storage_pre_allocate(true);
            break;

        case 24:
            configure->set_use_dio_read(true);
            break;

        case 25:
            configure->set_use_dio_write(true);
            break;

        case 26:
            configure->set_dynamic_allocate(atoi(optarg));
            break;

        case 27:
            configure->set_stdout_progress(true);
            configure->set_progress_interval(2);
            break;

        case 28:
            configure->set_ignore_hdfs_error(true);
            break;

        case 29:
            configure->set_mem_limit(atoi(optarg));
            break;

        case 31:
            configure->set_libhdfs_opts(optarg);
            break;

        case 32:
            configure->set_write_cache_line_size(atoi(optarg));
            break;

        case 33:
            configure->set_read_cache_line_size(atoi(optarg));
            break;

        case 34:
            configure->set_enable_dynamic_hash_check(false);
            configure->set_disable_hash_check(true);
            break;

        case 35: {
            message::SourceURI *source_uri = configure->add_source();
            if (!parse_uri_entry(optarg, source_uri)) {
                fprintf(stderr, "parse source uri failed: %s\n", optarg);
                return 1;
            }
            break;
        }

        case 36:
            configure->add_muti_save_paths(Path::trim(Path::absolute(optarg)));
            break;

        case 37:
            configure->set_dynamic_allocate_limit(StringUtil::readable_to_bytes(optarg));
            break;

        case 38:
            configure->set_product_tag(optarg);
            break;

        case 39:
            configure->set_tk_job_id(optarg);
            break;

        case 40:
            configure->set_tk_master(optarg);
            break;

        case 41:
            configure->set_max_hdfs_cache_pieces(atoi(optarg));
            break;

        case 42:
            configure->set_cache_size(atoi(optarg));
            break;

        case 43:
            configure->set_dont_delete_resume_file(true);
            break;

        case 44:
            configure->set_use_torrent_provider(true);
            break;

        case 45:
            configure->set_is_transfer(true);
            break;

        case 46:
            configure->set_no_check_when_allocated(true);
            break;

        case 47:
            configure->set_force_transfer_number(atoi(optarg));
            break;

        case 48:
            configure->set_ld_library_path(optarg);
            break;

        case 49:
            configure->set_no_p2p(true);
            break;

        case 50:
            configure->set_enable_dynamic_hash_check(true);
            break;

        case 51:
            // --user-conf, we have processed it before
            break;

        case 52:
            configure->set_save_infohash_file_path(Path::trim(Path::absolute(optarg)));
            break;

        case 53:
            configure->set_auto_add_time(atoi(optarg));
            break;

        case ':':
        case '?':
        default:
            return 1;
        }
    }

    if (!configure->has_infohash() && configure->has_use_torrent_provider()) {
        fprintf(stderr, "use --get-torrent but not use -i[infohash]!\n");
        return 1;
    }

    Downloader downloader(configure);
    _exit(downloader.download());
}

static int process_list(int argc, char* argv[]) {
    int option = -1;
    int index = -1;
    const struct option long_opt[] = {
            { "task", required_argument, NULL, 't' },
            { "attr", required_argument, NULL, 'a' },
            { "infohash", required_argument, NULL, 'i' },
            { "noheader", no_argument, NULL, 1 },
            { "unix-socket", required_argument, NULL, 2 },
            { NULL, no_argument, NULL, 0 }
    };

    bool no_header = false;
    vector<int64_t> taskids;
    string unix_socket_path;
    string infohash;

    while ((option = getopt_long(argc - 1, &argv[1], "t:a:i:w", long_opt, &index)) != -1) {
        switch (option) {
        case 't':
            taskids.push_back(atoll(optarg));
            break;

        case 'a':
            parse_attributes(optarg);
            break;

        case 'i':
            infohash = optarg;
            break;

        case 1:
            no_header = true;
            break;

        case 2:
            unix_socket_path = optarg;
            break;

        case ':':
        case '?':
        default:
            return 1;
        }
    }

    BBTSClient client;
    if (!unix_socket_path.empty()) {
        client.set_endpoint(unix_socket_path);
        client.no_send_cred();
    }
    message::BatchListRes res;
    if (client.list_tasks(taskids, &res) != 0) {
        return 3;
    }

    if (!no_header) {
        print_task_status_header();
    }
    bool success = true;
    for (int i = 0; i < res.status_size(); ++i) {
        const message::TaskStatus &task_status = res.status(i);
        if (task_status.status() != message::TaskStatus::UNKNOW) {
            print_task_status(res.status(i));
        } else {
            fprintf(stdout, "%12ld: %s\n",
                    task_status.task().taskid(), task_status.error().c_str());
            success = false;
        }
    }
    if (success) {
        return 0;
    } else {
        return 2;
    }
}

static int process_batch_ctrl(int argc, char* argv[], message::BatchCtrl::ctrl_t ctrl_type) {
    int option = -1;
    int index = -1;
    const struct option long_opt[] = {
            { "task", required_argument, NULL, 't' },
            { "infohash", required_argument, NULL, 'i' },
            { "all", no_argument, NULL, 3 },
            { NULL, no_argument, NULL, 0 }
    };

    bool ctrl_all = false;
    vector<string> infohashs;
    vector<int64_t> taskids;
    while ((option = getopt_long(argc - 1, &argv[1], "t:i:", long_opt, &index)) != -1) {
        switch (option) {
        case 't':
            taskids.push_back(atoll(optarg));
            break;

        case 'i':
            infohashs.push_back(optarg);
            break;

        case 3:
            ctrl_all = true;
            break;

        case ':':
        case '?':
        default:
            return 1;
        }
    }

    if (!ctrl_all && taskids.empty() && infohashs.empty()) {
        fprintf(stderr, "Must specify some taskid or infohash or --all.\n");
        exit(1);
    }

    BBTSClient client;
    message::BatchCtrlRes res;
    if (client.batch_control(taskids, infohashs, ctrl_type, &res) != 0) {
        return 1;
    }
    bool success = true;
    for (int i = 0; i < res.tasks_size(); ++i) {
        const message::TaskRes &task_res = res.tasks(i);
        const message::BaseRes &base_res = task_res.res();
        if (base_res.ret_code() == 0) {
            fprintf(stdout, "ctrl task %ld success\n", task_res.taskid());
        } else {
            fprintf(stderr, "ctrl task %ld fail: %s\n",
                    task_res.taskid(), base_res.fail_msg().c_str());
            success = false;
        }
    }
    if (success) {
        return 0;
    } else {
        return 2;
    }
}

static int process_getopt(int argc, char* argv[]) {
    int option = -1;
    int index = -1;
    const struct option long_opt[] = {
            { "task", required_argument, NULL, 't' },
            { "unix-socket", required_argument, NULL, 1 },
            { NULL, no_argument, NULL, 0 }
    };

    int64_t taskid = -1;
    string unix_socket_path;
    string infohash;
    while ((option = getopt_long(argc - 1, &argv[1], "t:i:", long_opt, &index)) != -1) {
        switch (option) {
        case 't':
            taskid = atoll(optarg);
            break;

        case 1:
            unix_socket_path = optarg;
            break;

        case ':':
        case '?':
        default:
            return 1;
        }
    }

    BBTSClient client;
    if (!unix_socket_path.empty()) {
        client.set_endpoint(unix_socket_path);
        client.no_send_cred();
    }
    if (taskid > 0 || !unix_socket_path.empty()) {
        message::TaskOptions options;
        options.set_taskid(taskid);
        options.set_upload_limit(0);
        options.set_download_limit(0);
        options.set_max_connections(0);
        if (client.get_task_options(&options) != 0) {
            return 1;
        }
        fprintf(stdout, "upload limit: %s/s\nmax connections: %d\n",
                StringUtil::bytes_to_readable(options.upload_limit()).c_str(),
                options.max_connections());
    } else {
        message::AgentOptions options;
        options.set_upload_limit(0);
        options.set_max_connections(0);
        if (client.get_agent_options(&options) != 0) {
            return 1;
        }
        fprintf(stdout, "bind port: %d\nupload limit: %s/s\nmax connections: %d\n",
                options.bind_port(), StringUtil::bytes_to_readable(options.upload_limit()).c_str(),
                options.max_connections());
    }
    return 0;
}

static int process_setopt(int argc, char* argv[]) {
    int option = -1;
    int index = -1;
    const struct option long_opt[] = {
            { "task", required_argument, NULL, 't' },
            { "infohash", required_argument, NULL, 'i' },
            { "downlimit", required_argument, NULL, 'd' },
            { "uplimit", required_argument, NULL, 'u' },
            { "connlimit", required_argument, NULL, 'c' },
            { "unix-socket", required_argument, NULL, 1 },
            { NULL, no_argument, NULL, 0 }
    };

    int64_t taskid = -1;
    int download_limit = 0;
    int upload_limit = 0;
    int max_connections = 0;
    std::string infohash;
    string unix_socket_path;

    while ((option = getopt_long(argc - 1, &argv[1], "t:d:u:c:i:", long_opt, &index)) != -1) {
        switch (option) {
        case 't':
            taskid = atoll(optarg);
            break;

        case 'i':
            infohash.assign(optarg);
            break;

        case 'd':
            download_limit = StringUtil::readable_to_bytes(string(optarg));
            break;

        case 'u':
            upload_limit = StringUtil::readable_to_bytes(string(optarg));
            break;

        case 'c':
            max_connections = atoi(optarg);
            break;

        case 1:
            unix_socket_path = optarg;
            break;

        case ':':
        case '?':
        default:
            return 1;
        }
    }

    int ret = 0;
    BBTSClient client;
    if (!unix_socket_path.empty()) {
        client.set_endpoint(unix_socket_path);
        client.no_send_cred();
    }
    if (taskid > 0 || !infohash.empty() || !unix_socket_path.empty()) {
        message::TaskOptions options;
        options.set_taskid(taskid);
        if (download_limit) {
            options.set_download_limit(download_limit);
        }
        if (upload_limit) {
            options.set_upload_limit(upload_limit);
        }
        if (max_connections) {
            options.set_max_connections(max_connections);
        }
        if (!infohash.empty()) {
            options.set_infohash(infohash);
        }
        ret = client.set_task_options(options);
    } else {
        message::AgentOptions options;
        if (upload_limit) {
            options.set_upload_limit(upload_limit);
        }
        if (max_connections) {
            options.set_max_connections(max_connections);
        }
        ret = client.set_agent_options(options);
    }
    if (ret != 0) {
        return 1;
    }
    return 0;
}

static int process_status(int argc, char* argv[]) {
    int ret = 0;
    BBTSClient client;
    message::Status status;
    ret = client.get_status(&status);
    print_status(status, argc > 2);
    return ret;
}

static int process_rmfiles(int argc, char* argv[]) {
    int option = -1;
    int index = -1;
    const struct option long_opt[] = {
            { "torrent", required_argument, NULL, 't' },
            { "path", required_argument, NULL, 'p' },
            { "fullpath", required_argument, NULL, 'n' },
            { "link", no_argument, NULL, 'l' },
            { "in", no_argument, NULL, 1 },
            { "not-in", no_argument, NULL, 2 },
            { NULL, no_argument, NULL, 0 }
    };

    DeleteFilesArgs delete_args;
    while ((option = getopt_long(argc - 1, &argv[1], "r:p:n:l", long_opt, &index)) != -1) {
        switch (option) {
        case 'r':
            delete_args.torrent_path = Path::trim(Path::absolute(optarg));
            break;

        case 'p':
            if (delete_args.new_name.empty()) {
                delete_args.save_path = Path::trim(Path::absolute(optarg));
            }
            break;

        case 'n':
            Path::slipt(Path::trim(Path::absolute(optarg)),
                    &delete_args.save_path, &delete_args.new_name);
            break;

        case 'l':
            delete_args.flags |= DeleteFilesArgs::SYMLINKS;
            break;

        case 1:
            delete_args.flags |= DeleteFilesArgs::DELETE_IN_TORRENT;
            break;

        case 2:
            delete_args.flags &= ~DeleteFilesArgs::DELETE_IN_TORRENT;
            break;

        case ':':
        case '?':
        default:
            return 1;
        }
    }
    if (!delete_files_by_torrent(delete_args)) {
        return 1;
    }
    return 0;
}

static int process(int argc, char* argv[]) {
    if (argc < 2) {
        print_help(argc, argv);
        return 1;
    }

    typedef boost::function<int (int argc, char* argv[])> Process;
    map<string, Process> process_map = map_list_of<string, Process>
            ("add", &process_add)
            ("serve", &process_serve)
            ("down", &process_download)("sdown", &process_download)
            ("list", &process_list)
            ("status", &process_status)
            ("mkseed", &process_mkseed)
            ("cancel", bind(&process_batch_ctrl, _1, _2, message::BatchCtrl::CANCEL))
            ("pause", bind(&process_batch_ctrl, _1, _2, message::BatchCtrl::PAUSE))
            ("resume", bind(&process_batch_ctrl, _1, _2, message::BatchCtrl::RESUME))
            ("setopt", &process_setopt)
            ("getopt", &process_getopt)
            ("dump", &process_dump)
            ("rmfiles", &process_rmfiles)
            ("bbts-agent", &agent::bbts_agent)
            ("help", &print_help)("--help", &print_help)("-h", &print_help)
            ("-v", &print_version)("--version", &print_version);
    map<string, Process>::iterator it = process_map.find(argv[1]);
    if (it == process_map.end()) {
        fprintf(stderr, "no this operation: %s\n", argv[1]);
        print_help(argc, argv);
        return 1;
    }

    return it->second(argc, argv);
}

static bool init_download_configure(int argc, char* argv[]) {
    if (argc < 2) {
        // print help in bbts::process()
        return true;
    }

    if (strncmp(argv[1], "bbts-agent", strlen("bbts-agent")) == 0) {
        // if is bbts-agent, not need load_download_configure
        return true;
    }

    string conf_file(g_process_info->root_dir() + "/conf/download.conf");
    string user_conf;
    if (get_user_conf_file(argc, argv, &user_conf)) {
        conf_file.assign(user_conf);
    }
    DownloadConfigure *configure = LazySingleton<DownloadConfigure>::instance();
    if (!load_pbconf(conf_file, configure)) {
        fprintf(stderr, "load download.conf failed.\n");
        return false;
    }
    BBTSClient::set_default_endpoint(configure->socket_file());
    return true;
}

} // namespace bbts

int main(int argc, char* argv[]) {
    if (!bbts::init_download_configure(argc, argv)) {
        return 1;
    }

    GOOGLE_PROTOBUF_VERIFY_VERSION;
    int ret = bbts::process(argc, argv);
    google::protobuf::ShutdownProtobufLibrary();
    return ret;
}
