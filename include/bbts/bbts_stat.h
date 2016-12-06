/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   bbts_stat.h
 *
 * @author yanghanlin 
 * @date   2015-11-26
 * @brief 
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_BBTS_STAT_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_BBTS_STAT_H

#include <vector>
#include <string>

#include "bbts/routing.h"
#include "bbts/torrent_plugin.h"
#include "StatAnnounce.h"

namespace bbts {

/**
* @brief 统计类，用于搜集task和peer信息，结束发给statistica server
*  
*/
class BbtsStat : public Routing {
public:
    BbtsStat();

    ~BbtsStat();
   
    /**
     * @brief 增加一条peer info 到vector中 
     *
     * @param [in] alert   : const PeerStatAlert&
     * @return  void 
    **/
    void add_to_peer_vector(const PeerStatAlert& alert); 

    /**
     * @brief 得到_task_info 对象，对其进行显示或者赋值修改 
     *
     * @return  stat::TaskInfo& 
    **/
    stat::TaskInfo& get_task_info(); 

    /**
     * @brief send task info and a vector of peer info to server 
     *
     * @return  void 
    **/
    void send_stat();

    /**
     * @brief  将task的信息打印到指定文件 
     *
     * @param [in] filename   : const std::string&
     * @return  void 
    **/
    void print_task_statistics(const std::string &filename);

    /**
     * @brief 打开peer log 
     *
     * @param [in] filename   : const std::tring&
     * @return  bool 
    **/
    bool open_peer_file(const std::string& filename);

private:
    /**
     * @brief  将peer的n条信息打印到指定文件 
     *
     * @param [in] filename   : const PeerStatAlert&
     * @return  void 
    **/
    void print_peer_statistics(const PeerStatAlert& alert);

    FILE *_peer_file;
    std::vector<stat::PeerInfo> _peer_info_vector;
    stat::TaskInfo  _task_info;
};

} // namespace bbts

#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_STATISTICS_H
