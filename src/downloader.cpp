/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   downloader.cpp
 *
 * @author liuming03
 * @date   2013-7-26
 * @brief 
 */

#include "bbts/tool/downloader.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <deque>
#include <fstream>

#include <boost/bind.hpp>
#include <boost/system/error_code.hpp>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/extensions/metadata_transfer.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>

#include "bbts/bbts_stat.h"
#include "bbts/torrent_plugin.h"
#include "bbts/config.h"
#include "bbts/encode.h"
#include "bbts/file.h"
#include "bbts/options_parse.h"
#include "bbts/path.h"
#include "bbts/process_info.h"
#include "bbts/routing.h"
#include "bbts/string_util.h"
#include "bbts/timer_util.h"
#include "bbts/tool/bbts_client.h"
#include "bbts/tool/cluster_downloader.h"
#include "bbts/torrent_file_util.h"
#include "bbts/transfer_server.h"
#include "transfer_server_types.h"

using std::back_inserter;
using std::copy;
using std::deque;
using std::ifstream;
using std::make_pair;
using std::map;
using std::ofstream;
using std::ostream_iterator;
using std::pair;
using std::set;
using std::string;
using std::vector;

using boost::asio::ip::tcp;
using boost::bind;
using boost::intrusive_ptr;
using boost::posix_time::seconds;
using boost::scoped_array;
using boost::scoped_ptr;
using boost::shared_ptr;
using boost::system::error_code;
using google::protobuf::RepeatedPtrField;
using libtorrent::add_torrent_params;
using libtorrent::address;
using libtorrent::alert;
using libtorrent::cluster_config_entry;
using libtorrent::entry;
using libtorrent::ex_announce_response;
using libtorrent::file_entry;
using libtorrent::ip_filter;
using libtorrent::libtorrent_exception;
using libtorrent::session_settings;
using libtorrent::storage_moved_failed_alert;
using libtorrent::symlink_file;
using libtorrent::torrent_info;
using libtorrent::torrent_status;

