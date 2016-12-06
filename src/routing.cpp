/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   routing.cpp
 *
 * @author liuming03
 * @date   2015年5月3日
 * @brief 
 */
#include "bbts/routing.h"

#include <sstream>

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include "bbts/log.h"
#include "bbts/pbconf.hpp"
#include "bbts/process_info.h"

using std::string;
using std::vector;
using std::make_pair;

namespace bbts {

const char* Routing::_s_default_inner_machine_room_area = "default_area";
const char* Routing::_s_default_machine_room_area = "hb";

inline std::ostream& operator <<(std::ostream &os, const Routing::Node &node) {
    os << node.first << node.second;
    return os;
}

void Routing::load_area(const message::RoutingArea& area) {
    if (_service_index < 0 || _service_index > area.service_size()) {
        WARNING_LOG("area service_size is %d, max than service_index:%d",
                area.service_size(), _service_index);
        return;
    }

    const message::RoutingService &service = area.service(_service_index);
    for (int i = 0; i < service.server_size(); ++i) {
        const message::Server &server = service.server(i);
        _hashring.add_node(make_pair(server.host(), server.port()));
    }
}

string Routing::get_machine_room_area(const string &machine_room) {
    std::string area = get_machine_room_area_inner(machine_room);
    if (area != _s_default_inner_machine_room_area) {
        return area;
    }

    return _s_default_machine_room_area;  // default hb
}

bool Routing::check_machine_room_area_default(const std::string &machine_room) {
    std::string area = get_machine_room_area_inner(machine_room);
    if (area != _s_default_inner_machine_room_area) {
        return false;
    }

    return true;
}

string Routing::get_machine_room_area_inner(const string &machine_room) {
    for (int j = 0; j < _routing_conf.area_size(); ++j) {
        const message::RoutingArea& area = _routing_conf.area(j);
        for (int i = 0; i < area.machine_room_size(); ++i) {
            if (ProcessInfo::get_machine_room_without_digit(machine_room) == area.machine_room(i)
                    || machine_room == area.machine_room(i)) {
                return area.area_name();
            }
        }
    }

    return _s_default_inner_machine_room_area;
}

bool Routing::load_conf(const string &conf_path, const std::string &machine_room) {
    _hashring.clear();
    if (!load_pbconf(conf_path, &_routing_conf)) {
        DEBUG_LOG("load routing conf(%s) failed", conf_path.c_str());
        return false;
    }

    bool use_default = true;
    for (int j = 0; j < _routing_conf.area_size(); ++j) {
        const message::RoutingArea& area = _routing_conf.area(j);
        bool find_area = false;
        for (int i = 0; i < area.machine_room_size(); ++i) {
            if (ProcessInfo::get_machine_room_without_digit(machine_room) == area.machine_room(i)
                    || machine_room == area.machine_room(i)) {
                find_area = true;
                break;
            }
        }
        if (find_area) {
            load_area(area);
            use_default = false;
            break;
        }
    }

    if (use_default) {
        const message::RoutingArea& area = _routing_conf.default_();
        load_area(area);
    }

    if (_hashring.empty()) {
        FATAL_LOG("can't find routing node!");
        return false;
    }
    return true;
}

void Routing::get_nodes(const string &info, NodeVector *nodes) const {
    _hashring.get_all_nodes(info, nodes);
}

}  // namespace bbts
