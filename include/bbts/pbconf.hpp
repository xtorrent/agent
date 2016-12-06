/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/*
 * @file   pb_conf.h
 *
 * @author liuming03
 * @date   2013-4-9
 * @brief  通过protobuf配置文件加载配置
 */

#ifndef  OP_OPED_NOAH_BBTS_AGENT_PBCONF_H
#define  OP_OPED_NOAH_BBTS_AGENT_PBCONF_H

#include <errno.h>
#include <fcntl.h>

#include <string>

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include "bbts/log.h"

namespace bbts {

inline bool load_pbconf(const std::string &conf_file, google::protobuf::Message *conf) {
    int conf_fd = open(conf_file.c_str(), O_RDONLY);
    if (conf_fd < 0) {
        DEBUG_LOG("open conf %s failed: %d", conf_file.c_str(), errno);
        return false;
    }

    google::protobuf::io::FileInputStream ifs(conf_fd);
    ifs.SetCloseOnDelete(true);
    if (!google::protobuf::TextFormat::Parse(&ifs, conf)) {
        DEBUG_LOG("parse conf %s failed", conf_file.c_str());
        return false;
    }
    return true;
}

}  // namespace bbts

#endif  // OP_OPED_NOAH_BBTS_AGENT_PBCONF_H
