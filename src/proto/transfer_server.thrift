namespace cpp bbts 

struct RequestTransferInfo {
  1:string uri;
  2:string infohash;
  3:string key;             // format is "infohash_hd_hb"
  4:i64 size;
  5:string hostname;
  6:i32 download_limit;
  7:i32 failed_num = 0;
  8:bool disable_hash_check = false;
  9:optional string product_name;
  10: i32 transfer_number = 0;
  11:optional bool enable_dynamic_hash_check = false;
}

struct GeneralResponse {
  1:string message;
  2:i32 error_code;
}

enum TransferClientURIStatus {
  STATUS_START          = 0,
  STATUS_RUNNING        = 1,
  STATUS_STOP           = 2,
  STATUS_START_FAILED   = 3,
  STATUS_RUNNING_FAILED = 4,
  STATUS_STOP_FAILED    = 5,
  STATUS_DELETED        = 6,
  STATUS_UNKNOWN        = 10,

}

struct TransferClientURIInfo {
  1:string uri;
  2:string infohash;
  3:string data_path;
  4:string client_name;
  5:TransferClientURIStatus status;
  6:optional i64 total_upload;
  7:optional i64 total_download;
  8:optional i64 upload_rate;
}

enum TransferClientStatus {
  STATUS_START          = 0,
  STATUS_RUNNING        = 1,
  STATUS_STOP           = 2,
  STATUS_ERROR          = 3,
  STATUS_UNKNOWN        = 10,
}

struct TransferClientInfo {
  1:string hostname;
  2:i64 disk_total_size;
  3:i64 disk_left_size;
  4:i64 net_total_size;
  5:i64 net_left_size;
  6:i32 uri_total_number;
  7:TransferClientStatus status;
}

service TransferServer {
  GeneralResponse request_transfer(1:RequestTransferInfo request_info);
  GeneralResponse client_heartbeat(1:TransferClientInfo client_info,
                                   2:list<TransferClientURIInfo> uri_list);
}

