/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   subnet_mask.h
 *
 * @author liuming03
 * @date   2014-4-10
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_SUBNET_MASK_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_SUBNET_MASK_H

#include <string>

namespace bbts {

bool parse_subnet_mask(const std::string &ip, const std::string &mask, std::string *range);

}  // namespace bbts

#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_SUBNET_MASK_H
