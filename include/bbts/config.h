/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   config.h
 *
 * @author liuming03
 * @date   2013-4-13
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_CONFIG_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_CONFIG_H

#ifndef GINGKO_VERSION
#define GINGKO_VERSION "unknow version"
#endif

namespace bbts {

#if defined(__DATE__) && defined(__TIME__)
static const char BUILD_DATE[] = __DATE__ " " __TIME__;
#else
static const char BUILD_DATE[] = "unknown"
#endif

enum {
    REQ_ADD_TASK = 1,
    REQ_TASK_GETOPT,
    REQ_TASK_SETOPT,
    REQ_AGENT_GETOPT,
    REQ_AGENT_SETOPT,
    REQ_BATCH_CTRL,
    REQ_STATUS,

    REQ_ADD_METADATA = 100,

    RES_BASE = 10000,
    RES_TASK,
    RES_TASK_STATUS,
    RES_BATCH_CTRL,
    RES_BATCH_LIST,
    RES_TASK_GETOPT,
    RES_AGENT_GETOPT,
    RES_STATUS,
};

} // namespace bbts

#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_CONFIG_H
