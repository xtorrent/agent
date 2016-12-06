/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/**
 * @file   torrent_file_util.cpp
 *
 * @author liuming03
 * @date   2013-2-1
 */

#include "bbts/torrent_file_util.h"

#include <ftw.h>

#include <vector>

#include <boost/bind.hpp>
#include <boost/noncopyable.hpp>
#include <boost/regex.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/thread.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/bitfield.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/file.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/lazy_entry.hpp>

#include "bbts/encode.h"
#include "bbts/file.h"
#include "bbts/log.h"
#include "bbts/options_parse.h"
#include "bbts/path.h"
#include "bbts/process_info.h"
#include "bbts/torrent_provider.h"

using std::string;
using std::vector;
using boost::regex;
using boost::system::error_code;
using libtorrent::create_torrent;
using libtorrent::symlink_file;
using libtorrent::torrent_info;

namespace bbts {

namespace detail {

class FileFilter {
public:
    FileFilter(
            const string &root_path,
            const string &path_reg,
            const vector<string> &include,
            const vector<string> &exclude) :
            _root_path(root_path),
            _prefix_len(0),
            _include(include),
            _exclude(exclude) {
        try {
            _re.assign(path_reg);
        } catch (boost::bad_expression &e) {
            _error = e.what();
            WARNING_LOG("path regex(%s) invalid: %s", path_reg.c_str(), e.what());
        }
        _prefix_len = Path::parent_dir(_root_path).length() + 1;
    }

    inline bool is_error() {
        return !_error.empty();
    }

    bool operator ()(const string &filename) {
        if (filename == _root_path) {
            return true;
        }
        if (!boost::regex_match(filename.c_str(), _re)) {
            return false;
        }
        RegexMatch regex_m(filename.substr(_prefix_len));
        if (!_include.empty()
                && std::find_if(_include.begin(), _include.end(), regex_m) == _include.end()) {
            return false;
        }
        return std::find_if(_exclude.begin(), _exclude.end(), regex_m) == _exclude.end();
    }

private:
    string _root_path;
    regex _re;
    string _error;
    int _prefix_len;
    const vector<string> &_include;
    const vector<string> &_exclude;
};

static void print_progress(int i, int num) {
    // printf("\r%d/%d", i + 1, num);
}

class QuickPiecesHashes : public boost::noncopyable {
public:
    QuickPiecesHashes(create_torrent& t, string const& path, int thread_num);
    virtual ~QuickPiecesHashes();
    bool set_piece_hashes();

private:
    void thread_main();
    bool read_piece(int i, char *buf, int buf_len);

