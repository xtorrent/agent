find_path(BOOST_H NAMES boost/system/error_code.hpp)
find_library(BOOST_SYSTEM_LIB NAMES libboost_system.a)
if (APPLE)
    find_library(BOOST_THREAD_LIB NAMES libboost_thread-mt.a)
elseif (UNIX)
    find_library(BOOST_THREAD_LIB NAMES libboost_thread.a)
endif()
find_library(BOOST_REGEX_LIB NAMES libboost_regex.a)
if(NOT BOOST_H OR NOT_TR1_H OR NOT BOOST_SYSTEM_LIB OR NOT BOOST_THREAD_LIB OR NOT BOOST_REGEX_LIB)
    message(FATAL_ERROR "boost library(boost_system/boost_thread/boost_regex) not found.")
endif()

find_path(PROTOBUF_H NAMES google/protobuf/message.h)
find_library(PROTOBUF_LIB NAMES libprotobuf.a)
if(NOT PROTOBUF_H OR NOT PROTOBUF_LIB)
    message(FATAL_ERROR "protobuf not found.")
endif()

find_path(LIBEVENT_H NAMES event.h)
find_library(LIBEVENT_LIB NAMES libevent.a)
if(NOT LIBEVENT_H OR NOT LIBEVENT_LIB)
    message(FATAL_ERROR "libevent not found.")
endif()

find_path(SNAPPY_H NAMES snappy.h)
find_library(SNAPPY_LIB NAMES libsnappy.a)
if(NOT SNAPPY_H OR NOT SNAPPY_LIB)
    message(FATAL_ERROR "snappy not found.")
endif()

find_path(THRIFT_H NAMES thrift/Thrift.h)
find_library(THRIFT_LIB NAMES libthrift.a)
find_library(THRIFT_NB_LIB NAMES libthriftnb.a)
if(NOT THRIFT_H OR NOT THRIFT_LIB OR NOT THRIFT_NB_LIB)
    message(FATAL_ERROR "thrift not found.")
endif()

find_path(LIBTORRENT_H NAMES libtorrent/torrent.hpp)
find_library(LIBTORRENT_LIB NAMES libtorrent-rasterbar.a)
if(NOT LIBTORRENT_H OR NOT LIBTORRENT_LIB)
    message(FATAL_ERROR "libtorrent not found.")
endif()

find_path(LOG4CPP_H NAMES log4cpp/Category.hh)
find_library(LOG4CPP_LIB NAMES liblog4cpp.a)
if(NOT LOG4CPP_H OR NOT LOG4CPP_LIB)
    message(FATAL_ERROR "log4cpp not found")
endif()

include_directories(${PROJECT_SOURCE_DIR}/src/proto
                    ${PROJECT_SOURCE_DIR}/include
                    ${BBTS_PROTOCOL_H}
                    ${BOOST_H}
                    ${SNAPPY_H}
                    ${PROTOBUF_H}
                    ${LIBEVENT_H}
                    ${THRIFT_H}
                    ${LIBTORRENT_H}
                    ${LOG4CPP_H})

if("$ENV{SCMPF_MODULE_VERSION}" STREQUAL "")
    set(GINGKO_VERSION "\"1.0.0\"")
else()
    set(GINGKO_VERSION "\"$ENV{SCMPF_MODULE_VERSION}\"")
endif()

add_definitions(-D_XOPEN_SOURE=500
                -D_GNU_SOURCE
                -DHAVE_NETINET_IN_H
                -DHAVE_NETDB_H=1
                -DGINGKO_VERSION=${GINGKO_VERSION}
                -DTORRENT_NO_DEPRECATE=1
                -DTORRENT_DISABLE_ENCRYPTION=1
                -DTORRENT_DISABLE_GEO_IP=1
                -DTORRENT_DISABLE_DHT=1
                -DBOOST_ASIO_HASH_MAP_BUCKETS=1021
                -DBOOST_ASIO_SEPARATE_COMPILATION=1
                -DBOOST_ASIO_HAS_EPOLL=1
                #-DTORRENT_DEBUG=1
                -DBOOST_EXCEPTION_DISABLE=1
                -DBOOST_ASIO_ENABLE_CANCELIO=1)

set(CMAKE_CXX_FLAGS "-g -fPIC -ggdb -Wall -pipe -fpermissive -Wno-unused-function -Wno-unused-parameter -Wno-invalid-offsetof -Winline -Wpointer-arith -Wwrite-strings -Woverloaded-virtual -ftemplate-depth-128 -Wreorder -Wswitch -Wformat")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb")
