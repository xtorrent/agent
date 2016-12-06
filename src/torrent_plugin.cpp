/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file torrent_plugin.cpp
 *
 * @author liuming03
 * @date 2013-7-31
 * @brief 
 */

#include "bbts/torrent_plugin.h"

#include <libtorrent/bt_peer_connection.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/torrent.hpp>

#include "bbts/path.h"
#include "bbts/process_info.h"

using std::map;
using std::pair;
using std::string;
using std::vector;

using boost::shared_ptr;
using boost::system::error_code;
using boost::weak_ptr;
using libtorrent::aux::session_impl;
using libtorrent::file_slice;
using libtorrent::peer_connection;
using libtorrent::peer_plugin;
using libtorrent::peer_request;
using libtorrent::tcp;
using libtorrent::torrent;
using libtorrent::torrent_info;
using libtorrent::torrent_plugin;
using libtorrent::detail::write_string;
using libtorrent::detail::write_uint8;
using libtorrent::detail::write_uint32;
using libtorrent::detail::write_uint64;

namespace bbts {

void LibtorrentPlugin::on_tick() {
    if (++_tick_count % _release_file_interval == 0) {
        shared_ptr<session_impl> impl = _impl.lock();
        if (impl) {
            impl->release_files();
        }
    }
}

string PeerStatAlert::message() const {
    char ih_hex[41];
    libtorrent::to_hex((const char*)&_infohash[0], 20, ih_hex);
    error_code ec;
    string address = _cluster_uri.empty() ? _remote.address().to_string(ec) : _cluster_uri;
    char msg[200];
    snprintf(msg, sizeof(msg), "%s,%s,%s,%ld,%ld,%ld,%ld,%d", ih_hex, g_process_info->ip().c_str(),
            address.c_str(), _start_time, _end_time, _uploaded, _downloaded, !_cluster_uri.empty());
    return msg;
}

string PeerStatAlert::infohash() const {
    char ih_hex[41];
    libtorrent::to_hex((const char*)&_infohash[0], 20, ih_hex);
    return ih_hex;
}

string PeerStatAlert::local_host() const {
    return g_process_info->ip();
}

string PeerStatAlert::remote_host() const {
    error_code ec;
    return _cluster_uri.empty() ? _remote.address().to_string(ec) : _cluster_uri;
}

/*********************************************/
class TorrentStatPlugin : public torrent_plugin {
public:
    TorrentStatPlugin(torrent& t) : _torrent(t) {}
    virtual ~TorrentStatPlugin() {}
    virtual shared_ptr<peer_plugin> new_connection(peer_connection* pc);

private:
    torrent& _torrent;
};

class PeerStatPlugin : public peer_plugin {
public:
    PeerStatPlugin(peer_connection& pc, torrent& t) :
        _start_time(time(NULL)),
        _peer_connection(pc),
        // _torrent(t),
        _infohash(t.info_hash()),
        _session(t.session()) {}

    virtual ~PeerStatPlugin() {
        error_code ec;
        if (_session.m_alerts.should_post<PeerStatAlert>()) {
            _session.m_alerts.post_alert(
                    PeerStatAlert(_infohash,
                    _peer_connection.remote(),
                    _start_time,
                    _peer_connection.statistics().total_payload_upload(),
                    _peer_connection.statistics().total_payload_download()));
        }
    }

    virtual void on_piece_failed(int index) {
        _peer_connection.disconnect(libtorrent::errors::too_many_corrupt_pieces);
    }

private:
    time_t _start_time;
    peer_connection& _peer_connection;
    // torrent& _torrent;
    // with torrent not a smart ptr, maybe core by destruct
    // so we must get session and infohash first
    libtorrent::sha1_hash _infohash;
    libtorrent::aux::session_impl &_session;
};

shared_ptr<peer_plugin> TorrentStatPlugin::new_connection(peer_connection *pc) {
    if (pc->type() != peer_connection::bittorrent_connection) {
        return shared_ptr<peer_plugin>();
    }
    return shared_ptr<peer_plugin>(new PeerStatPlugin(*pc, _torrent));
}

shared_ptr<torrent_plugin> create_torrent_stat_plugin(torrent *t, void *args) {
    return shared_ptr<torrent_plugin>(new TorrentStatPlugin(*t));
}

/**************************************************************************************************/

class SourcePeerPlugin : public peer_plugin {
public:
    enum MessageType {
        MESSAGE_URI_PIECE_REQUEST = 1,
        MESSAGE_URI_PIECE_FAILED = 2,
        MESSAGE_URI_PIECE_HASH_REQUEST  = 3,
        MESSAGE_URI_PIECE_HASH_RESPONSE  = 4,
    };

    enum MessageIndex {
        MESSAGE_INDEX = 20
    };

