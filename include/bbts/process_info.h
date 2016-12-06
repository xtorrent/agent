/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   process_info.h
 *
 * @author liuming03
 * @date   2014年10月21日
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_PROCESS_INFO_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_PROCESS_INFO_H

#include <netinet/in.h> // for in_addr_t
#include <sys/types.h>  // for pid_t, uid_t

#include <string>

#include <boost/thread/tss.hpp>

namespace bbts {

/**
 * @brief
 */
class ProcessInfo {
public:
    ProcessInfo();
    ~ProcessInfo() {}

    const std::string& program() const {
        return _program;
    }

    const std::string& root_dir() const {
        return _root_dir;
    }

    const std::string& hostname() const {
        return _hostname;
    }

    const std::string& machine_room() const {
        return _machine_room;
    }

    const std::string& machine_room_without_digit() const {
        return _machine_room_without_digit;
    }

    const std::string& ip() const {
        return _ip;
    }

    uid_t uid() const {
        return _uid;
    }

    pid_t pid() const {
        return _pid;
    }

    struct in_addr addr() const {
        return _in_addr;
    }

    bool set_evn(const std::string &key, const std::string &value) const;

    std::string get_evn(const std::string &key) const;

    pid_t thread_id() const ;

    int random() const;

    static std::string get_machine_room(const std::string &name, bool is_hdfs);

    static std::string get_machine_room_without_digit(const std::string &machine_room);

    /**
     * @brief 根据任意指定的pid，和查看的type 包括 执行程序exe, 执行命令cmdline, 执行路径cwd 等等 
     *
     * @param [in] pid   :  pid_t  
     * @param [in] type   : const std::string&
     * @return  std::string 得到的process 信息
     * @author yanghanlin
     * @date 2016/02/18 16:06:27
    **/
    static std::string get_link_info_by_pid_and_type(pid_t pid, const std::string &type);

    /**
     * @brief 读取/proc/pid/cmdline的内容
     *
     * @param [in] pid   :  pid_t
     * @return  std::string process name 
     * @author yanghanlin
     * @date 2016/02/18 16:22:25
    **/
    static std::string get_process_cmdline_by_pid(pid_t pid);

private:
    std::string _program;
    std::string _root_dir;
    std::string _hostname;
    std::string _machine_room;
    std::string _machine_room_without_digit;
    struct in_addr _in_addr;
    std::string _ip;
    uid_t _uid;
    pid_t _pid;

    mutable boost::thread_specific_ptr<unsigned> _rand_seed;
};

extern ProcessInfo *g_process_info;

}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_PROCESS_INFO_H
