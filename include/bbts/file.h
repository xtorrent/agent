/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   file.h
 *
 * @author liuming03
 * @date   2014-12-29
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_FILE_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_FILE_H

#include <string>
#include <vector>

#include <boost/system/error_code.hpp>

namespace bbts {
class File {
public:
    static bool lock(const std::string &filename, boost::system::error_code &ec);

    static int64_t size(const std::string &filename, boost::system::error_code &ec);

    static int read(const std::string &filename,
            std::vector<char> *buffer,
            boost::system::error_code &ec,
            int64_t limit);

    static int write(const std::string &filename,
            const std::vector<char> &buffer,
            boost::system::error_code &ec);

private:
    File();
    ~File();
};

} // namespace bbts

#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_FILE_H
