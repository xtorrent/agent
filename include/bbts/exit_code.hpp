/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file exit_code.h
 *
 * @author liuming03
 * @date 2013-6-26
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_EXIT_CODE_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_EXIT_CODE_H

#include <sys/wait.h>

#include <string>

namespace bbts {

/**
 * @brief wait/waitpid/system等返回值转换
 */
class ExitCode {
public:
    enum exit_type {
        T_WUNKNOW,
        T_WEXITED,
        T_WSIGNALED,
        T_WSTOPPED,
        T_WCONTINUED,
    };

    ExitCode(int status) : _type(T_WUNKNOW), _code(status) {
        if (WIFEXITED(status)) {
            _type = T_WEXITED;
            _code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            _type = T_WSIGNALED;
            _code = WTERMSIG(status);
        } else if (WIFSTOPPED(status)) {
            _type = T_WSTOPPED;
            _code = WSTOPSIG(status);
        }
    }

    int get_code() const {
        return _code;
    }

    exit_type get_type() const {
        return _type;
    }

    std::string get_type_string() const {
        static std::string s_exit_type_string[] = {
            "UNKNOW",
            "EXITED",
            "SIGNALED",
            "STOPPED",
            "CONTINUED",
        };
        return s_exit_type_string[_type];
    }

private:
    exit_type _type;
    int _code;

};

}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_EXIT_CODE_H
