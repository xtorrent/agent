namespace cpp bbts.stat

struct PeerInfo {
    1: optional string    infohash,
    2: optional string    local_host,
    3: optional string    remote_host,
    5: optional i64       start_time = 0,
    6: optional i64       end_time = 0,
    7: optional i64       uploaded = 0,
    8: optional i64       downloaded = 0,
    9: optional i32       is_cluster = 0, 
}

struct TaskInfo {
    1:  optional string    host_name,
    2:  optional string    ip,
    3:  optional string    infohash,
    4:  optional i64       total_size = 0,
    5:  optional i32       piece_length = 0,
    6:  optional i32       num_pieces = 0,
    7:  optional i32       num_files = 0,
    8:  optional i32       num_paths = 0,
    9:  optional i32       num_symlinks = 0,
    10: optional i32       download_limit = 0,
    11: optional i32       upload_limit = 0,
    12: optional i64       payload_downloaded = 0,
    13: optional i64       payload_uploaded = 0,
    14: optional i32       progress_ppm = 0,
    15: optional i64       start_time = 0,
    16: optional i64       end_time = 0,
    17: optional i32       retval = 0,
    18: optional i32       time_for_download_metadata = 0,
    19: optional i32       time_for_check_files = 0,
    20: optional i32       time_for_downloaded = 0,
    21: optional i32       time_for_seeding = 0,
    22: optional bool      is_hdfs_download = false,
    23: optional i64       downloaded_from_hdfs = 0,
    24: optional string    hdfs_address,
    25: optional string    product_tag,
    26: optional i32       port = 0,
}

service StatAnnounce {
    bool report_stat(1:list<PeerInfo> peer_info, 2:TaskInfo task_info),
}
