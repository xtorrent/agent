/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   resume.h
 *
 * @author liuming03
 * @date   2015年6月6日
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_RESUME_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_RESUME_H

#include <string>
#include <vector>

namespace bbts {
namespace tool {

/**
 * @brief
 */
class Resume {
public:
    Resume();

    void set_filename(const std::string &resume_file);

    void load(std::vector<char> *buffer) const;

    void save(const std::vector<char> &buffer) const;

    void remove() const;

    bool check_file() const;

    ~Resume();

private:
    std::string _filename;
    std::string _tmp_filename;
};

} // namespace tool
} // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_RESUME_H
