/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   regex_match.h
 *
 * @author liuming03
 * @date   2013-12-31
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_REGEX_MATCH_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_REGEX_MATCH_H

#include <string>

#include <boost/regex.hpp>

namespace bbts {

/**
 * @brief
 */
class RegexMatch {
public:
    explicit RegexMatch(const std::string &str) : _str(str) {}

    explicit RegexMatch(const char *str) : _str(str) {}

    ~RegexMatch() {}

    bool operator ()(const std::string &regex_str) {
        try {
            boost::regex regex(regex_str);
            return boost::regex_match(_str, regex);
        } catch (boost::bad_expression &e) {
            fprintf(stderr, "regex string %s invalid: %s\n", regex_str.c_str(), e.what());
        }
        return false;
    }

private:
    std::string _str;
};

template <typename Container>
bool is_accord_rule(const std::string &str, const Container &include, const Container &exclude) {
    if (include.begin() == include.end() && exclude.begin() == exclude.end()) {
        return true;
    }
    bool is_accord = false;
    RegexMatch regex_m(str);
    if (include.begin() == include.end()
            || std::find_if(include.begin(), include.end(), regex_m) != include.end()) {
        is_accord = true;
    }
    if (std::find_if(exclude.begin(), exclude.end(), regex_m) !=  exclude.end()) {
        is_accord = false;
    }
    return is_accord;
}

}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_REGEX_MATCH_H
