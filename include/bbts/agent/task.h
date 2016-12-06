/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   task.h
 *
 * @author liuming03
 * @date   2013-1-16
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_TASK_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_TASK_H

#include <boost/asio/deadline_timer.hpp>
#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <libtorrent/torrent_handle.hpp>

//#include "bbts/bbts_stat.hpp"
#include "bbts/statistics.hpp"
#include "message.pb.h"

namespace bbts {
namespace agent {

/**
 * @brief 任务类
 * 每个任务对应一个torrent_handle句柄
 */
class Task : public boost::enable_shared_from_this<Task>, public boost::noncopyable {
public:
    ~Task();

    int64_t get_id() const {
        return _id;
    }

    std::string get_infohash() const {
        return _infohash;
    }

    std::string get_data_path() const {
        return _save_path + '/' + _new_name;
    }

    libtorrent::torrent_handle get_torrent() const {
        return _torrent;
    }

    message::TaskType get_type() const {
        return _type;
    }

    void set_error(const std::string &error) {
        _error = error;
    }

    const TaskStatistics& get_taskstat() const {
        return _stat;
    }

    int get_seeding_time() const {
        return _seeding_time;
    }

    /**
     * @brief 需要从task_manager中添加peer信息给相应的task
     *
     * @param [in] peer_message   : const std::string& 打印到日志的字符串
     * @return  void 
     * @author yanghanlin
     * @date 2015/11/27 10:51:00
    **/
    //void add_peer_stat(const std::string& peer_message) {
    //    _bbts_stat.set_peer_vector(peer_message);
    //}

    /**
     * @brief  创建一个未初始化的任务
     *
     * @param  taskid(in) 任务id
     * @return 成功返回任务对象，失败返回空
     */
    static boost::shared_ptr<Task> create(int64_t taskid);

    /**
     * @brief 创建一个任务
     *
     * @param  message(in) 任务参数
     * @param  ti(in)      种子信息
     * @param  ae(out)     出现错误则该值非空
     * @return 成功返回任务对象，失败返回NULL
     */
    static boost::shared_ptr<Task> create(
            const message::AddTask &message,
            const boost::intrusive_ptr<libtorrent::torrent_info> &ti,
            boost::system::error_code &ec);

    /**
     * @brief 根据指定的文件恢复上次未完成的任务
     *
     * @param  resume_file(in)  任务持久化文件绝对路径
     * @param  file_size(in)    文件长度
     * @return 成功返回任务对象，失败返回NULL
     */
    static boost::shared_ptr<Task> create_from_resume_file(
            const std::string &resume_file,
            int64_t file_size);

    /**
     * @brief 校验用户是否有操作本任务的权限
     *
     * @param  cred(in)  cmd传来的用户id信息
     * @return 校验成功返回true，失败返回false
     */
    bool check_cred(const struct ucred &cred) const;

    /**
     * @brief 将本任务暂停
     */
    void pause() const;

    /**
     * @brief 将本任务恢复执行
     */
    void resume() const;

    /**
     * @brief 标记本任务被取消；不表示任务立即被删除
     */
    void cancel();

    /**
     * @brief 设置任务的相关参数，如上传下载限速等
     *
     * @param opt_msg(in) 要设置的选项
     */
    void set_options(const message::TaskOptions &task_opt) const;

    /**
     * @brief 获取本任务的相关参数，上传下载限速等
     *
     * @param opt_msg(out) 获取后的参数放在此传出参数中
     */
    void get_options(message::TaskOptions *task_opt) const;

    /**
     * @brief 获取本任务的状态信息
     *
     * 如果是正在运行的任务，则直接查询任务状态；如果是已完成的任务，则通过task.db查询任务的状态
     * @param task_msg(out) 保存获取的任务状态信息
     */
    void get_status(message::TaskStatus *task_status) const;

    /**
     * @brief 任务进入做种
     */
    void to_seeding();

    /**
     * @brief 标记任务需持久化到磁盘
     *
     * agent退出时，持久化存储一个任务
     */
    void persist();

    /**
     * @brief 取消关联的torrent_
     */
    void remove_torrent();

private:
    /**
     * @brief 创建任务的构造函数
     *
     * @param id(in) 任务id，用于向task.db中获取已完成任务的信息等临时任务实例；
     */
    explicit Task(int64_t id);

    /**
     * @brief 更新task.db中本任务的状态信息
     *
     * @param is_finish(in) 任务是否完成
     */
    void update_status(bool is_finish) const;

    /**
     * @brief 根据任务状态获取状态码
     *
     * @param ts(in)        任务状态
     * @param is_finish(in) 任务是否完成
     */
    message::TaskStatus::status_t get_status_code(
            const libtorrent::torrent_status &ts,
            bool is_finish) const;

    /**
     * @brief 删除持久化文件
     */
    void delete_resume_file() const;

    /**
     * @brief 将本任务持久化到本地文件中去
     */
    void generate_resume_file() const;

    /**
     * @brief 从task.db获取一个新的任务自增id
     */
    bool gen_task_id();

    enum {
        TASK_FLAG_NOT_NEW = 0x00000001,            // 是否是新new的任务
        TASK_FLAG_CANCELED = 0x00000002,           // 任务是否被取消
        TASK_FLAG_PERSIST = 0x00000004,
    };
    volatile int32_t _flags;                       // 状态标记

    int64_t _id;                                   // 任务编号
    time_t _start_time;
    std::string _infohash;
    std::string _cmd;                              // 添加进的任务命令行
    ucred _cred;                                   // 创建该任务的用户、组信息
    std::string _save_path;                        // 存储路径
    std::string _new_name;
    message::TaskType _type;                       // 任务类型
    std::vector<std::pair<std::string, int> > _trackers;
    int _seeding_time;                             // 做种时间
    boost::asio::deadline_timer _seeding_timer;    // 做种定时器
    libtorrent::torrent_handle _torrent;
    std::string _error;                            // 错误信息
    TaskStatistics _stat;
    // BbtsStat _bbts_stat;                           // 总体统计信息，发送给bbts-stat server

};

}  // namespace agent
}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_TASK
