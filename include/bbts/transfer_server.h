/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   transfer_server.h
 *
 * @author hechaobin01 
 * @date   2015-4-20
 *
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_TRANSFER_SERVER_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_TRANSFER_SERVER_H

#include <string>
#include <vector>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

#include "transfer_server_types.h"
#include "TransferServer.h"
#include "bbts/routing.h"

namespace bbts {

/**
 * @brief
 */
class TransferServer : public Routing {
public:
    TransferServer();
    ~TransferServer();

    void request_transfer(const RequestTransferInfo &info);

private:
};

} // namespace bbts

#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_TRANSFER_SERVER_H
