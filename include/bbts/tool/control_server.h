/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   control_server.h
 *
 * @author liuming03
 * @date   2015-1-19
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_CONTROL_SERVER_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_CONTROL_SERVER_H

#include <vector>

#include <boost/shared_ptr.hpp>
#include <libtorrent/torrent_handle.hpp>

#include "bbts/unix_socket_server.h"
#include "configure.pb.h"

namespace bbts {
namespace tool {

/**
 * @brief
 */
class ControlServer {
public:
    ControlServer(message::DownloadConfigure *configure, boost::asio::io_service &io_service);

    ~ControlServer();

    bool start(const libtorrent::torrent_handle &_torrent);

    void stop();

private:
    void control_handler(const boost::shared_ptr<UnixSocketConnection> &connection,
            const boost::shared_ptr<const std::vector<char> > &data);

    void list_task(const boost::shared_ptr<UnixSocketConnection> &connection);

    message::DownloadConfigure *_configure;
    UnixSocketServer _server;
    libtorrent::torrent_handle _torrent;
};

} // namespace tool
} // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_CONTROL_SERVER_H
