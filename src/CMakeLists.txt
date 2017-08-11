set(GINGKO_SRC
    bbts_agent
    bbts_client
    bbts_stat
    torrent_plugin
    control_server
    cluster_api
    cluster_downloader
    disk_allocate_thread
    downloader
    encode
    error_category
    file
    gko3
    help
    log
    options_parse
    path
    process_info
    resume
    routing
    speed_limit
    string_util
    task
    task_db
    task_manager
    thrift_tracker
    timer_util
    torrent_file_util
    torrent_provider    
    transfer_server
    unix_socket_client
    unix_socket_connection
    unix_socket_server
    worker_pool
    proto/configure.pb
    proto/message.pb
    proto/routing.pb
    proto/StatAnnounce
    proto/stat_constants
    proto/stat_types
    proto/TorrentProvider_constants
    proto/TorrentProvider_types
    proto/TorrentProviderService
    proto/transfer_server_constants
    proto/transfer_server_types
    proto/TransferServer
    proto/tracker_constants
    proto/tracker_types
    proto/announce_constants
    proto/announce_types
    proto/Announce)

include(${PROJECT_SOURCE_DIR}/config.cmake)
add_executable(gko3 ${GINGKO_SRC})
target_link_libraries(gko3 sqlite3
                           uri_agent
                           ${LOG4CPP_LIB}
                           ${LIBTORRENT_LIB}
                           ${BOOST_SYSTEM_LIB}
                           ${BOOST_THREAD_LIB}
                           ${BOOST_REGEX_LIB}
                           ${THRIFT_LIB}
                           ${PROTOBUF_LIB}
                           ${SNAPPY_LIB}
                           pthread dl rt)
                           
add_executable(file_is_locked file_is_locked)

set_target_properties(gko3 PROPERTIES INSTALL_RPATH ".:$ENV{HADOOP_HOME}/lib/native:$ENV{JAVA_HOME}/jre/lib/amd64/server")
install(TARGETS gko3 file_is_locked RUNTIME DESTINATION bin)