namespace bbts {
namespace tool {

static bool catch_stop_sigal = false;

static void handle_stop_sigal(int sig) {
    NOTICE_LOG("catch sigal %d, will stop download", sig);
    catch_stop_sigal = true;
}

static void set_signal_action() {
    struct sigaction sa;
    sa.sa_flags = SA_RESETHAND;
    sa.sa_handler = handle_stop_sigal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static void log_progress(const torrent_status &ts, bool need_stdout) {
    string downrate = StringUtil::bytes_to_readable(ts.download_rate);
    string uprate = StringUtil::bytes_to_readable(ts.upload_rate);
    string download = StringUtil::bytes_to_readable(ts.total_payload_download);
    string upload = StringUtil::bytes_to_readable(ts.total_payload_upload);
    NOTICE_LOG("status: %d, progress: %5.2f%%, downrate: %8s/s, uprate: %8s/s, "
            "download: %12s, upload: %12s", ts.state, (ts).progress * 100, downrate.c_str(),
            uprate.c_str(), download.c_str(), upload.c_str());
    if (need_stdout) {
        fprintf(stdout, "\rstatus: %d, progress: %5.2f%%, downrate: %8s/s, uprate: %8s/s, "
                "download: %12s, upload: %12s", ts.state, (ts).progress * 100, downrate.c_str(),
                uprate.c_str(), download.c_str(), upload.c_str());
        fflush(stdout);
    }
}

Downloader::Downloader(message::DownloadConfigure *configure) :
        _configure(configure),
        _should_stop(false),
        _thrift_tracker(_io_service),
        _seeding_timer(_io_service),
        _timeout_timer(_io_service),
        _quit_check_timer(_io_service),
        _is_timeout(false),
        _is_mem_exceed(false),
        _is_source_quit(false),
        _hang_check_timer(_io_service),
        _last_check_time(time(NULL)),
        _last_check_downloaded(0),
        _low_downrate_times(0),
        _progress_timer(_io_service),
        _is_request_transfer_server(false),
        _disk_allocate_thread(configure),
        _control_server(configure, _io_service),
        _check_download_finished(1) {
    string log_dir;
    string log_name;
    Path::slipt(_configure->download_log_file(), &log_dir, &log_name);
    init_log(log_dir, log_name, LOG_LEVEL_TRACE, (LogLevel)_configure->log_level());
}

Downloader::~Downloader() {
    CLOSE_LOG();
}

bool Downloader::get_ip_filter(ip_filter *filter) {
    if (_configure->subnet_mask_size() != 0 && _configure->is_black_ip_filter()) {
        FATAL_LOG("subnet mask and black ip filter can not used both");
        return false;
    }

    int flag = _configure->is_black_ip_filter() ? ip_filter::blocked : 0;
    error_code ec;
    filter->add_rule(address::from_string("0.0.0.0", ec),
            address::from_string("255.255.255.255", ec), !flag);
    for (int i = 0; i < _configure->ip_filter_size(); ++i) {
        if (!parse_ip_filter(_configure->ip_filter(i), flag, filter)) {
            FATAL_LOG("parse ip filter(%d) failed: %s", flag, _configure->ip_filter(i).c_str());
            return false;
        }
    }
    for (int i = 0; i < _configure->subnet_mask_size(); ++i) {
        if (!parse_subnet_mask(g_process_info->ip(), _configure->subnet_mask(i), filter)) {
            FATAL_LOG("parse subnet mask(%s) failed", _configure->subnet_mask(i).c_str());
            return false;
        }
    }
    return true;
}

void Downloader::get_trackers(vector<pair<string, int> > *trackers) {
    if (_configure->tracker_size() == 0) {
        _thrift_tracker.get_nodes(_configure->infohash(), trackers);
        return;
    }
    vector<string> v;
    for (int i = 0; i < _configure->tracker_size(); ++i) {
        const message::Server& tracker = _configure->tracker(i);
        trackers->push_back(make_pair(tracker.host(), tracker.port()));
    }
}

void Downloader::remove_torrent() {
    torrent_status ts = _torrent.status(0);
    if (_configure->need_resume() && ts.has_metadata && ts.need_save_resume) {
        _torrent.save_resume_data();
    }
    if (_cluster_downloader) {
        _cluster_downloader->stop_download();
        if (_torrent.is_valid()) {
            _stat.get_task_info().__set_downloaded_from_hdfs(
                    _cluster_downloader->total_download());
            NOTICE_LOG("hadoop download: %lld, p2p download: %lld", 
                    _stat.get_task_info().downloaded_from_hdfs,
                    ts.total_payload_download - _stat.get_task_info().downloaded_from_hdfs);
            PeerStatAlert alert(_torrent.info_hash(), _cluster_downloader->start_time(), 0, 
                    _stat.get_task_info().downloaded_from_hdfs,
                    _cluster_downloader->cluster_uri().host()); 
            _stat.add_to_peer_vector(alert);
            DEBUG_LOG("set peer info to vector");
        }
    }
    if (!_torrent.is_valid()) {
        return;
    }
    _stat.get_task_info().__set_payload_downloaded(ts.total_payload_download);
    _stat.get_task_info().__set_payload_uploaded(ts.total_payload_upload);
    _stat.get_task_info().__set_progress_ppm(ts.progress_ppm);
    if (_stat.get_task_info().time_for_downloaded) {
        _stat.get_task_info().__set_time_for_seeding(time(NULL) - _stat.get_task_info().start_time
                - _stat.get_task_info().time_for_download_metadata
                - _stat.get_task_info().time_for_check_files 
                - _stat.get_task_info().time_for_downloaded);
    }
    error_code ec;
    _progress_timer.cancel(ec);
    _session->remove_torrent(_torrent);
}

void Downloader::timeout_callback() {
    WARNING_LOG("download timeout, will quit.");
    if (!_torrent.status(0).is_finished) {
        _is_timeout = true;
    }
    remove_torrent();
}

void Downloader::quit_check_callback() {
    if (use_dynamic_disk_allocate()) {
        if (_disk_allocate_thread.is_end()
            && _torrent.status(0).pieces.count() >= _disk_allocate_thread.pieces_count()
            && (_torrent.status(0).state == torrent_status::finished
                || _torrent.status(0).state == torrent_status::seeding)
            && _check_download_finished != 1) {
            NOTICE_LOG("disk allocate thread end and torrent finished!");
            _check_download_finished = 0;
            if (!on_torrent_finished()) {
                remove_torrent();
                _check_download_finished = -4;
                return;
            }
        }
    }

    if (_source_peers.empty()) {
        return;
    }

    if (_thrift_tracker.have_seed()
        || _cluster_downloader
        || _configure->web_seed_url().size() > 0) {
        // have seed or have cluseter downloader or web seed, just retur and no check
        return;
    }

    bool is_need_quit = true;
    int64_t current_time = time(NULL);
    std::map<tcp::endpoint, SourcePeerInfo>::iterator it = _source_peers.begin();
    for (; it != _source_peers.end(); ++it) {
        if (it->second.is_failed) {
            continue;
        }

        if (it->second.is_connect_success
            && (current_time - it->second.disconnect_time > kMaxSourceDisconnectTime / 2)) {
                it->second.disconnect_time = 0;
        }

        if (it->second.disconnect_time == 0
            || current_time - it->second.disconnect_time < kMaxSourceDisconnectTime) {
            is_need_quit = false;
        }

        if (!it->second.is_connect_success) {
            // try to reconnect
            _torrent.connect_peer(it->first, libtorrent::peer_info::tracker);
        }
    }

    if (!is_need_quit) {
        return;
    }

    TRACE_LOG("all source has quit, so we also quit.");
    _is_source_quit = true;
    remove_torrent();
}

void Downloader::hang_check_callback() {
    torrent_status ts = _torrent.status(0);
    if (_configure->need_resume() && ts.has_metadata && ts.need_save_resume) {
        _torrent.save_resume_data();
    }
    if (ts.progress_ppm == 1000000) {
        return;
    }

    time_t now = time(NULL);
    if ((ts.state == torrent_status::downloading
            && ts.total_payload_download == _last_check_downloaded)
            || (ts.state == torrent_status::downloading_metadata)) {
        if (_last_check_time + _configure->hang_timeout() - now <= 0) {
            FATAL_LOG("download timeout, maybe noseeder.");
            _is_timeout = true;
            remove_torrent();
            return;
        }
    } else {
        _last_check_time = now;
        _last_check_downloaded = ts.total_payload_download;
    }

    if (_configure->download_limit() > 2048 * 1024 && ts.download_rate < 800 * 1024) {
        ++_low_downrate_times;
        if (_low_downrate_times > 9) {
            _torrent.pause();
            _torrent.resume();
            _low_downrate_times = 0;
        }
    }

    // check mem limit
    if (_configure->mem_limit() > 0) {
        static const long PAGE_SIZE = sysconf(_SC_PAGESIZE);
        long virt = 0;
        long rss = 0;
        FILE *fp = fopen("/proc/self/statm", "r");
        fscanf(fp, "%ld%ld", &virt, &rss);
        fclose(fp);
        rss = rss * PAGE_SIZE / 1024 / 1024;
        if (rss > _configure->mem_limit()) {
            FATAL_LOG("rss(%ldM) exceed memery limit(%d)", rss, _configure->mem_limit());
            _is_mem_exceed = true;
            remove_torrent();
            return;
        }
    }
}

void Downloader::progress_callback() {
    torrent_status ts = _torrent.status(0);
    log_progress(ts, _configure->stdout_progress());
}

void Downloader::seeding_complete_callback() {
    TRACE_LOG("download finished, will quit.");
    remove_torrent();
}

void Downloader::announce_callback(const shared_ptr<ex_announce_response> &response, int *retval) {
    if (_thrift_tracker.no_p2p()) {
        NOTICE_LOG("we should not use p2p download");
        _torrent.pause();
    }
    switch (response->ret) {
    case 26234:
        NOTICE_LOG("tracker said we should stop downloading.");
        remove_torrent();
        *retval = 50;
        break;

    case 10000:
        if (_configure->quit_by_tracker_failed()) {
            NOTICE_LOG("connect with tracker failed, we should stop downloading.");
            remove_torrent();
            *retval = 51;
        }
        break;

    default:
        break;
    }
}

void Downloader::to_seeding() {
    if (_configure->enable_dynamic_hash_check()) {
        intrusive_ptr<torrent_info const> ti = _torrent.torrent_file();
        save_torrent_file(true);

        if (_meta_msg) {
            _meta_msg->set_data(ti->metadata().get(), ti->metadata_size());
        }
    }

    torrent_status ts = _torrent.status(0);
    log_progress(ts, _configure->stdout_progress());
    if (_configure->seeding_time() == -1) {
        NOTICE_LOG("seeding for infinite");
        return;
    }

    int seeding_time = 0;
    if (_configure->seeding_time() >= 0) {
        seeding_time = _configure->seeding_time();
    } else {
        int upload_limit = ts.uploads_limit > 0 ? ts.uploads_limit : _configure->upload_limit();
        seeding_time = (ts.num_peers + 1) * ts.total_payload_download / (1 + ts.num_seeds)
                / (upload_limit + 1) / 2;
        int tmp = ts.total_download / (ts.download_rate + 1);
        if (tmp < seeding_time) {
            seeding_time = tmp;
        }
    }
    NOTICE_LOG("seeding for %d seconds", seeding_time);
    timer_run_once("seeding timer", _seeding_timer, seconds(seeding_time),
            bind(&Downloader::seeding_complete_callback, this));
}

bool Downloader::correct_mode_and_symlinks() {
    string save_path = _configure->save_path();
    intrusive_ptr<torrent_info const> ti = _torrent.torrent_file();
    if (!ti) {
        FATAL_LOG("can't get torrent info");
        return false;
    }
    if (!is_patition_download() && _configure->muti_save_paths_size() == 0) {
        // 创建&更改目录mode
        make_torrent_dir(*ti, save_path, bind(&is_accord_rule<RepeatedPtrField<string> >,
                _1, _configure->include_regex(), _configure->exclude_regex()));
        const vector<symlink_file> &symlinks = ti->files().get_symlink_file();
        for (vector<symlink_file>::const_iterator it = symlinks.begin();
                it != symlinks.end(); ++it) {
            if (!is_accord_rule(it->path, _configure->include_regex(),
                    _configure->exclude_regex())) {
                continue;
            }
            string path = save_path + "/" + it->path;
            if (symlink(it->symlink_path.c_str(), path.c_str()) < 0) {
                FATAL_LOG("symlink %s -> %s failed.", path.c_str(), it->symlink_path.c_str());
                return false;
            }
            DEBUG_LOG("symlink file %s -> %s success.", path.c_str(), it->symlink_path.c_str());
        }
    }

    vector<int> file_priorities = _torrent.file_priorities();
    for (int i = 0; i < ti->num_files(); ++i) {
        file_entry fe = ti->file_at(i);
        string file_name = Path::is_absolute(fe.path) ? fe.path : save_path + "/" + fe.path;
        if (file_priorities[i] == 0 || fe.pad_file) {
            continue;
        }
        if (chmod(file_name.c_str(), fe.mode) != 0) {
            FATAL_LOG("file(%s) chmod to %o failed, errno[%d]:%s",
                    file_name.c_str(), fe.mode, errno, strerror(errno));
            return false;
        }
    }
    return true;
}

bool Downloader::on_torrent_finished() {
    if (use_dynamic_disk_allocate() && _check_download_finished > 0) {
        int current_finished_pieces = _torrent.status(0).pieces.count();
        if (current_finished_pieces < _disk_allocate_thread.pieces_count()) {
            // not really finished
            DEBUG_LOG("current_finished_pieces = %d, pieces_count = %d",
                    current_finished_pieces, _disk_allocate_thread.pieces_count());
            return true;
        }
    }

    if (_cluster_downloader) {
        _cluster_downloader->stop_download();
    }
    _stat.get_task_info().__set_time_for_downloaded(time(NULL) - _stat.get_task_info().start_time 
            - _stat.get_task_info().time_for_download_metadata
            - _stat.get_task_info().time_for_check_files);
    error_code ec;
    _hang_check_timer.cancel(ec);
    if (_configure->need_down_to_tmp_first()) {
        TRACE_LOG("mv data from %s to %s", _tmp_save_path.c_str(), _configure->save_path().c_str());
        _torrent.move_storage(_configure->save_path());
        return true;
    }
    return on_storage_moved();
}

bool Downloader::on_storage_moved() {
    error_code ec;
    if (!correct_mode_and_symlinks()) {
        return false;
    }
    to_seeding();
    return true;
}

void Downloader::check_and_request_transfer(
        const std::string &source_host,
        const std::string &uri) {
    // check if source host the same area with dest
    // if so, not need request transfer
    DEBUG_LOG("check and request transfer uri is %s", uri.c_str());
    std::string local_machine_room = g_process_info->machine_room_without_digit();

    // get source machine room
    std::string source_machine_room;
    if (uri.find("hdfs") != std::string::npos) {
        // hdfs uri
        source_machine_room = g_process_info->get_machine_room(source_host, true);
    } else {
        // other uri
        source_machine_room = g_process_info->get_machine_room(source_host, false);
    }
    source_machine_room = g_process_info->get_machine_room_without_digit(source_machine_room);

    // check machine_room area
    std::string conf_file = _configure->routing_conf_file();
    if (!Path::exist(conf_file)) {
        conf_file = g_process_info->root_dir() + "/conf/routing.conf";
    }

    Routing rout;
    rout.set_service_name("transfer");
    if (!rout.load_conf(conf_file, local_machine_room)) {
        WARNING_LOG("load conf:%s for machine_room:%s failed",
                conf_file.c_str(), local_machine_room.c_str());
        return;
    }

    std::string local_area = rout.get_machine_room_area(local_machine_room);
    std::string source_area = rout.get_machine_room_area(source_machine_room);

    if (local_area == source_area && _configure->force_transfer_number() <= 0) {
        NOTICE_LOG("local_machine_room:%s and source:%s are the same, don't need transfer",
                local_machine_room.c_str(), source_machine_room.c_str());
        return;
    }
    if (source_area.empty()) {
        source_area = local_area;
    }

    RequestTransferInfo info;
    intrusive_ptr<torrent_info const> ti = _torrent.torrent_file();
    string infohash;
    hex_encode(ti->info_hash().to_string(), &infohash);
    info.__set_infohash(infohash);

    std::string key = infohash + "_" + source_area + "_" + local_area;
    info.__set_key(key);
    info.__set_uri(uri);
    info.__set_size(ti->total_size());
    info.__set_hostname(g_process_info->hostname());
    info.__set_download_limit(_configure->download_limit());
    info.__set_disable_hash_check(_configure->disable_hash_check());
    info.__set_enable_dynamic_hash_check(_configure->enable_dynamic_hash_check());
    info.__set_product_name(_configure->product_tag());
    if (_configure->force_transfer_number() > 0) {
        info.__set_transfer_number(_configure->force_transfer_number());
    }
    
    TransferServer transfer_server;
    transfer_server.set_routing_conf(_configure->routing_conf_file());
    transfer_server.request_transfer(info);
}

bool Downloader::on_state_changed(const libtorrent::state_changed_alert *alert) {
    NOTICE_LOG("%s", alert->message().c_str());

    intrusive_ptr<torrent_info const> ti = _torrent.torrent_file();
    if (alert->state != torrent_status::downloading_metadata && ti
            && use_dynamic_disk_allocate() && !_disk_allocate_thread.is_start()) {
        _disk_allocate_thread.start(_torrent);
    }

    if (alert->state != torrent_status::downloading) {
        return true;
    }

    _stat.get_task_info().__set_time_for_check_files(time(NULL) - _stat.get_task_info().start_time 
            - _stat.get_task_info().time_for_download_metadata);

    message::SourceURI cluster_uri;
    if (ti) {
        string infohash;
        hex_encode(ti->info_hash().to_string(), &infohash);
        _stat.get_task_info().__set_infohash(infohash);
        _stat.get_task_info().__set_total_size(ti->total_size());
        _stat.get_task_info().__set_piece_length(ti->piece_length());
        _stat.get_task_info().__set_num_files(ti->num_files());
        _stat.get_task_info().__set_num_paths(ti->get_paths().size());
        _stat.get_task_info().__set_num_symlinks(ti->files().get_symlink_file().size());
        _stat.get_task_info().__set_num_pieces(ti->num_pieces());
        cluster_uri = convert(ti->cluster_config());
    }

    if (!_cluster_downloader) {
        if (cluster_uri.port() != 0 && !cluster_uri.host().empty() &&
                !cluster_uri.user().empty() && !cluster_uri.passwd().empty()) {
            int flags = 0;
            if (_configure->ignore_hdfs_error()) {
                flags |= ClusterDownloader::IGNORE_ERROR;
            }
            if (_configure->disable_hash_check()) {
                flags |= ClusterDownloader::DISABLE_HASH_CHECK;
            }
            if (use_dynamic_disk_allocate()) {
                flags |= ClusterDownloader::USE_DYNAMIC_ALLOCATE;
            }
            _cluster_downloader.reset(new ClusterDownloader(cluster_uri, _torrent,
                    _configure->cluster_thread_num(), flags, _configure->max_hdfs_cache_pieces()));
        }
    }

    if (_cluster_downloader && !_cluster_downloader->is_start()) {
        _disk_allocate_thread.set_cluster_downloader(_cluster_downloader.get());
        if (!_cluster_downloader->start_download()) {
            return false;
        }

        if (_disk_allocate_thread.is_end()) {
            // if allocate end, but cluster is not started, should tell it start
            _cluster_downloader->update_allocated_pieces(_torrent.torrent_file()->num_pieces());
            _cluster_downloader->notify_all();
        }

        set_signal_action();
        _stat.get_task_info().__set_is_hdfs_download(true);
        _stat.get_task_info().__set_hdfs_address(to_string(_cluster_downloader->cluster_uri()));
    }

    if (!_is_request_transfer_server && !_configure->product_tag().empty()) {
        if (!ti) {
            return true;
        }

        TRACE_LOG("product tag is %s", _configure->product_tag().c_str());
        _stat.get_task_info().__set_product_tag(_configure->product_tag());
        _is_request_transfer_server = true;

        if (cluster_uri.host().empty()
            && _configure->cluster_uri().empty()
            && _configure->source_size() == 0
            && _configure->force_transfer_number() <= 0) {
            NOTICE_LOG("use product tag:%s, but both cluster_uri and source_uri is empty!",
                    _configure->product_tag().c_str());
        } else if (!_configure->cluster_uri().empty()) {
            message::SourceURI param_cluster_uri;
            if (!parse_uri_entry(_configure->cluster_uri(), &param_cluster_uri)) {
                WARNING_LOG("can't parse cluster uri:%s", _configure->cluster_uri().c_str());
                return true;
            }
            check_and_request_transfer(param_cluster_uri.host(), _configure->cluster_uri());
        } else if (!cluster_uri.host().empty()) {
            check_and_request_transfer(cluster_uri.host(), to_string(cluster_uri));
        } else if (_configure->source_size() != 0) {
            const message::SourceURI &source = _configure->source(0);
            check_and_request_transfer(source.host(), to_string(source));
        } else if (_configure->force_transfer_number() > 0) {
            // 这里其实不必要 if，直接else就行，只是为了直观说明条件，分支已全覆盖
            check_and_request_transfer("", "");
        }
    }

    return true;
}

void Downloader::on_metadata_received() {
    _stat.get_task_info().__set_time_for_download_metadata(
            time(NULL) - _stat.get_task_info().start_time);
    intrusive_ptr<torrent_info const> ti = _torrent.torrent_file();
    vector<int> file_priorities;
    if (is_patition_download()) {
        parse_need_download_files<int>(_configure->include_regex(),
                _configure->exclude_regex(), *ti, &file_priorities);
        _torrent.prioritize_files(file_priorities);
        if (_configure->log_level() == LOG_LEVEL_DEBUG && is_patition_download()) {
            for (unsigned i = 0; i < file_priorities.size(); ++i) {
                DEBUG_LOG("file %d priorities: %d", i, file_priorities[i]);
            }
        }
    } else {
        file_priorities.resize(ti->num_files(), 1);
    }
    if (!_meta_msg) {
        DEBUG_LOG("_meta_msg is NULL, return");
        return;
    }
    save_torrent_file(_configure->enable_dynamic_hash_check());

    save_infohash_file();

    if (ti->metadata_size() > _configure->max_metadata_size()) {
        _meta_msg.reset();
    } else {
        _meta_msg->set_data(ti->metadata().get(), ti->metadata_size());
    }
}

void Downloader::on_hash_failed(const libtorrent::hash_failed_alert *alert) {
    WARNING_LOG("alert: %s.", alert->message().c_str());
    if (_cluster_downloader) {
        _cluster_downloader->add_failed_piece(alert->piece_index);
    }
}

void Downloader::on_piece_finished(const libtorrent::piece_finished_alert * alert) {
    DEBUG_LOG("alert: %s.", alert->message().c_str());
    if (_cluster_downloader) {
        _cluster_downloader->notify_piece_finished(alert->piece_index);
    }
}

void Downloader::on_save_resume_data(const libtorrent::save_resume_data_alert *alert) {
    vector<char> buffer;
    libtorrent::bencode(back_inserter(buffer), *(alert->resume_data));
    _resume.save(buffer);
}

void Downloader::on_peer_disconnect(const libtorrent::peer_disconnected_alert *alert) {
    if (alert->error != boost::asio::error::connection_refused) {
        return;
    }
    if (_source_peers.find(alert->ip) != _source_peers.end()) {
        if (_source_peers[alert->ip].is_connect_success) {
            _source_peers[alert->ip].is_connect_success = false;
        }
        if (_source_peers[alert->ip].disconnect_time == 0) {
            _source_peers[alert->ip].disconnect_time = time(NULL);
        }
    }
}

void Downloader::on_peer_connect(const libtorrent::peer_connect_alert *alert) {
    if (_source_peers.find(alert->ip) != _source_peers.end()) {
        _source_peers[alert->ip].is_connect_success = true;
    }
}

void Downloader::on_peer_source_failed(const PeerSourceRequestFailedAlert *alert) {
    _source_peers[alert->ip].is_failed = true;
    WARNING_LOG("%s", alert->message().c_str());
}

bool Downloader::on_peer_stat_alert(const PeerStatAlert *alert) {
    _stat.add_to_peer_vector(*alert);
    DEBUG_LOG("set peer info: %s", alert->infohash().c_str());
    return true;
}

void Downloader::process_alert(int *retval, bool loop) {
    int &ret = *retval;
    if (catch_stop_sigal) {
        remove_torrent();
        ret = 20;
    }

    _session->wait_for_alert(libtorrent::milliseconds(100));
    deque<alert *> alerts;
    _session->pop_alerts(&alerts);
    for (deque<alert *>::iterator it = alerts.begin(); it != alerts.end(); ++it) {
        switch ((*it)->type()) {
        case libtorrent::save_resume_data_alert::alert_type:
            on_save_resume_data((libtorrent::save_resume_data_alert *)(*it));
            break;

        case libtorrent::listen_failed_alert::alert_type: {
            libtorrent::listen_failed_alert *lf_alert = (libtorrent::listen_failed_alert *)(*it);
            const libtorrent::tcp::endpoint &ep = lf_alert->endpoint;
            if (lf_alert->sock_type == libtorrent::listen_failed_alert::tcp
                    && ep.address().is_v4() && ep.port() != 0) {
                FATAL_LOG("%s", lf_alert->message().c_str());
                remove_torrent();
                ret = -2;
            }
            break;
        }

        case libtorrent::state_changed_alert::alert_type:
            if (!on_state_changed((libtorrent::state_changed_alert *)(*it))) {
                remove_torrent();
                ret = -3;
            }
            break;

        case libtorrent::metadata_received_alert::alert_type:
            NOTICE_LOG("%s", (*it)->message().c_str());
            on_metadata_received();
            break;

        case libtorrent::torrent_finished_alert::alert_type: {
            TRACE_LOG("%s", (*it)->message().c_str());
            if (!on_torrent_finished()) {
                remove_torrent();
                ret = -4;
            }
            break;
        }

        case libtorrent::storage_moved_alert::alert_type:
            TRACE_LOG("%s", (*it)->message().c_str());
            if (!on_storage_moved()) {
                remove_torrent();
                ret = -5;
            }
            break;

        case libtorrent::torrent_error_alert::alert_type:
            FATAL_LOG("download failed: %s", (*it)->message().c_str());
            remove_torrent();
            ret = -6;
            break;

        case libtorrent::hash_failed_alert::alert_type:
            on_hash_failed((libtorrent::hash_failed_alert *)(*it));
            break;

        case libtorrent::torrent_removed_alert::alert_type: {
            TRACE_LOG("%s", (*it)->message().c_str());
            if (_is_timeout) {
                ret = -7;
            } else if (_is_mem_exceed) {
                ret = -9;
            } else if (_is_source_quit) {
                ret = -10;
            }
            _should_stop = true;
            break;
        }

        case libtorrent::storage_moved_failed_alert::alert_type:
            FATAL_LOG("%s", (*it)->message().c_str());
            remove_torrent();
            ret = -8;
            break;

        case PeerStatAlert::ALERT_TYPE:
            on_peer_stat_alert(static_cast<PeerStatAlert *>(*it));
            break;

        case libtorrent::listen_succeeded_alert::alert_type:
        case libtorrent::add_torrent_alert::alert_type:
        case libtorrent::torrent_added_alert::alert_type:
        case libtorrent::torrent_deleted_alert::alert_type:
        case libtorrent::lsd_peer_alert::alert_type:
        case libtorrent::url_seed_alert::alert_type:
        case libtorrent::torrent_resumed_alert::alert_type:
        case libtorrent::torrent_paused_alert::alert_type:
        case libtorrent::tracker_reply_alert::alert_type:
        case libtorrent::tracker_announce_alert::alert_type:
        case libtorrent::torrent_checked_alert::alert_type:
            TRACE_LOG("%s", (*it)->message().c_str());
            break;

        case libtorrent::peer_blocked_alert::alert_type:
        case libtorrent::peer_ban_alert::alert_type:
            NOTICE_LOG("%s", (*it)->message().c_str());
            break;

        case libtorrent::peer_error_alert::alert_type:
        case libtorrent::torrent_delete_failed_alert::alert_type:
        case libtorrent::metadata_failed_alert::alert_type:
        case libtorrent::tracker_warning_alert::alert_type:
        case libtorrent::tracker_error_alert::alert_type:
        case libtorrent::file_error_alert::alert_type:
        case libtorrent::save_resume_data_failed_alert::alert_type:
        case libtorrent::fastresume_rejected_alert::alert_type:
            WARNING_LOG("%s", (*it)->message().c_str());
            break;

        case libtorrent::peer_disconnected_alert::alert_type:
            on_peer_disconnect((libtorrent::peer_disconnected_alert *)(*it));
            DEBUG_LOG("%s", (*it)->message().c_str());
            break;

        case libtorrent::peer_connect_alert::alert_type:
            on_peer_connect((libtorrent::peer_connect_alert *)(*it));
            DEBUG_LOG("%s", (*it)->message().c_str());
            break;

        case PeerSourceRequestFailedAlert::ALERT_TYPE:
            on_peer_source_failed((PeerSourceRequestFailedAlert*)(*it));
            break;

        case libtorrent::incoming_connection_alert::alert_type:
        case libtorrent::performance_alert::alert_type:
            DEBUG_LOG("%s", (*it)->message().c_str());
            break;

        case libtorrent::piece_finished_alert::alert_type:
            on_piece_finished((libtorrent::piece_finished_alert *)(*it));
            break;

        default:
            DEBUG_LOG("%s", (*it)->message().c_str());
            break;
        }
        delete *it;
    }

    if (_cluster_downloader && _cluster_downloader->is_stoped()
            && _cluster_downloader->get_retval() != 0) {
        if (!_torrent.status(0).is_finished) {
            ret = _cluster_downloader->get_retval();
            remove_torrent();
        } else {
            _cluster_downloader->set_retval(0);
        }
    }

    if (_should_stop) {
        error_code ec;
        _seeding_timer.cancel(ec);
        _timeout_timer.cancel(ec);
        _quit_check_timer.cancel(ec);
        _hang_check_timer.cancel(ec);
        _progress_timer.cancel(ec);
        _control_server.stop();
    }

    if (loop) {
        _io_service.post(bind(&Downloader::process_alert, this, retval, !_should_stop));
    }
}

void Downloader::start_download() {
    _stat.get_task_info().__set_start_time(time(NULL));
    _torrent.resume();
    if (!use_dynamic_disk_allocate()) {
        _torrent.auto_managed(true);
    }
    timer_run_cycle("hang check", _hang_check_timer, seconds(15),
            bind(&Downloader::hang_check_callback, this));
    if (_configure->timeout() > 0) {
        timer_run_once("download timeout", _timeout_timer, seconds(_configure->timeout()),
                bind(&Downloader::timeout_callback, this));
    }

    timer_run_cycle("download progress", _progress_timer, seconds(_configure->progress_interval()),
            bind(&Downloader::progress_callback, this));

    for (map<tcp::endpoint, SourcePeerInfo>::iterator i = _source_peers.begin();
            i != _source_peers.end(); ++i) {
        _torrent.connect_peer(i->first, libtorrent::peer_info::tracker);
    }

    timer_run_cycle("download progress", _quit_check_timer, seconds(1),
            bind(&Downloader::quit_check_callback, this));
}

static bool compair(const pair<string, uint64_t> &left, const pair<string, uint64_t> &right) {
    return left.second > right.second;
}

void Downloader::slip_files_to_muti_paths(torrent_info *ti, const vector<int> &file_priorities) {
    vector<pair<string, uint64_t> > heap;
    for (int i = 0; i < _configure->muti_save_paths_size(); ++i) {
        heap.push_back(make_pair(_configure->muti_save_paths(i), 0L));
    }
    for (int i = 0; i < ti->num_files(); ++i) {
        file_entry fe = ti->file_at(i);
        if (file_priorities[i] == 0 || fe.pad_file) {
            continue;
        }
        pop_heap(heap.begin(), heap.end(), &compair);
        pair<string, uint64_t> &last_elem = heap.back();
        string new_filename = last_elem.first + '/' + Path::subpath(fe.path);
        DEBUG_LOG("rename file from %s to %s", fe.path.c_str(), new_filename.c_str());
        ti->rename_file(i, new_filename);
        last_elem.second += fe.size;
        push_heap(heap.begin(), heap.end(), &compair);
    }
}

bool Downloader::add_torrent_file(const vector<char> &buffer, add_torrent_params *torrent_params) {
    error_code ec;
    torrent_params->ti = new torrent_info(&buffer[0], buffer.size(), ec, 0, _configure->new_name());
    if (ec) {
        FATAL_LOG("pares torrent file(%s) failed, ret(%d): %s.",
                _configure->torrent_path().c_str(), ec.value(), ec.message().c_str());
        return false;
    }
    _configure->set_infohash(libtorrent::to_hex(torrent_params->ti->info_hash().to_string()));
    vector<int> file_priorities;
    if (is_patition_download()) {
        parse_need_download_files<int>(_configure->include_regex(),
                _configure->exclude_regex(), *(torrent_params->ti), &file_priorities);
        copy(file_priorities.begin(), file_priorities.end(),
                back_inserter(torrent_params->file_priorities));
    } else {
        file_priorities.resize(torrent_params->ti->num_files(), 1);
    }

    if (_configure->muti_save_paths_size() != 0) {
        slip_files_to_muti_paths(torrent_params->ti.get(), file_priorities);
    }
    return true;
}

void Downloader::save_infohash_file() {
    const string &save_infohash_file_path = _configure->save_infohash_file_path();
    if (save_infohash_file_path.empty()) {
        return;
    }
    DEBUG_LOG("save infohash file path is %s", save_infohash_file_path.c_str());
    ofstream ofs(save_infohash_file_path.c_str());
    if (!ofs) {
        WARNING_LOG("open file(%s) to save infohash failed", save_infohash_file_path.c_str());
        return;
    } 
    ofs << _configure->infohash();
    ofs.close();
}

void Downloader::save_torrent_file(bool dynamic_hash_check) {
    const string &save_torrent_path = _configure->save_torrent_path();
    if (save_torrent_path.empty()) {
        return;
    }
    DEBUG_LOG("save torrent file path is %s", save_torrent_path.c_str());
    intrusive_ptr<torrent_info const> ti = _torrent.torrent_file();
    libtorrent::create_torrent ct(*ti);
    // 如果动态校验，需要保持旧的infohash，在libtorrent里 解析时如果有sp则用它替换掉infohash
    if (dynamic_hash_check) {
        string bytes;
        hex_decode(_configure->infohash(), &bytes);
        ct.set_sp_infohash(libtorrent::sha1_hash(bytes));
    }
    entry te = ct.generate();
    ofstream ofs(save_torrent_path.c_str());
    if (!ofs) {
        WARNING_LOG("open file(%s) to save torrent failed", save_torrent_path.c_str());
        return;
    }
    libtorrent::bencode(ostream_iterator<char>(ofs), te);
    ofs.close();
}

void Downloader::save_torrent_file_by_data(
        const vector<char> &buffer) {
    const string &save_torrent_path = _configure->save_torrent_path();
    if (save_torrent_path.empty()) {
        return;
    }
    error_code ec;
    intrusive_ptr<torrent_info const> ti = new torrent_info(&buffer[0],
            buffer.size(), ec, 0, _configure->new_name());
    libtorrent::create_torrent ct(*ti);
    entry te = ct.generate();
    DEBUG_LOG("save torrent file by data path is %s", save_torrent_path.c_str());
    ofstream ofs(save_torrent_path.c_str());
    if (!ofs) {
        WARNING_LOG("open file(%s) to save torrent failed", save_torrent_path.c_str());
        return;
    } 
    libtorrent::bencode(ostream_iterator<char>(ofs), te);
    ofs.close();
}

bool Downloader::add_download_task() {
    message::SourceURI cluster_uri;
    if (!parse_uri_entry(_configure->cluster_uri(), &cluster_uri)) {
        FATAL_LOG("Can't parse cluster uri %s", _configure->cluster_uri().c_str());
        return false;
    }
    if (_configure->muti_save_paths_size() != 0) {
        if (_configure->torrent_path().empty() &&
                (cluster_uri.path().empty() || cluster_uri.protocol() != "hdfs")) {
            FATAL_LOG("muti save path is only used by torrent file or torrent provider!");
            return false;
        }
        for (int i = 0; i < _configure->muti_save_paths_size(); ++i) {
            const string &muti_save_path = _configure->muti_save_paths(i);
            if (!Path::mkdirs(muti_save_path, 0755)) {
                FATAL_LOG("mkdir muti save path(%s) failed", muti_save_path.c_str());
                return false;
            }
        }
        _configure->set_save_path(_configure->muti_save_paths(0));
    }
    if (!Path::mkdirs(_configure->save_path(), 0755)) {
        FATAL_LOG("mkdirs save path(%s) faled", _configure->save_path().c_str());
        return false;
    }

    error_code ec;
    add_torrent_params torrent_params;
    torrent_params.userdata = &_source_peers;
    if (!_configure->torrent_path().empty()) {
        vector<char> buffer;
        int size = File::read(_configure->torrent_path(), &buffer, ec, 10 * 1024 * 1024);
        if (size <= 0) {
            FATAL_LOG("read torrent file(%s) failed: %s",
                    _configure->torrent_path().c_str(), ec.message().c_str());
            return false;
        }
        if (!add_torrent_file(buffer, &torrent_params)) {
            return false;
        }
    } else if (!_configure->torrent_url().empty()) {
        torrent_params.url = _configure->torrent_url();
        _configure->set_need_resume(false);
        _configure->set_need_down_to_tmp_first(false);
    } else if (!_configure->infohash().empty()) {
        // check if infohash correct
        string bytes;
        if (_configure->infohash().length() != 40
                || !bbts::hex_decode(_configure->infohash(), &bytes)) {
            FATAL_LOG("infohash(%s) not correct.", _configure->infohash().c_str());
            return false;
        }

        if (_configure->use_torrent_provider()) {
            // get torrent from torrent-provider 
            vector<char> buffer;
            std::string source;
            if (!_torrent_provider.get_infohash_torrent_file(
                        _configure->infohash(),
                        &source,
                        &buffer)) {
                FATAL_LOG("can not get torrent file(%s) from torrent provider",
                        _configure->cluster_uri().c_str());
                return false;
            }
            if (!add_torrent_file(buffer, &torrent_params)) {
                return false;
            }

            _meta_msg.reset(new message::Metadata());
            _meta_msg->set_infohash(_configure->infohash());
            _meta_msg->set_data(torrent_params.ti->metadata().get(),
                    torrent_params.ti->metadata_size());

            message::SourceURI *source_uri = _configure->add_source();
            // ignore parse result
            if (parse_uri_entry(source, source_uri)) {
                if (source_uri->protocol() == "gko3") {
                    if (!check_and_add_source_peers(true)) {
                        return false;
                    }
                } else if (source_uri->protocol() == "hdfs"
                    || source_uri->protocol() == "nfs") {
                    // torrent code may including hdfs info
                    // TODO hdfs -C param
                }
            }

            // save infohash to file
            save_infohash_file();

            // save torrent file only if not enable_dynamic_hash_check
            if (!_configure->enable_dynamic_hash_check()) {
                save_torrent_file_by_data(buffer);
            }
        } else {
            if (!_configure->save_torrent_path().empty()) {
                ofstream ofs(_configure->save_torrent_path().c_str());
                if (!ofs) {
                    FATAL_LOG("can't create torrent file(%s) for save.",
                            _configure->save_torrent_path().c_str());
                    return false;
                }
                ofs.close();
            }

            torrent_params.info_hash.assign(bytes);
            torrent_params.ti = new torrent_info(torrent_params.info_hash,
                    0, _configure->new_name());
            _meta_msg.reset(new message::Metadata());
            _meta_msg->set_infohash(_configure->infohash());
        }

    } else if ((!cluster_uri.path().empty() && cluster_uri.protocol() == "hdfs")
            || (_configure->source_size() > 0 && _configure->source(0).protocol() == "gko3")) {
        // cluster or source peers, use torrent provider
        vector<char> buffer;
        std::string uri;
        if (!cluster_uri.path().empty() && cluster_uri.protocol() == "hdfs") {
            uri = _configure->cluster_uri();
        } else {
            // 这里取第一个source的地址
            const message::SourceURI &source_uri = _configure->source(0);
            uri = to_string(source_uri);
            DEBUG_LOG("first source is: %s", uri.c_str());
        }
        if (!_torrent_provider.get_torrent_file(uri, &buffer)) {
            FATAL_LOG("can not get torrent file(%s) from torrent provider", uri.c_str());
            return false;
        }
        if (!add_torrent_file(buffer, &torrent_params)) {
            return false;
        }
        if (!cluster_uri.path().empty()) {
            cluster_uri.set_path(Path::parent_dir(cluster_uri.path()));
        }
        _meta_msg.reset(new message::Metadata());
        _meta_msg->set_infohash(_configure->infohash());
        _meta_msg->set_uri(uri);
        _meta_msg->set_data(torrent_params.ti->metadata().get(),
                torrent_params.ti->metadata_size());
        
        // save infohash to file
        save_infohash_file();

        // save torrent file only if not enable_dynamic_hash_check
        if (!_configure->enable_dynamic_hash_check()) {
            save_torrent_file_by_data(buffer);
        }
    } else {
        FATAL_LOG("not set infohash or torrent file or torrent_url");
        return false;
    }

    get_trackers(&torrent_params.thrift_trackers);
    if (_configure->need_down_to_tmp_first()) {
        _tmp_save_path = _configure->save_path() + "/.gko3_" + _configure->infohash() + "_"
                + _configure->new_name() + ".tmp";
        if (!Path::mkdir(_tmp_save_path, 0755)) {
            FATAL_LOG("tmp save path(%s) can not create.", _tmp_save_path.c_str());
            return false;
        }
    } else {
        _tmp_save_path = _configure->save_path();
    }
    torrent_params.save_path = _tmp_save_path;

    if (_configure->need_resume()) {
        string resume_filename = _tmp_save_path + "/.gko3_" + _configure->infohash() + "_"
                + _configure->new_name() + ".resume";
        _resume.set_filename(resume_filename);
        _resume.load(&torrent_params.resume_data);
    }

    if (_configure->storage_pre_allocate()) {
        torrent_params.storage_mode = libtorrent::storage_mode_allocate;
    }

    // check file prioritizes for dynamic allocate
    if (use_dynamic_disk_allocate()
        && _configure->no_check_when_allocated()
        && !_resume.check_file()) {

        if (torrent_params.file_priorities.size()
            < static_cast<size_t>(torrent_params.ti->num_files())) {
            // not file priorities, resize and set 1
            torrent_params.file_priorities.resize(torrent_params.ti->num_files(), 1);
        }

        for (int i = 0; i < torrent_params.ti->num_files(); ++i) {
            libtorrent::file_entry fe = torrent_params.ti->file_at(i);
            if (torrent_params.file_priorities.at(i) == 0 || fe.pad_file || fe.size == 0) {
                continue;
            }
            string file_name = Path::is_absolute(fe.path) ?
                    fe.path : torrent_params.save_path + "/" + fe.path;
            struct stat statbuf;
            if (::stat(file_name.c_str(), &statbuf) != 0) {
                // no exist, ignore
                continue;
            }
            if (statbuf.st_size == fe.size) {
                DEBUG_LOG("file:%s exist, no check and skip it!", file_name.c_str());
                torrent_params.file_priorities.at(i) = 0;
            }
        }
    }

    torrent_params.flags |= add_torrent_params::flag_paused;
    torrent_params.flags |= add_torrent_params::flag_duplicate_is_error;
    torrent_params.flags |= add_torrent_params::flag_override_resume_data;
    torrent_params.flags &= ~add_torrent_params::flag_auto_managed;
    if (use_dynamic_disk_allocate()) {
        torrent_params.flags |= add_torrent_params::flag_upload_mode;
    }
    if (_configure->is_transfer()) {
        torrent_params.flags |= add_torrent_params::flag_transfer;
    }
    torrent_params.download_limit = _configure->download_limit();
    torrent_params.upload_limit = _configure->upload_limit();
    torrent_params.max_connections = _configure->connection_limit();
    copy(_configure->web_seed_url().begin(), _configure->web_seed_url().end(),
            back_inserter(torrent_params.url_seeds));
    _torrent = _session->add_torrent(torrent_params, ec);
    if (ec) {
        FATAL_LOG("add torrent failed: %s.", ec.message().c_str());
        return false;
    }

    if (!cluster_uri.path().empty()) {
        int flags = 0;
        if (_configure->ignore_hdfs_error()) {
            flags |= ClusterDownloader::IGNORE_ERROR;
        }
        if (_configure->disable_hash_check()) {
            flags |= ClusterDownloader::DISABLE_HASH_CHECK;
        }
        if (use_dynamic_disk_allocate()) {
            flags |= ClusterDownloader::USE_DYNAMIC_ALLOCATE;
        }
        if (_configure->enable_dynamic_hash_check()) {
            flags |= ClusterDownloader::ENABLE_DYNAMIC_HASH_CHECK;
        }
        _cluster_downloader.reset(new ClusterDownloader(cluster_uri, _torrent,
                _configure->cluster_thread_num(), flags, _configure->max_hdfs_cache_pieces()));
    }
    NOTICE_LOG("[infohash: %s][downlimit: %s][uplimit: %s][maxconn: %d]",
            _configure->infohash().c_str(),
            StringUtil::bytes_to_readable(_configure->download_limit()).c_str(),
            StringUtil::bytes_to_readable(_configure->upload_limit()).c_str(),
            _torrent.max_connections());
    return true;
}

bool Downloader::check_and_add_source_peers(bool is_from_torrent_provider) {
    error_code ec;
    tcp::resolver resolver(_io_service);
    tcp::resolver::iterator end;
    for (int i = 0; i < _configure->source_size(); ++i) {
        const message::SourceURI &source_uri = _configure->source(i);
        if (source_uri.protocol() != "gko3") {
            WARNING_LOG("protocol(%s) invalid, must be gko3!", source_uri.protocol().c_str());
            continue;
        }
        if (source_uri.port() == 0) {
            WARNING_LOG("port:0 is invalid, must great than 0!");
            continue;
        }

        tcp::resolver::query query(source_uri.host(), StringUtil::to_string(source_uri.port()));
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query, ec);
        if (ec) {
            WARNING_LOG("resolve source(%s) failed: %s",
                    source_uri.host().c_str(), ec.message().c_str());
            continue;
        }
        while (endpoint_iterator != end) {
            if (_source_peers.find(endpoint_iterator->endpoint()) != _source_peers.end()
                && is_from_torrent_provider) {
                // from torrent_provider, and source has exists before,
                // we don't use source from torrent_provider because it may has been deleted
                ++endpoint_iterator;
                continue;
            }
            SourcePeerInfo source_info;
            source_info.path = source_uri.path();
            _source_peers[endpoint_iterator->endpoint()] = source_info;
            ++endpoint_iterator;
        }
    }
    
    if (_configure->source_size() > 0 && _source_peers.empty()) {
        FATAL_LOG("use --source option, but all sources are invalid!");
        return false;
    }

    return true;
}

bool Downloader::auto_add_task(int32_t seeding_time) {
    if (seeding_time <= 0) {
        WARNING_LOG("auto-add-time must greater than 0!");
        return false;
    }
    message::AddTask add_task_params;
    message::Task *task_info = add_task_params.mutable_task();
    task_info->set_type(message::NOCHECK_TASK);

    task_info->set_cmd(_configure->cmd());
    DEBUG_LOG("auto add task's cmd is:%s", task_info->cmd().c_str());
        
    task_info->set_product_tag(_configure->product_tag());
    DEBUG_LOG("auto add task's product_tag is:%s", task_info->product_tag().c_str());
    message::TaskOptions *options = add_task_params.mutable_options();
        
    options->set_upload_limit(_configure->upload_limit());
    DEBUG_LOG("auto add task's upload limit is:%d", options->upload_limit());
        
    options->set_max_connections(_configure->connection_limit());
    DEBUG_LOG("auto add task's max connections is:%d", options->max_connections());
        
    task_info->set_infohash(_configure->infohash());
    DEBUG_LOG("auto add task's infohash is:%s", task_info->infohash().c_str());

    task_info->set_save_path(Path::trim(Path::absolute(_configure->save_path())));
    DEBUG_LOG("auto add task's save path is:%s", task_info->save_path().c_str());

    task_info->set_seeding_time(seeding_time);
    DEBUG_LOG("auto add task's seeding time is:%d", task_info->seeding_time());

    task_info->set_type(message::SEEDING_TASK);

    int64_t taskid = -1;
    BBTSClient client;
    if (client.create_task(add_task_params, &taskid) != 0) {
        return false;
    }
    NOTICE_LOG("auto add for %d seconds, taskid:%ld\n", seeding_time, taskid);
    return true;
}

bool Downloader::init() {
    set_signal_action();
    if (!_stat.open_peer_file(_configure->peer_stat_file())) {
        WARNING_LOG("Open peer stat file %s failed", _configure->peer_stat_file().c_str());
    }

    if (_configure->tracker_size() == 0) {
        const string &machine_room = g_process_info->machine_room();
        if (!_thrift_tracker.load_conf(_configure->routing_conf_file(), machine_room)) {
            if (!_thrift_tracker.load_conf(
                    g_process_info->root_dir() + "/conf/routing.conf", machine_room)) {
                FATAL_LOG("load thrift trackers fail");
                return false;
            }
        }
    }
    _torrent_provider.set_routing_conf(_configure->routing_conf_file());
    _stat.set_routing_conf(_configure->routing_conf_file());

    if (!check_and_add_source_peers(false)) {
        return false;
    }

    _session.reset(new libtorrent::session(
            libtorrent::fingerprint("GK", 3, 0, 0, 0),
            make_pair(_configure->listen_port_start(), _configure->listen_port_end()),
            "0.0.0.0", 0,
            alert::error_notification | alert::peer_notification |
            alert::tracker_notification | alert::storage_notification |
            alert::status_notification | alert::performance_warning |
            alert::ip_block_notification | alert::debug_notification));
    session_settings settings = libtorrent::high_performance_seed();
    settings.use_php_tracker = false;
    settings.use_c_tracker = true;
    settings.stop_tracker_timeout = 1;
    settings.no_connect_privileged_ports = false;
    settings.allow_multiple_connections_per_ip = true;
    settings.alert_queue_size = 50000;
    settings.num_want = _configure->peers_num_want();
    settings.low_prio_disk = false;
    settings.lock_disk_cache = _configure->lock_disk_cache();
    if (_configure->suggest_mode()) {
        settings.explicit_read_cache = true;
        settings.suggest_mode = session_settings::suggest_read_cache;
    }
    settings.disk_io_read_mode = _configure->use_dio_read() ?
            session_settings::disable_os_cache : session_settings::enable_os_cache;
    settings.disk_io_write_mode = _configure->use_dio_write() ?
            session_settings::disable_os_cache : session_settings::enable_os_cache;
    settings.max_metadata_size = _configure->max_metadata_size();
    settings.min_announce_interval = _configure->seeding_announce_interval();
    settings.max_announce_interval = _configure->max_announce_interval();
    settings.min_reconnect_time = _configure->min_reconnect_time();
    settings.peer_connect_timeout = _configure->peer_connect_timeout();
    settings.max_queued_disk_bytes = _configure->max_queued_disk_bytes();
    settings.max_out_request_queue = _configure->max_out_request_queue();
    settings.max_allowed_in_request_queue = _configure->max_allowed_in_request_queue();
    settings.whole_pieces_threshold = _configure->whole_pieces_threshold();
    settings.request_queue_time = _configure->request_queue_time();
    settings.cache_size = _configure->cache_size();
    settings.cache_expiry = _configure->cache_expiry();
    settings.read_cache_line_size = _configure->read_cache_line_size();
    settings.write_cache_line_size = _configure->write_cache_line_size();
    settings.file_pool_size = _configure->file_pool_size();
    settings.send_buffer_watermark = _configure->send_buffer_watermark();
    settings.send_buffer_low_watermark = _configure->send_buffer_low_watermark();
    settings.send_socket_buffer_size = _configure->send_socket_buffer_size();
    settings.recv_socket_buffer_size = _configure->recv_socket_buffer_size();
    settings.dont_count_slow_torrents = true;
    settings.active_seeds = 1;
    settings.active_downloads = 1;
    settings.active_limit = 1;
    settings.ignore_limits_on_local_network = false;
    settings.enable_outgoing_utp = false;
    settings.enable_incoming_utp = false;
    settings.choking_algorithm = session_settings::auto_expand_choker;
    settings.seed_choking_algorithm = session_settings::round_robin;
    settings.unchoke_slots_limit = 1000;
    settings.ignore_resume_timestamps = true;
    settings.anonymous_mode = false;
    settings.disable_hash_checks = _configure->disable_hash_check();
    settings.max_failcount = 2;
    //settings.no_recheck_incomplete_resume = true;
    _session->set_settings(settings);
    _session->add_extension(&libtorrent::create_metadata_plugin);
    _session->add_extension(&create_torrent_stat_plugin);
    if (_configure->enable_dynamic_hash_check()) {
        _session->add_extension(&create_piece_hash_plugin);
        _session->add_extension(&create_hash_check_torrent_source_plugin);
    } else {
        _session->add_extension(&create_no_check_torrent_source_plugin);
    }
    ip_filter filter;
    if (!get_ip_filter(&filter)) {
        return false;
    }
    _session->set_ip_filter(filter);
    _session->add_ex_announce_func(bind(&ThriftTracker::announce, &_thrift_tracker, _1, _2));

    _stat.get_task_info().__set_ip(g_process_info->ip());
    _stat.get_task_info().__set_port(_session->listen_port());
    _stat.get_task_info().__set_host_name(g_process_info->hostname());
    _stat.get_task_info().__set_download_limit(_configure->download_limit());
    _stat.get_task_info().__set_upload_limit(_configure->upload_limit());
    _thrift_tracker.set_peerid(_session->id().to_string());
    _thrift_tracker.set_no_p2p(_configure->no_p2p());
    NOTICE_LOG("[version: %s][build date: %s][local ip: %s][pid: %s]", GINGKO_VERSION, BUILD_DATE,
            g_process_info->ip().c_str(), libtorrent::to_hex(_session->id().to_string()).c_str());
    return true;
}

int Downloader::download() {
    if (!check_download_configure(*_configure)) {
        return 1;
    }

    if (!init()) {
        return -1;
    }
    if (!add_download_task()) {
        FATAL_LOG("add download task fail.");
        if (_torrent_provider.is_file_no_found()) {
            return -25;
        }
        return -1;
    }
    if (!_control_server.start(_torrent)) {
        FATAL_LOG("start control server failed.");
        return -1;
    }
    int ret = 0;
    _thrift_tracker.set_announce_callback(bind(&Downloader::announce_callback, this, _1, &ret));
    start_download();
    error_code ec;
    _io_service.post(bind(&Downloader::process_alert, this, &ret, true));
    try {
        _io_service.run(ec);
    } catch (libtorrent_exception &e) {
        FATAL_LOG("exception: %s, torrent maybe removed", e.what());
        ret = -1;
    } catch (std::exception &e) {
        FATAL_LOG("exception: %s, torrent maybe removed", e.what());
        ret = -1;
    }

    if (_check_download_finished < 0) {
        ret = _check_download_finished;
    }

    if (ret != 0) {
        FATAL_LOG("download fail: %d", ret);
    } else {
        NOTICE_LOG("download success.");
        if (!_configure->dont_delete_resume_file()) {
            _resume.remove();
        }
        rmdir(_tmp_save_path.c_str());
        if (_meta_msg) {
            BBTSClient client;
            client.add_metadata(_meta_msg.get());    //忽略返回值
        }
        if (_configure->has_auto_add_time()) {
            NOTICE_LOG("set auto add time, begin to add task");
            if (!auto_add_task(_configure->auto_add_time())) {
                WARNING_LOG("create auto add task failed!");
            }
        }
    }
    _stat.get_task_info().__set_retval(ret);
    _stat.get_task_info().__set_end_time(time(NULL));

    // print to log
    _stat.print_task_statistics(_configure->task_stat_file());

    DEBUG_LOG("set task info to class");
    // runitao: no stat component, so disable send_stat() to suppress error log
    // _stat.send_stat();
    return ret;
}

}  // namespace tool
}  // namespace bbts
