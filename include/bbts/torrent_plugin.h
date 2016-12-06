/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file torrent_plugin.h
 *
 * @author liuming03
 * @date 2013-7-31
 * @brief 
 */

#ifndef OP_OPED_NOAH_BBTS_AGENT_TORRENT_PLUGIN_H
#define OP_OPED_NOAH_BBTS_AGENT_TORRENT_PLUGIN_H

#include <stdint.h>

#include <boost/shared_ptr.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/extensions.hpp>

namespace bbts {

class LibtorrentPlugin : public libtorrent::plugin {
public:
    LibtorrentPlugin(int release_file_interval) :
        _tick_count(0),
        _release_file_interval(release_file_interval) {}

    virtual ~LibtorrentPlugin() {}

    virtual void added(boost::weak_ptr<libtorrent::aux::session_impl> impl) {
        _impl = impl;
    }

    virtual void on_tick();

private:
    boost::weak_ptr<libtorrent::aux::session_impl> _impl;
    uint64_t _tick_count;
    int _release_file_interval;
};

boost::shared_ptr<libtorrent::torrent_plugin>
create_torrent_stat_plugin(libtorrent::torrent *torrent, void *args);

boost::shared_ptr<libtorrent::torrent_plugin>
create_no_check_torrent_source_plugin(libtorrent::torrent *torrent, void *args);

boost::shared_ptr<libtorrent::torrent_plugin>
create_hash_check_torrent_source_plugin(libtorrent::torrent *torrent, void *args);

boost::shared_ptr<libtorrent::torrent_plugin>
create_piece_hash_plugin(libtorrent::torrent *torrent, void *args);

class PeerStatAlert : public libtorrent::alert {
public:
    PeerStatAlert(const libtorrent::sha1_hash &infohash,
            const libtorrent::tcp::endpoint &ep,
            time_t start_time,
            int64_t uploaded,
            int64_t downloaded) :
            _infohash(infohash),
            _remote(ep),
            _start_time(start_time),
            _end_time(time(NULL)),
            _uploaded(uploaded),
            _downloaded(downloaded) {}

    PeerStatAlert(const libtorrent::sha1_hash &infohash,
            time_t start_time,
            int64_t uploaded,
            int64_t downloaded,
            std::string cluster_uri) :
            _infohash(infohash),
            _start_time(start_time),
            _end_time(time(NULL)),
            _uploaded(uploaded),
            _downloaded(downloaded),
            _cluster_uri(cluster_uri) {}

    virtual ~PeerStatAlert() {}

    virtual int type() const {
        return ALERT_TYPE;
    }

    virtual std::auto_ptr<alert> clone() const {
        return std::auto_ptr<alert>(new PeerStatAlert(*this));
    }

    virtual int category() const {
        return static_category;
    }

    virtual char const* what() const {
        return "peer_stat_alert";
    }

    virtual std::string message() const;

    std::string infohash() const; 

    std::string remote_host() const;

    std::string local_host() const;

    time_t start_time() const {
        return _start_time;
    }

    time_t end_time() const {
        return _end_time;
    }

    int64_t uploaded() const {
        return _uploaded;
    }

    int64_t downloaded() const {
        return _downloaded;
    }

    bool is_cluster() const {
        return !_cluster_uri.empty();
    }

    const libtorrent::tcp::endpoint& remote() const {
        return _remote;
    }

public:
    const static int static_category = libtorrent::alert::peer_notification;
    const static int ALERT_TYPE = libtorrent::user_alert_id + 1;

private:
    libtorrent::sha1_hash _infohash;
    libtorrent::tcp::endpoint _remote;
    time_t _start_time;
    time_t _end_time;
    int64_t _uploaded;
    int64_t _downloaded;
    std::string _cluster_uri;
};

class PeerSourceRequestFailedAlert : public libtorrent::peer_alert {
public:
    PeerSourceRequestFailedAlert(const libtorrent::torrent_handle &h,
            const libtorrent::tcp::endpoint &ep,
            const libtorrent::peer_id &peer_id,
            const std::string &error) :
                peer_alert(h, ep, peer_id),
                _error(error) {}

    virtual ~PeerSourceRequestFailedAlert() {}

    virtual std::string message() const {
        return peer_alert::message() + " peer error: " + _error.c_str();
    }

    virtual int type() const {
        return ALERT_TYPE;
    }

    virtual std::auto_ptr<alert> clone() const {
        return std::auto_ptr<alert>(new PeerSourceRequestFailedAlert(*this));
    }

    virtual int category() const {
        return static_category;
    }

    virtual char const* what() const {
        return "peer_source_request_alert";
    }

public:
    const static int static_category = libtorrent::alert::peer_notification;
    const static int ALERT_TYPE = libtorrent::user_alert_id + 2;

private:
    std::string _error;
};

}  // namespace bbts

#endif // OP_OPED_NOAH_BBTS_AGENT_TORRENT_PLUGIN_H
