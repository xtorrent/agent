/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   thrift_tracker.h
 *
 * @author liuming03
 * @date   2013-9-12
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_THRIFT_TRACKER_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_THRIFT_TRACKER_H

#include <boost/asio/io_service.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include "bbts/routing.h"

namespace libtorrent {
class torrent_handle;
class ex_announce_request;
class ex_announce_response;
}

namespace bbts {

namespace tracker {
class AnnounceRequest;
}

/**
 * @brief
 */
class ThriftTracker : public Routing {
public:
    typedef boost::function<
            void(const boost::shared_ptr<libtorrent::ex_announce_response> &)> AnnounceCallback;

    ThriftTracker(boost::asio::io_service& ios);
    virtual ~ThriftTracker();

    bool have_seed() const {
        return _have_seed;
    }

    void set_peerid(const std::string &peerid) {
        _peerid = peerid;
    }

    void set_no_p2p(bool no_p2p) {
        _no_p2p = no_p2p;
    }

    bool no_p2p() const {
        return _no_p2p; 
    }

    void set_announce_callback(const AnnounceCallback &announce_callback) {
        _announce_callback = announce_callback;
    }

    void announce(
            const libtorrent::torrent_handle &handle,
            boost::shared_ptr<libtorrent::ex_announce_request>);

private:
    void on_announce(
            const libtorrent::torrent_handle &handle,
            const NodeVector &trackers,
            const boost::shared_ptr<bbts::tracker::AnnounceRequest> &request) const;

    boost::asio::io_service& _ios;
    std::string _peerid;
    mutable bool _have_seed;
    mutable bool _no_p2p;
    AnnounceCallback _announce_callback;
};

}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_THRIFT_TRACKER_H