    SourcePeerPlugin(peer_connection& pc, torrent& t, string source_path, bool dynamic_hash_check) :
        _peer_connection(pc),
        _torrent(t),
        _source_path(source_path),
        _dynamic_hash_check(dynamic_hash_check),
        _tick_once(false) {
    }

    virtual ~SourcePeerPlugin() {}

    virtual void tick() {
        if (!_tick_once && _torrent.valid_metadata()) {
            _tick_once = true;
            _peer_connection.set_upload_only(true);
            _peer_connection.incoming_have_all();
            _peer_connection.incoming_unchoke();
        }
    }

    virtual void add_handshake(libtorrent::entry &e) {
        const torrent_info& ti = _torrent.torrent_file();
        libtorrent::entry &message = e["m"];
        message["piece_length"] = ti.piece_length();
        message["root_path"] = _source_path;
    }

    virtual bool write_request(peer_request const& r) {
        if (_dynamic_hash_check && r.start == 0) {
            write_request_with_type(MESSAGE_URI_PIECE_HASH_REQUEST, r);
        }

        return write_request_with_type(MESSAGE_URI_PIECE_REQUEST, r);
    }

    virtual bool on_extended(int length, int msg, libtorrent::buffer::const_interval body) {
        if (MESSAGE_INDEX != msg) {
            return false;
        }

        if (body.left() < 1) {
            return true;
        }
        int type = libtorrent::detail::read_uint8(body.begin);

        switch (type) {
        case MESSAGE_URI_PIECE_FAILED: {// failed
            if (body.left() < length - 1) {
                return true;
            }
            session_impl &impl = _torrent.session();
            if (impl.m_alerts.should_post<PeerSourceRequestFailedAlert>()) {
                impl.m_alerts.post_alert(PeerSourceRequestFailedAlert(
                        _torrent.get_handle(),
                        _peer_connection.remote(),
                        _peer_connection.pid(),
                        string(body.begin, body.end)));
            }
            _peer_connection.peer_info_struct()->connectable = false;
            _torrent.remove_peer(&_peer_connection);
            _peer_connection.disconnect(libtorrent::errors::invalid_request);
            break;
        }

        case MESSAGE_URI_PIECE_HASH_RESPONSE: {
            if (body.left() < 24) {
                return true;
            }
            int piece = libtorrent::detail::read_uint32(body.begin);
            libtorrent::sha1_hash hash(body.begin);
            _torrent.set_piece_hash(piece, hash);
            break;
        }

        default:
            _peer_connection.disconnect(libtorrent::errors::invalid_request, 2);
            break;
        }
        return true;
    }

private:
    bool write_request_with_type(int type, peer_request const &r) {
        const torrent_info& ti = _torrent.torrent_file();
        int length = r.length;
        int start = r.start;
        if (type == MESSAGE_URI_PIECE_HASH_REQUEST) {
            length = ti.piece_size(r.piece);
            start = 0;
        }
        vector<file_slice> files = ti.map_block(r.piece, start, length);

        vector<char> msg_buffer;
        std::back_insert_iterator<vector<char> > ptr(msg_buffer);
        write_uint32(0, ptr);
        write_uint8(libtorrent::bt_peer_connection::msg_extended, ptr);
        write_uint8(MESSAGE_INDEX, ptr);
        write_uint8(type, ptr);
        write_uint32(r.piece, ptr);
        write_uint32(start, ptr);
        for (vector<file_slice>::iterator i = files.begin(); i != files.end(); ++i) {
            write_string(Path::subpath(ti.file_at(i->file_index).path), ptr);
            write_uint8('\0', ptr);
            write_uint64(i->offset, ptr);
            write_uint32(i->size, ptr);
        }

        char *p = &msg_buffer[0];
        write_uint32(msg_buffer.size() - 4, p);
        _peer_connection.send_buffer(&msg_buffer[0], msg_buffer.size());
        _peer_connection.setup_send();
        return true;
    }

    peer_connection& _peer_connection;
    torrent& _torrent;
    string _source_path;
    bool _dynamic_hash_check;
    bool _tick_once;
};

class TorrentSourcePlugin : public torrent_plugin {
public:
    TorrentSourcePlugin(torrent& t, bool hash_check, const map<tcp::endpoint, string> &source_peers) :
        _torrent(t),
        _dynamic_hash_check(hash_check),
        _source_peers(source_peers) {}

    virtual ~TorrentSourcePlugin() {}

