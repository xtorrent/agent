/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   string_util.h
 *
 * @author liuming03
 * @date   2013-12-30
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_STRING_UTIL_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_STRING_UTIL_H

#include <stdint.h>

#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace bbts {

class StringUtil {
public:
    static bool start_with(const std::string &str, const std::string &prefix);

    static bool end_with(const std::string &str, const std::string &suffix);

    template <typename T>
    static std::string to_string(T num) {
        std::stringstream strm;
        strm << num;
        return strm.str();
    }

    static std::string bytes_to_readable(int64_t bytes);

    static int64_t readable_to_bytes(const std::string &bytes_string, int64_t unit);
    static inline int64_t readable_to_bytes(const std::string &bytes_string) {
        return readable_to_bytes(bytes_string, 1024LL * 1024LL);
    }

    static void slipt(
            const std::string &str,
            const std::string &delimiter,
            std::vector<std::string> *v);

    static void slipt(
            const std::string &str,
            const std::string &delimiter,
            std::set<std::string> *s);

private:
    StringUtil();
    ~StringUtil();
};

}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_STRING_UTIL_H
