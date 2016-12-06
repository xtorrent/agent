/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   task_manager.h
 *
 * @author liuming03
 * @date   2013-1-14
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_TASK_MANAGER_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_TASK_MANAGER_H

#include <list>
#include <map>

#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/unordered_map.hpp>
#include <libtorrent/alert_types.hpp>

#include "bbts/statistics.hpp"
#include "bbts/thrift_tracker.h"
#include "bbts/unix_socket_server.h"
#include "bbts/worker_pool.h"
#include "message.pb.h"
#include "configure.pb.h"

#include "../uri/uri_agent.hpp"

namespace bbts {

namespace agent {

class TaskDB;
class Task;

/**
 * @brief 任务管理类
 *
 * 任务管理类是单实例的类，管理agent中的所有任务；其他线程的处理，均通过任务管理类的单实例进行处理
 */
class TaskManager : public boost::noncopyable {
public:
    TaskManager();

    struct Metadata {
        std::string infohash;
        std::string uri;
        std::string metadata;
        time_t add_time;
    };

    typedef boost::asio::local::stream_protocol::socket LocalSocket;
    typedef std::map<int64_t, boost::shared_ptr<Task> > TasksMap;
    typedef boost::unordered_map<libtorrent::torrent_handle, boost::weak_ptr<Task> > TorrentMap;
    typedef boost::unordered_map<std::string, boost::weak_ptr<Task> > InfohashMap;
    typedef boost::unordered_map<std::string, boost::weak_ptr<Task> > DataPathMap;
    typedef std::list<boost::shared_ptr<Metadata> > MetadataList;

    ~TaskManager();

    /**
     * @brief 任务管理器正式启动运行
     *
     * @return 成功返回true，否则返回false
     */
    bool start();

    /**
     * @brief 等待TaskManager处理结束
     */
    void join();

    /**
     * @brief 停止任务
     */
    void stop();

    boost::asio::io_service& get_io_service() {
        return _worker_pool.get_io_service();
    }

    libtorrent::session* get_session() {
        return _session.get();
    }

    int get_listen_port() const {
        return _listen_port;
    }

    TaskDB* get_task_db() {
        return _task_db.get();
    }

    const ThriftTracker& get_thrift_tracker() const {
        return _thrift_tracker;
    }

    /**
     * @brief 做种完成后的回调函数
     *
     * @param taskid(in) 任务id
     * @param ec(in)     错误码
     */
    void seeding_timer_callback(int taskid);

private:

    /**
     * @brief CmdDispatcher绑定的处理函数，交由ProcessMessage()进行处理
     *
     * @param peer_sock(in) 对端socket
     */
    void on_read_message(const boost::shared_ptr<UnixSocketConnection> &connection,
            const boost::shared_ptr<const std::vector<char> > &data);

    void on_accept(const boost::shared_ptr<UnixSocketConnection> &);

    /**
     * @brief 处理cmd发来的命令
     *
     * 从fd中读取命令解析后处理，处理完成后，写消息告知cmd。
     * @param peer_sock(in) cmd请求的socket
     */
    void process_message(const boost::shared_ptr<UnixSocketConnection> &connection,
            const boost::shared_ptr<const std::vector<char> > &data);

    /**
     * @brief 持久化并删除TaskManager中的所有task
     */
    void persist_tasks();

    /**
     * @brief 获取infohash
     *
     * @param taskinfo(in)
     */
    boost::shared_ptr<Metadata> get_metadata_by_taskinfo(const message::Task &taskinfo);
    /**
     * @brief 处理一次session中的alerts
     */
    void process_alert();

    /**
     * @brief 删除（下载出错的）任务
     *
     * @param taskid
     * @param error 出错原因
     */
    void remove_task_by_error(boost::shared_ptr<Task> task, const std::string &error);

    /**
     * @brief 根据torrent_handle找到对应的task，并对task调用指定的回调函数
     * @param torrent task(in) 对应的torrent_handle
     * @param cb(in)           回调函数
     */
    void process_task_by_torrent(libtorrent::torrent_handle torrent,
            boost::function<void(boost::shared_ptr<Task>)> cb);

    /**
     * @brief 启动时将持久化存储的任务恢复到任务管理器中
     */
    void add_resume_task();