    virtual shared_ptr<peer_plugin> new_connection(peer_connection* pc) {
        shared_ptr<peer_plugin> empty_peer_plugin;
        if (pc->type() != peer_connection::bittorrent_connection) {
            return empty_peer_plugin;
        }
        map<tcp::endpoint, string>::const_iterator i = _source_peers.find(pc->remote());
        if (i == _source_peers.end()) {
            return empty_peer_plugin;
        }
        return shared_ptr<peer_plugin>(new SourcePeerPlugin(*pc, _torrent, i->second, _dynamic_hash_check));
    }

private:
    torrent& _torrent;
    bool _dynamic_hash_check;
    const map<tcp::endpoint, string> &_source_peers;
};

shared_ptr<torrent_plugin> create_no_check_torrent_source_plugin(torrent *t, void *args) {
    map<tcp::endpoint, string> *source_peers = (map<tcp::endpoint, string> *)args;
    return shared_ptr<torrent_plugin>(new TorrentSourcePlugin(*t, false, *source_peers));
}

shared_ptr<torrent_plugin> create_hash_check_torrent_source_plugin(torrent *t, void *args) {
    map<tcp::endpoint, string> *source_peers = (map<tcp::endpoint, string> *)args;
    return shared_ptr<torrent_plugin>(new TorrentSourcePlugin(*t, true, *source_peers));
}

/**************************************************************************************************/

class PieceHashPeerPlugin : public peer_plugin {
public:
    enum MessageType {
        MESSAGE_PIECE_HASH_REQUEST = 1,
        MESSAGE_PIECE_HASH_RESPONSE = 2,
    };

    enum MessageIndex {
        MESSAGE_INDEX = 40
    };

    PieceHashPeerPlugin(peer_connection& pc, torrent& t) :
        _peer_connection(pc),
        _torrent(t) {}

    virtual ~PieceHashPeerPlugin() {}

    virtual bool write_request(peer_request const& r) {
        if (r.start != 0) {
            return false;
        }

        vector<char> msg_buffer;
        std::back_insert_iterator<vector<char> > ptr(msg_buffer);
        write_uint32(0, ptr);
        write_uint8(libtorrent::bt_peer_connection::msg_extended, ptr);
        write_uint8(MESSAGE_INDEX, ptr);
        write_uint8(MESSAGE_PIECE_HASH_REQUEST, ptr);
        write_uint32(r.piece, ptr);
        char *p = &msg_buffer[0];
        write_uint32(msg_buffer.size() - 4, p);
        _peer_connection.send_buffer(&msg_buffer[0], msg_buffer.size());
        _peer_connection.setup_send();
        return false;
    }

    void write_piece_hash(int piece) {
        vector<char> msg_buffer;
        std::back_insert_iterator<vector<char> > ptr(msg_buffer);
        write_uint32(0, ptr);
        write_uint8(libtorrent::bt_peer_connection::msg_extended, ptr);
        write_uint8(MESSAGE_INDEX, ptr);
        write_uint8(MESSAGE_PIECE_HASH_RESPONSE, ptr);
        write_uint32(piece, ptr);
        write_string(_torrent.torrent_file().hash_for_piece(piece).to_string(), ptr);
        char *p = &msg_buffer[0];
        write_uint32(msg_buffer.size() - 4, p);
        _peer_connection.send_buffer(&msg_buffer[0], msg_buffer.size());
        _peer_connection.setup_send();
    }

    virtual bool on_extended(int length, int msg, libtorrent::buffer::const_interval body) {
        if (MESSAGE_INDEX != msg) {
            return false;
        }

        if (body.left() < 1) {
            return true;
        }
        int type = libtorrent::detail::read_uint8(body.begin);

        switch (type) {
        case MESSAGE_PIECE_HASH_REQUEST: {
            if (body.left() < 4) {
                return true;
            }
            int piece = libtorrent::detail::read_uint32(body.begin);
            write_piece_hash(piece);
            break;
        }

        case MESSAGE_PIECE_HASH_RESPONSE: {// failed
            if (body.left() < 24) {
                return true;
            }
            int piece = libtorrent::detail::read_uint32(body.begin);
            libtorrent::sha1_hash hash(body.begin);
            _torrent.set_piece_hash(piece, hash);
            break;
        }
        default:
            break;
        }
        return true;
    }

private:
    peer_connection& _peer_connection;
    torrent& _torrent;
};

class TorrentPieceHashPlugin : public torrent_plugin {
public:
    TorrentPieceHashPlugin(torrent& t) : _torrent(t) {}

    virtual ~TorrentPieceHashPlugin() {}

    virtual shared_ptr<peer_plugin> new_connection(peer_connection* pc) {
        if (pc->type() != peer_connection::bittorrent_connection) {
            return shared_ptr<peer_plugin>();
        }
        return shared_ptr<peer_plugin>(new PieceHashPeerPlugin(*pc, _torrent));
    }

private:
    torrent& _torrent;
};

shared_ptr<torrent_plugin> create_piece_hash_plugin(torrent *t, void *args) {
    return shared_ptr<torrent_plugin>(new TorrentPieceHashPlugin(*t));
}

}  // namespace bbts
