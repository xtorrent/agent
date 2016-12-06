/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   downloader.h
 *
 * @author liuming03
 * @date   2013-7-26
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_DOWNLOADER_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_DOWNLOADER_H

#include <boost/asio/io_service.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/noncopyable.hpp>
#include <libtorrent/alert_types.hpp>

#include "bbts/bbts_stat.h"
#include "bbts/log.h"
#include "bbts/statistics.hpp"
#include "bbts/thrift_tracker.h"
#include "bbts/tool/control_server.h"
#include "bbts/tool/disk_allocate_thread.h"
#include "bbts/tool/resume.h"
#include "bbts/torrent_provider.h"
#include "configure.pb.h"
#include "message.pb.h"

namespace libtorrent {
class ip_filter;
class session;
class ex_announce_response;
class torrent_info;
}

namespace bbts {
namespace tool {

class ClusterDownloader;

/**
 * @brief
 */
class Downloader : public boost::noncopyable {
public:
    Downloader(message::DownloadConfigure *configure);
    ~Downloader();
    int download();

private:
    struct SourcePeerInfo {
        SourcePeerInfo()
            : disconnect_time(0),
              is_connect_success(false),
              is_failed(false) {}
        std::string path;
        int64_t disconnect_time;
        bool is_connect_success;
        bool is_failed;
    };

    bool init();
    bool add_download_task();
    bool add_torrent_file(const std::vector<char> &buffer,
            libtorrent::add_torrent_params *torrent_params);
    void slip_files_to_muti_paths(libtorrent::torrent_info *ti,
            const std::vector<int> &file_priorities);
    void start_download();
    void process_alert(int *retval, bool loop);
    bool correct_mode_and_symlinks();
    void to_seeding();
    void remove_torrent();
    void check_and_request_transfer(const std::string &source_host, const std::string &uri);
    void save_torrent_file_by_data(const std::vector<char> &buffer);
    void save_torrent_file(bool dynamic_hash_check);
    void save_infohash_file();
    bool check_and_add_source_peers(bool is_from_torrent_provider);
    bool auto_add_task(int32_t seeding_time);

    bool is_patition_download() const {
        return _configure->include_regex_size() != 0 || _configure->exclude_regex_size() != 0;
    }

    bool use_dynamic_disk_allocate() const {
        return _configure->dynamic_allocate() >= 0;
    }

    void get_trackers(std::vector<std::pair<std::string, int> > *trackers);
    bool get_ip_filter(libtorrent::ip_filter *filter);

    void announce_callback(const boost::shared_ptr<libtorrent::ex_announce_response> &response, 
            int *retval);
    void seeding_complete_callback();
    void timeout_callback();
    void quit_check_callback();
    void hang_check_callback();
    void progress_callback();

    bool on_torrent_finished();
    bool on_state_changed(const libtorrent::state_changed_alert *alert);
    void on_metadata_received();
    void on_hash_failed(const libtorrent::hash_failed_alert *alert);
    void on_piece_finished(const libtorrent::piece_finished_alert *alert);
    void on_save_resume_data(const libtorrent::save_resume_data_alert *alert);
    bool on_storage_moved();
    void on_peer_disconnect(const libtorrent::peer_disconnected_alert *alert);
    void on_peer_connect(const libtorrent::peer_connect_alert *alert);
    void on_peer_source_failed(const PeerSourceRequestFailedAlert *alert);
    bool on_peer_stat_alert(const PeerStatAlert *alert);

    static const int64_t kMaxSourceDisconnectTime = 10;

    message::DownloadConfigure *_configure;
    boost::scoped_ptr<libtorrent::session> _session;
    libtorrent::torrent_handle _torrent;
    volatile bool _should_stop;
    boost::asio::io_service _io_service;
    ThriftTracker _thrift_tracker;
    boost::asio::deadline_timer _seeding_timer;
    boost::asio::deadline_timer _timeout_timer;
    boost::asio::deadline_timer _quit_check_timer;
    bool _is_timeout :1;
    bool _is_mem_exceed :1;
    bool _is_source_quit :1;
    boost::scoped_ptr<ClusterDownloader> _cluster_downloader;

    std::map<boost::asio::ip::tcp::endpoint, SourcePeerInfo> _source_peers;

    // hang住检测以及下载速率效率800KB/s
    boost::asio::deadline_timer _hang_check_timer;
    time_t _last_check_time;
    int64_t _last_check_downloaded;
    int _low_downrate_times;

    // print progress
    boost::asio::deadline_timer _progress_timer;

    // tmp save path
    std::string _tmp_save_path;

    Resume _resume;

    // if has sent transfer request
    bool _is_request_transfer_server;

    // Tell Agent
    boost::scoped_ptr<message::Metadata> _meta_msg;

    DiskAllocateThread _disk_allocate_thread;

    ControlServer _control_server;

    BbtsStat _stat;

    TorrentProvider _torrent_provider;
    // check if download can finished and store finished value
    // if 1, means can not finish, this is default value
    // if <= 0, means should finish, and return this value for this download
    int _check_download_finished;
};

}  // namespace tool
}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_DOWNLOADER_H
