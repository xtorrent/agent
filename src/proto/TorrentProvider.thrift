namespace java com.baidu.noah.hermes.torrentprovider.thrift

enum TorrentStatus {
  STATUS_OK       = 0,
  STATUS_PROCESS  = 1,
  STATUS_WAIT     = 2,
  STATUS_ERROR    = 3,
  STATUS_ERROR_FILE_NOT_EXIST = 4,
  STATUS_UNKNOWN  = 10,
}

struct GeneralResponse {
  1: i32            retCode,
  2: string         message,
}

struct TorrentResponse {
  1: string           torrentUrl,
  2: TorrentStatus    torrentStatus = TorrentStatus.STATUS_UNKNOWN,
  3: binary           torrentZipCode,
  4: optional string  message,
  5: optional i64     modifyTime,
}

struct InfohashTorrent {
  1: string           infohash,
  2: TorrentStatus    torrentStatus = TorrentStatus.STATUS_UNKNOWN,
  3: binary           torrentZipCode,
  4: optional string  message,
  5: optional string  source,
}

enum ProviderServerCmdType {
  CMD_STATUS      = 0,
  CMD_STOP        = 1,
  CMD_UNKNOWN     = 2,
}

struct ProviderServerCmd {
  1: ProviderServerCmdType type = ProviderServerCmdType.CMD_UNKNOWN,
  2: string                cmd,
}

service TorrentProviderService {
  TorrentResponse getTorrentZipCode(1:string torrentUrl),
  GeneralResponse controlServer(1:ProviderServerCmd cmd),
  GeneralResponse uploadTorrent(1:InfohashTorrent torrent),
  InfohashTorrent getInfohashTorrent(1:string infohash),
}
