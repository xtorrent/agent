/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   encode.h
 *
 * @author liuming03
 * @date   2013-1-24
 */

#ifndef OP_OPED_NOAH_BBTS_AGENT_ENCODE_H
#define OP_OPED_NOAH_BBTS_AGENT_ENCODE_H

#include <string>

namespace bbts {

bool base64_encode(const std::string &input, std::string *output);

bool base64_decode(const std::string &input, std::string *output);

bool hex_decode(const std::string &hex, std::string* bytes);

bool hex_encode(const std::string &bytes, std::string* hex);

bool hex_to_base64(const std::string &hex, std::string* base64str);

bool base64_to_hex(const std::string &base64str, std::string* hex);

}  // namespace bbts

#endif /* OP_OPED_NOAH_BBTS_AGENT_ENCODE_H */
