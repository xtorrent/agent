/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   help.cpp
 *
 * @author liuming03
 * @date   2015-1-24
 * @brief 
 */
#include <stdio.h>

#include "bbts/config.h"
#include "bbts/lazy_singleton.hpp"
#include "configure.pb.h"

namespace bbts {

void print_attr_help() {
    fprintf(stdout, "\
attr help\n\
    Attributes is used for specifie what you want to display.This arguments can used for operation status,\n\
    list, wait and down. Multiple attributes need separate with blank, colon or semicolon.\n\
\n\
    taskid    task id                             status    task current status\n\
    progress  percentage of the task progress     infohash  seed token\n\
    user      the user who add the task to agent  group     the group of the user\n\
    cmd       command self which add the task     error     display error messsage\n\
    uprate    current upload speeds               upload    total uploaded for this task\n\
    downrate  current upload speeds               download  total uploaded for this task\n\
    peers     the number of the current peers     seeds     the number of the current seed peers\n\
    savepath  the data to be saved to\n\
\n\
    If not specifie the -a arguments, will display 'taskid status progress infohash user cmd'\n\
\n");
}

int print_help(int argc, char *argv[]) {
    if (argc > 2) {
        if (strncmp("attr", argv[2], 5) == 0) {
            print_attr_help();
            return 0;
        }
        printf("invalid argument: %s\n", argv[2]);
        return 1;
    }

    message::DownloadConfigure *configure = LazySingleton<message::DownloadConfigure>::instance();
    fprintf(stdout, "\
NAME:\n\
  gko3 -- p2p transfer, download data from many other peers by BitTorrent protocol.\n\
\n\
Usage: gko3 operation [options]\n\
  operation described as follows:\n\
\n\
  mkseed -p path [r:s:C:W:l] [--include-files regex] [--exclude-files regex]\n\
    Make a torrent file for spcified path.\n\
      -p,  --path          The root path you want to share.\n\
      -r,  --torrent       Output filename of the torrent file for this path, default to path.torrent.\n\
      -s,  --size          Piece size(KB) for the torrent, default to program auto-computed value(>=2MB), 0 = default.\n\
                           Multiple of 16 KB is recommended.\n\
      -C,  --cluster       The hadoop addr where source data located, like hdfs://user:passwd@host:port/path\n\
      -W,  --webseed       The http addr where source data located, like http://noah.baidu.com/filepath\n\
      -l,  --link          Don't follow symlinks, instead encode them as links in the torrent file.\n\
           --include-files The files that you want to include in .torrent for serve. If use regex, need add quotation\n\
                           marks around.\n\
           --exclude-files The files that you don't want to include in .torrent for serve. As the same, need add\n\
                           quotation marks around if use regex.\n\
\n\
  dump   -r torrent\n\
    Dump spcified torrent file info.\n\
\n\
  add    (-r torrent | -i infohash) (-p path | -n fullpath) -S seedtime [-u uplimit] [--seed]\n\
    Add a p2p task(seeding task or download task).\n\
      -r,  --torrent       The BitTorrent seed file, like *.torrent.\n\
      -i,  --infohash      The 20 bytes infohash of a *.torrent file.\n\
      -p,  --path          For seeder is where seeded file located.\n\
      -n,  --fullpath      Path + root name, if specified, we will ignore -p \n\
      -S,  --seedtime      When task is finished, it will seeding for seedtime seconds. \n\
                           If seedtime equals -1, the task won't quit until user cancel this Task.\n\
                           If not set, this task will quit when it has finished, just like seedtime equals 0.\n\
      -u,  --uplimit       Limit the upload bandwidth(MB/s), default to %d MB/s, 0 = default.\n\
           --seed          This task will be as a seeding task, not download any data.\n\
\n\
  serve  -p path -S seedtime [u:r:s:l] [--include-files] [--exclude-files]\n\
    Atomic operation for mkseed and add. Arguments is like mkseed and add --seed.\n\
\n\
  down   (-r torrent | -i infohash) (-p path | -n fullpath) [S:O:d:u:C:W:P:] [--include-files] [--exclude-files]\n\
    Synchronous download.\n\
      -d,  --downlimit     Limit the download bandwith, unit MB/s. Default or 0 will be set to %d MB/s.\n\
      -O,  --timeout       If download time exceed this value, the download process will quit as timeout\n\
      -P,  --port          Bind port range for p2p transfer, such as 6881,6889.\n\
           --include-files The files that you want to download. Suport regex, should add quotation marks around.\n\
           --exclude-files The files that you don't want to download. Suport regex, should add quotation marks around.\n\
           --continue      Continue getting a partially-downloaded job. Default is disabled\n\
           --tmp-files     The data will be download to tmp files first, and then move to save path that the user set.\n\
           --unix-socket   Set a unix socket path for communicate with download process, such as modify download limit, etc.\n\
           --save-torrent  If download by infohash, set this can save torrent to local file.\n\
           --hang-timeout  This is for check whether download is hang, if exceed this value, download quit as no seeder.\n\
           --debug         Open debug mode. Default not open.\n\
           --ip-white      Ip white ip list for connect to theses peers.\n\
                           e.g. --ip-white=10.26.0.0-10.26.255.255,10.152.15.37,10.38.24.129-10.38.24.192\n\
           --mask          Like subnet mask, only these subnet can connect, host ip 10.26.38.49\n\
                           with --mask=255.255.0.0 is the same as --ip-white=10.26.0.0-10.26.255.255\n\
\n\
  cancel/pause/resume [-t taskid ... ] [--all]\n\
    Cancel/pause/resume task from agent, it can specifiy muti-taskid.\n\
      -t,  --task         The task id that add return, unique mark a task.\n\
           --all          Batch process all task that your account can.\n\
\n\
  list   [-t taskid ... ] [-a attributes] [--noheader]\n\
    List the tasks you specified, if not set -t, will list all tasks.\n\
\n\
  setopt [-t taskid | --unix-socket] [-u uprate] [-d downrate]\n\
    Modify the task or agent option, you should use unix-socket for download task.\n\
\n\
  getopt [-t taskid | --unix-socket]\n\
    Get task or agent option, you should use unix-socket for download task\n\
\n\
  help [attr]\n\
\n\
      -h,  --help         Show this help message.\n\
      -v,  --version      Show version message\n\
\n\
  bbts-group\n\
    Use bbts-group, for more infomation, use gko3 bbts-group -h for help \n\
\n\
EXAMPLES\n\
  You can make seed file(a.torrent) as follows:\n\
     gko3 mkseed -p /path/to/a -r a.torrent\n\
  To seeding /path/to/a with a.torrent in local machine:\n\
     gko3 add -r a.torrent -p /path/to -S 3600 -u 20 --seed\n\
  Or you can combine mkseed and add for seeding /path/to/a as follows:\n\
     gko3 serve -p /path/to/a -r a.torrent -S 3600 -u 20\n\
\n\
  The following is how to download from the torrent file a.torrent to local path /path/to/save\n\
     gko3 down -r a.torrent -p /path/to/save -u 10 -d 10\n\
\n\
   WIKI: http://doc.noah.baidu.com/new/bbts/introduction.md\n\
\n\
AUTHORS: liuming03@baidu.com, hechaobin01@baidu.com, bask@baidu.com\n\
     QA: zhangning05@baidu.com\n",
            configure->upload_limit() / 1024 / 1024,
            configure->download_limit() / 1024 / 1024);
    return 0;
}

int print_version(int argc, char *argv[]) {
    fprintf(stdout, "%s\n  version: %s\n  build date: %s\n", argv[0], GINGKO_VERSION, BUILD_DATE);
    return 0;
}
} // namespace bbts