    int _thread_num;
    string _root_path;
    create_torrent &_ct;
    libtorrent::bitfield _pieces_completed;
    boost::mutex _pieces_mutex;
    boost::thread_group _threads;
    volatile bool _have_error;
};

QuickPiecesHashes::QuickPiecesHashes(create_torrent& t, string const& root_path, int thread_num) :
        _thread_num(thread_num),
        _root_path(root_path),
        _ct(t),
        _pieces_completed(_ct.num_pieces(), false),
        _have_error(false) {}

QuickPiecesHashes::~QuickPiecesHashes() {}

bool QuickPiecesHashes::read_piece(int i, char *buf, int buf_len) {
    FILE *fp = NULL;
    char *ptr = buf;
    const libtorrent::file_storage &fs = _ct.files();
    vector<libtorrent::file_slice> files = fs.map_block(i, 0, fs.piece_size(i));
    for (vector<libtorrent::file_slice>::iterator it = files.begin();
            it != files.end() && !_have_error; ++it) {
        string filename = _root_path + "/" + fs.at(it->file_index).path;
        fp = fopen(filename.c_str(), "rb");
        if (!fp) {
            WARNING_LOG("open file failed: %s", filename.c_str());
            return false;
        }
        if (fseek(fp, it->offset, SEEK_SET) != 0) {
            WARNING_LOG("fseek file(%s) to offset(%ld) failed", filename.c_str(), it->offset);
            fclose(fp);
            return false;
        }
        if (fread(ptr, 1, it->size, fp) != (size_t)it->size) {
            WARNING_LOG("fread file(%s) failed, offset: %ld, len: %ld",
                    filename.c_str(), it->offset, it->size);
            fclose(fp);
            return false;
        }
        fclose(fp);
        ptr += it->size;
    }
    return true;
}

void QuickPiecesHashes::thread_main() {
    int num = _ct.num_pieces();
    int piece_length = _ct.piece_length();
    boost::scoped_array<char> buffer(new char[piece_length]);
    for (int i = 0; i < num && !_have_error; ++i) {
        {
            boost::mutex::scoped_lock lock;
            if (_pieces_completed.get_bit(i)) {
                continue;
            }
            _pieces_completed.set_bit(i);
        }

        if (!read_piece(i, buffer.get(), piece_length)) {
            _have_error = true;
            return;
        }
        libtorrent::hasher h(buffer.get(), _ct.piece_size(i));
        _ct.set_hash(i, h.final());
    }
}

bool QuickPiecesHashes::set_piece_hashes() {
    if (_ct.files().num_pieces() == 0) {
        return true;
    }

    for (int i = 0; i < _thread_num; ++i) {
        _threads.create_thread(boost::bind(&QuickPiecesHashes::thread_main, this));
    }
    _threads.join_all();
    if (_have_error) {
        return false;
    }
    return true;
}

}

bool make_torrent(
        const MakeTorrentArgs &args,
        string* infohash,
        vector<char> *torrent,
        string* error_message) {
    assert(infohash);
    assert(torrent);

    // error_message is only used by torrent_generator,
    // so we don't assign some errors that shouldn't happened in torrent_generator 
    assert(error_message);
    error_message->clear();

    if (!args.cluster_config.empty() && !check_cluster_uri(args.cluster_config)) {
        return false;
    }

    if (args.piece_size < 0 || args.piece_size > 20 * 1024 * 1024) {
        WARNING_LOG("piece size[16K-20M] invalid: %dKB", args.piece_size / 1024);
        return false;
    }

    string parent_path;
    string short_name;
    Path::slipt(args.path, &parent_path, &short_name);

    string reg_path;
    string root_path;
    string::size_type pos = short_name.find('*');
    if (pos == string::npos) {
        reg_path = Path::regex_replace(args.path, "^?+") + '/' + ".*";
        root_path = args.path;
    } else {
        do {
            short_name.replace(pos, 1, ".*");
            pos += 2;
        } while (string::npos != (pos = short_name.find('*', pos)));
        reg_path = Path::regex_replace(parent_path, "^?+") 
                + '/' + Path::regex_replace(short_name, "+");
        root_path = parent_path;
    }

    if (!Path::exist(root_path)) {
        error_message->assign("path:");
        error_message->append(root_path);
        error_message->append(" is not exist");
        WARNING_LOG("%s", error_message->c_str());
        return false;
    }

    detail::FileFilter filter(root_path, reg_path, args.include_regex, args.exclude_regex);
    if (filter.is_error()) {
        return false;
    }

    int flags = 0;
    if (args.flags & MakeTorrentArgs::SYMLINKS) {
        flags |= create_torrent::symlinks;
    }
    if (args.flags & MakeTorrentArgs::FILE_HASH) {
        flags |= create_torrent::calculate_file_hashes;
    }
    if (!(args.flags & MakeTorrentArgs::PIECE_HASH)) {
        flags |= create_torrent::no_piece_hashes;
    }

    libtorrent::file_storage fs;
    add_files(fs, root_path, filter, flags);
    if (fs.num_files() == 0) {
        error_message->assign("mkseed failed: no files");
        WARNING_LOG("%s", error_message->c_str());
        return false;
    }

    create_torrent t(fs, args.piece_size, -1, flags);
    if (!args.cluster_config.empty()) {
        message::SourceURI cluster_uri;
        if (!parse_uri_entry(args.cluster_config, &cluster_uri)) {
            WARNING_LOG("invalid cluster uri: %s", args.cluster_config.c_str());
            return false;
        }
        t.set_cluster_config(cluster_uri.protocol(), cluster_uri.host(), cluster_uri.port(),
                cluster_uri.user(), cluster_uri.passwd(), cluster_uri.path());
    }

    t.set_creator(args.creator.c_str());
    if (!args.comment.empty()) {
        t.set_comment(args.comment.c_str());
    }
    if (fs.num_files() != 0
        && (args.flags & MakeTorrentArgs::PIECE_HASH)
        && !(args.flags & MakeTorrentArgs::NO_CALCULATE_HASH)) {
        if (!(args.flags & MakeTorrentArgs::BEST_HASH)) {
            libtorrent::error_code ec;
            libtorrent::set_piece_hashes(t, libtorrent::parent_path(root_path),
                    boost::bind(&detail::print_progress, _1, t.num_pieces()), ec);
            if (ec) {
                WARNING_LOG("mkseed failed: %s", ec.message().c_str());
                return false;
            }
        } else {
            detail::QuickPiecesHashes quick_hashes(t, libtorrent::parent_path(root_path), 2);
            if (!quick_hashes.set_piece_hashes()) {
                WARNING_LOG("mkseed failed: hash pieces failed");
                return false;
            }
        }
    }

    for (vector<string>::const_iterator it = args.web_seeds.begin();
            it != args.web_seeds.end(); ++it) {
        t.add_url_seed(*it);
    }

    torrent->clear();
    bencode(back_inserter(*torrent), t.generate());
    hex_encode(t.get_info_hash().to_string(), infohash);

    // upload torrent to torrent-provider
    if (args.flags & MakeTorrentArgs::UPLOAD_TORRENT) {
        TorrentProvider provider;
        std::string source;
        if (!args.cluster_config.empty()) {
            source = args.cluster_config;
        } else if (args.web_seeds.size() > 0) {
            // if use web seeds, store the first one
            source = *(args.web_seeds.begin());
        } else {
            source = "gko3://" + g_process_info->hostname() + root_path;
        }
        if (!provider.upload_torrent_file(*infohash, source, *torrent)) {
            error_message->assign("upload torrent file to provider failed!");
            WARNING_LOG("%s", error_message->c_str());
            return false;
        }
    }

    return true;
}

bool dump_torrent(const std::string &filename, bool debug) {
    int64_t max_file_size = 10 * 1024 * 1024;
    error_code ec;
    int64_t size = File::size(filename.c_str(), ec);
    if (ec) {
        WARNING_LOG("torrent %s size failed: %s", filename.c_str(), ec.message().c_str());
        return false;
    }
    if (size <= 0) {
        WARNING_LOG("torrent %s is empty", filename.c_str());
        return false;
    }
    if (size > max_file_size) {
        WARNING_LOG("torrent %s size[< %ldMB] invalid: %ldMB",
                filename.c_str(), max_file_size / 1024 / 1024, size / 1024 / 1024);
        return false;
    }

    std::vector<char> buffer;
    int ret = File::read(filename, &buffer, ec, max_file_size);
    if (ret < 0) {
        WARNING_LOG("load torrent %s failed: %s", filename.c_str(), ec.message().c_str());
        return false;
    }

    int pos = 0;
    libtorrent::lazy_entry lazy_e;
    ret = libtorrent::lazy_bdecode(
            &buffer[0], &buffer[0] + buffer.size(), lazy_e, ec, &pos, 1000, 10000000);
    if (ret != 0) {
        WARNING_LOG("decode torrent failed: '%s' at character %d", ec.message().c_str(), pos);
        return false;
    }
    if (debug) {
        printf("%s\n", libtorrent::print_entry(lazy_e).c_str());
    }

    libtorrent::torrent_info t(lazy_e, ec);
    if (ec) {
        WARNING_LOG("parse torrent failed: %s", ec.message().c_str());
        return false;
    }
    lazy_e.clear();
    std::vector<char>().swap(buffer);

    char ih[41];
    libtorrent::to_hex((char const*)&t.info_hash()[0], 20, ih);
    printf("total size: %ld\n"
            "number of pieces: %d\n"
            "piece length: %d\n"
            "info hash: %s\n"
            "comment: %s\n"
            "creator: %s\n"
            "name: %s\n"
            "number of files: %d\n"
            "cluster path: %s://%s:%s@%s:%d%s\n"
            "paths:\n",
            t.total_size(),
            t.num_pieces(),
            t.piece_length(),
            ih,
            t.comment().c_str(),
            t.creator().c_str(),
            t.name().c_str(),
            t.num_files(),
            t.cluster_config().protocol.c_str(),
            t.cluster_config().user.c_str(), t.cluster_config().passwd.c_str(),
            t.cluster_config().host.c_str(), t.cluster_config().port,
            t.cluster_config().prefix_path.c_str());

    t.files().print_paths();
    if (t.files().has_symlinks()) {
        printf("symlinks:\n");
        t.files().print_symlinks();
    }

    printf("files:\n");
    for (int index = 0; index < t.num_files(); ++index) {
        libtorrent::file_entry fe = t.file_at(index);
        int first = t.map_file(index, 0, 0).piece;
        int last = t.map_file(
                index,
                (std::max)(libtorrent::size_type(fe.size) - 1, libtorrent::size_type(0)),
                0).piece;
        printf("  %11"PRId64" %c %o [ %4d, %4d ] %7u %s %s %s%s\n",
                fe.size,
                (fe.pad_file ? 'p' : '-'),
                fe.mode,
                first,
                last,
                boost::uint32_t(t.files().mtime(index)),
                t.files().hash(index) != libtorrent::sha1_hash(0) ?
                        libtorrent::to_hex(t.files().hash(index).to_string()).c_str() : "",
                t.files().file_path(index).c_str(),
                fe.symlink_attribute ? "-> " : "",
                fe.symlink_attribute ? t.files().symlink(index).c_str() : "");
    }
    return true;
}

namespace detail {

int delete_files_cb(const char* path, const struct stat* statbuf, int type, struct FTW* ftw_info,
        int base, const torrent_info &ti) {
    string base_path(path + base);
    switch (type) {
    case FTW_F: {
        if (!(statbuf->st_mode & S_IFREG)) {  //非规则文件，如pipe等，则跳过
            WARNING_LOG("skip pipe, sock file: %s", path);
            break;
        }
        bool found = false;
        for (int i = 0; i < ti.num_files(); ++i) {
            libtorrent::file_entry fe = ti.file_at(i);
            if (fe.path == base_path) {
                found = true;
                break;
            }
        }
        if (!found) {
            if (unlink(path) != 0) {
                WARNING_LOG("delete file(%s) error(%d): %s\n ", path, errno, strerror(errno));
            } else {
                WARNING_LOG("delete file: %s", path);
            }
        } else {
            WARNING_LOG("file(%s) is in torrent", base_path.c_str());
        }
        break;
    }

    case FTW_SL:  // no break;
    case FTW_SLN: {
        const vector<symlink_file> &symlinks = ti.files().get_symlink_file();
        bool found = false;
        for (vector<symlink_file>::const_iterator it = symlinks.begin();
                it != symlinks.end(); ++it) {
            if (it->path == base_path) {
                found = true;
                break;
            }
        }
        if (!found) {
            if (unlink(path) != 0) {
                WARNING_LOG("delete symlink file(%s) error(%d): %s", path, errno, strerror(errno));
            } else {
                printf("delete symlink file: %s\n", path);
            }
        } else {
            printf("symlink file(%s) is in torrent\n", base_path.c_str());
        }
        break;
    }

    case FTW_DP: {
        base_path.append("/");
        printf("path: %s\n", base_path.c_str());
        const vector<string>& paths = ti.get_paths();
        int path_len = paths.size();
        bool found = false;
        for (int i = 0; i < path_len; ++i) {
            if (paths[i] == base_path) {
                found = true;
            }
        }
        if (!found) {
            if (rmdir(path) != 0) {
                WARNING_LOG("delete path(%s) error(%d): %s", path, errno, strerror(errno));
            } else {
                printf("delete path: %s\n", path);
            }
        } else {
            printf("path(%s) is in torrent\n", base_path.c_str());
        }
        break;
    }

    case FTW_DNR:
        WARNING_LOG("find a not be read path: %s", path);
        return FTW_STOP;

    case FTW_NS:  // no break
    default:
        WARNING_LOG("%s can't process, skip", path);
        return FTW_STOP;
    }

    return FTW_CONTINUE;
}

bool delete_files_outof_torrent(const torrent_info &ti, const string &save_path,
        const string &new_name, bool symlinks) {
    const static int NFTW_DEPTH = 10;
    int nftw_flag = FTW_ACTIONRETVAL | FTW_DEPTH;
    if (symlinks) {
        nftw_flag |= FTW_PHYS;
    }
    string root_path = save_path + '/' + (new_name.empty() ? ti.name() : new_name);
    if (Path::nftw(root_path,
            boost::bind(&delete_files_cb, _1, _2, _3, _4, save_path.length() + 1, ti),
            NFTW_DEPTH, nftw_flag) != 0) {
        printf("nftw path(%s) error(%d): %s.\n", root_path.c_str(), errno, strerror(errno));
        return false;
    }
    return true;
}

bool delete_files_in_torrent() {
    return true;
}

}

bool delete_files_by_torrent(const DeleteFilesArgs &args) {
    if (!args.flags == 0) {
        WARNING_LOG("you must set delete in or not in torrent flag");
        return false;
    }

    if (args.torrent_path.empty() || args.save_path.empty()) {
        WARNING_LOG("not set torrent file or save path or rm type");
        return false;
    }

    int64_t size = libtorrent::file_size(args.torrent_path.c_str());
    if (size > 50 * 1024 * 1024 || size <= 0) {
        WARNING_LOG("file too big (%ld) > 50MB or empty, aborting", size);
        return false;
    }

    error_code ec;
    torrent_info ti(args.torrent_path, ec, 0, args.new_name);
    if (ec) {
        WARNING_LOG("error: %s", ec.message().c_str());
        return false;
    }

    if (args.flags & DeleteFilesArgs::DELETE_IN_TORRENT) {
        return detail::delete_files_in_torrent();
    }
    return detail::delete_files_outof_torrent(ti, args.save_path, args.new_name,
            args.flags & DeleteFilesArgs::SYMLINKS);
}

bool make_torrent_dir(
        const torrent_info &ti,
        const string save_path,
        boost::function<bool (const string &)> pred) {
    const vector<string>& paths = ti.get_paths();
    const vector<mode_t>& modes = ti.get_paths_mode();
    int path_len = paths.size();
    for (int i = 0; i < path_len; ++i) {
        string path = save_path + "/" + paths[i];
        if (!pred(path)) {
            continue;
        }
        mode_t mode = modes[i];
        struct stat statbuf;
        if (stat(path.c_str(), &statbuf) != 0) {
            if (!Path::mkdirs(path.c_str(), mode) != 0) {
                WARNING_LOG("mkdir %s(%o) failed.", path.c_str(), mode);
                return false;
            }
        } else if (statbuf.st_mode & S_IFDIR) {
            if (chmod(path.c_str(), mode) != 0) {
                WARNING_LOG("path(%s) chmod to %o failed.", path.c_str(), mode);
                return false;
            }
        } else {
            WARNING_LOG("path %s is not a dir, can't change mode.", path.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace bbts
