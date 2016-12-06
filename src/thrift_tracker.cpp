/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   thrift_tracker.cpp
 *
 * @author liuming03
 * @date   2013-9-12
 * @brief 
 */

#include "bbts/thrift_tracker.h"

#include <netinet/in.h>
#include <sys/socket.h>

#include <sstream>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/Thrift.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TSocket.h>

#include "bbts/log.h"
#include "bbts/process_info.h"
#include "Announce.h"

using std::string;
using std::vector;
using std::make_pair;
using boost::asio::io_service;
using boost::shared_ptr;
using bbts::tracker::AnnounceRequest;
using bbts::tracker::AnnounceResponse;
using bbts::tracker::Status;
using libtorrent::ex_announce_request;
using libtorrent::ex_announce_response;
using libtorrent::torrent_handle;

namespace bbts {

static void default_announce_callback(const shared_ptr<ex_announce_response> &) {}

ThriftTracker::ThriftTracker(io_service& ios) :
        _ios(ios),
        _have_seed(false),
        _no_p2p(false),
        _announce_callback(boost::bind(&default_announce_callback, _1)) {
    set_service_name("tracker");
}

ThriftTracker::~ThriftTracker() {}

void ThriftTracker::announce(const torrent_handle &handle, shared_ptr<ex_announce_request> req) {
    shared_ptr<AnnounceRequest> request(new AnnounceRequest());
    request->__set_infohash(req->infohash);
    request->peer.__set_ip(req->peer.ip);
    request->peer.__set_port(req->peer.port);
    request->peer.__set_peerid(req->peer.peerid);
    request->peer.__set_idc(g_process_info->machine_room());
    request->__isset.peer = true;
    request->stat.__set_uploaded(req->uploaded);
    request->stat.__set_downloaded(req->downloaded);
    request->stat.__set_left(req->left);
    request->__isset.stat = true;
    request->__set_is_seed(req->is_seed);
    request->__set_num_want(req->num_want);
    // request->__set_is_transfer(req->is_transfer);
    switch (req->status) {
    case ex_announce_request::DOWNLOAD:
        request->stat.__set_status(Status::DOWNLOAD);
        break;

    case ex_announce_request::METADATA:
        request->stat.__set_status(Status::METADATA);
        break;

    case ex_announce_request::SEEDING:
        request->stat.__set_status(Status::SEEDING);
        break;

    case ex_announce_request::PAUSED:
        case ex_announce_request::STOPPED:
        request->stat.__set_status(Status::STOPPED);
        break;

    default:
        break;
    }
    // libtorrent 内置主线程，不要堵塞其，发送给我们自己的线程来处理announce
    _ios.post(boost::bind(&ThriftTracker::on_announce, this, handle, req->trackers, request));
}

void ThriftTracker::on_announce(
        const torrent_handle &handle,
        const NodeVector &trackers,
        const shared_ptr<AnnounceRequest> &request) const {
    using apache::thrift::protocol::TBinaryProtocol;
    using apache::thrift::transport::TSocket;
    using apache::thrift::transport::TFramedTransport;
    using apache::thrift::TException;
    using bbts::tracker::Peer;
    using bbts::tracker::AnnounceClient;
    using libtorrent::ex_announce_peer;

    TRACE_LOG("[%s][seed:%d][state:%d][uploaded:%lld][downloaded:%lld][left:%lld][num_want:%d]",
            libtorrent::to_hex(request->infohash).c_str(),
            request->is_seed,
            request->stat.status,
            request->stat.uploaded,
            request->stat.downloaded,
            request->stat.left,
            request->num_want);

    shared_ptr<AnnounceResponse> response(new AnnounceResponse());
    bool announce_success = false;
    std::stringstream strm;
    for (NodeVector::const_iterator it = trackers.begin(); it != trackers.end(); ++it) {
        shared_ptr<TSocket> socket(new TSocket(it->first, it->second));
        socket->setConnTimeout(3000);
        socket->setSendTimeout(3000);
        socket->setRecvTimeout(5000);
        shared_ptr<TFramedTransport> transport(new TFramedTransport(socket));
        shared_ptr<TBinaryProtocol> protocol(new TBinaryProtocol(transport));
        AnnounceClient client(protocol);
        try {
            transport->open();
            client.announce((*response.get()), (*request.get()));
            transport->close();
            announce_success = true;
            if (response->ret == 0) {
                TRACE_LOG("[tracker:%s:%d][recv:%d][have_seed:%d]",
                        it->first.c_str(), it->second, response->peers.size(), response->have_seed);
            } else {
                WARNING_LOG("[tracker:%s:%d][fail:%s]",
                        it->first.c_str(), it->second, response->failure_reason.c_str());
            }
            break;
        } catch (TException &tx) {
            WARNING_LOG("[tracker:%s:%d][fail:%s]", it->first.c_str(), it->second, tx.what());
        }
    }

    shared_ptr<ex_announce_response> res(new ex_announce_response);
    if (!announce_success) {
        res->ret = 10000;
        res->failure_reason = "announce to tracker failed";
    } else {
        if (response->ret == 20000) {
            _no_p2p = true;
        }
        res->ret = response->ret;
        res->failure_reason = response->failure_reason;
        res->have_seed = _have_seed = response->have_seed;
        res->min_interval = response->min_interval;
        res->peers.resize(response->peers.size());
        vector<Peer>::iterator it = response->peers.begin();
        for (int i = 0; it != response->peers.end(); ++it, ++i) {
            ex_announce_peer &peer = res->peers[i];
            peer.ip = it->ip;
            peer.peerid = it->peerid;
            peer.port = it->port;
        }
    }
    _announce_callback(res);
    handle.recv_ex_tracker_announce_reply(res);
}

}  // namespace bbts