    /* 以下为处理各种用户请求消息的函数 */
    /**
     * @brief 处理添加任务的message
     * @param fd(in)
     * @param message(in)  message会被修改infohash,new_name字段
     * @param ae(in)
     */
    void add_task(
            const boost::shared_ptr<UnixSocketConnection> &connection,
            message::AddTask &message,
            boost::system::error_code &ec);

    /**
     * @brief 处理设置任务参数的消息
     */
    void set_task_options(
            const boost::shared_ptr<UnixSocketConnection> &connection,
            const message::TaskOptions &options,
            boost::system::error_code &ec);

    /**
     * @brief 从task.db中获得agent的配置
     * */
    bool get_agent_options_from_db(message::AgentOptions *options);

    /**
     * @brief 处理获取任务参数的消息
     */
    void get_task_options(
            const boost::shared_ptr<UnixSocketConnection> &connection,
            message::TaskOptions *options,
            boost::system::error_code &ec);

    /**
     * @brief 处理设置agent参数的消息
     */
    void set_agent_options(
            const boost::shared_ptr<UnixSocketConnection> &connection,
            const message::AgentOptions &options,
            boost::system::error_code &ec);

    /**
     * @brief 处理获取agent参数的消息
     */
    void get_agent_options(
            const boost::shared_ptr<UnixSocketConnection> &connection,
            message::AgentOptions *options);

    /**
     * @brief 获取状态信息
     */
    void get_status(const boost::shared_ptr<UnixSocketConnection> &connection);

    // 处理获取某个任务状态的消息
    void list_task(int64_t taskid, const TasksMap::iterator &it, message::BatchListRes &response);

    /**
     * @brief 处理取得本机当前所有任务状态的消息
     */
    void list_tasks(
            const boost::shared_ptr<UnixSocketConnection> &connection,
            const message::BatchCtrl &message);

    //通过taskid处理单个任务的控制消息
    void ctrl_task_by_id(
            int64_t taskid,
            const TasksMap::iterator &it,
            const struct ucred &cred,
            message::BatchCtrl::ctrl_t type,
            message::BatchCtrlRes &response,
            bool skip);
    
    //通过infohash处理单个任务的控制消息
    void ctrl_task_by_infohash(
            const InfohashMap::iterator &it,
            const struct ucred &cred,
            message::BatchCtrl::ctrl_t type,
            message::BatchCtrlRes &response,
            bool skip);

    /**
     * @brief 处理批处理消息
     */
    void batch_ctrl_tasks(
            const boost::shared_ptr<UnixSocketConnection> &connection,
            const message::BatchCtrl &message);

    /**
     * @brief 处理添加metadata的消息
     */
    void add_metadata(const message::Metadata &message);

    /*  以下为处理相关alert消息的回调函数  */
    void on_torrent_finished(const libtorrent::torrent_finished_alert *alert);
    void on_listen_failed(const libtorrent::listen_failed_alert *alert);
    void on_torrent_error(const libtorrent::torrent_error_alert *alert);
    void on_peer_disconnect(const libtorrent::peer_disconnected_alert *alert);

    // libtorrent中的session对象，管理所有任务
    boost::scoped_ptr<libtorrent::session> _session;
    // taskid => task
    TasksMap _tasks_map;
    // torrent_handle => task
    TorrentMap _torrent_map;
    // infohash => task;
    InfohashMap _infohash_map;
    // data_path => task
    DataPathMap _data_path_map;

    // 锁map的
    boost::mutex _tasks_lock;

    WorkerPool _worker_pool;

    WorkerPool _thrift_tracker_pool;
    ThriftTracker _thrift_tracker;

    // control server
    UnixSocketServerWithThread _control_server;

    // 任务数据库
    boost::scoped_ptr<TaskDB> _task_db;  //用指针，否则AgentConf对象还未加载

    MetadataList _metas;
    int _current_metas_total_size;
    boost::mutex _metas_lock;
    PeerStatFile _peer_stat;
    int _listen_port;

    // uri_agent
    UriAgent _uri_agent;
};

extern TaskManager *g_task_manager;
extern message::AgentConfigure *g_agent_configure;

}  // namespace agent
}  // namespace bbts
#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_TASK_MANAGER_H
