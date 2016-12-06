/***************************************************************************
 * 
 * Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   uri_agent.cpp
 *
 * @author liuming03
 * @date   2015年1月30日
 * @brief 
 */

#include <stdlib.h>
#include <boost/asio.hpp>
#include <libtorrent/io.hpp>
#include <bbts/encode.h>

using std::string;
using std::stringstream;
using std::vector;
using boost::asio::ip::tcp;
using boost::system::error_code;

void write_failed_message(tcp::socket &connect_socket, const string &error) {
    printf("%s\n", error.c_str());
    vector<char> msg_buffer;
    std::back_insert_iterator<vector<char> > ptr(msg_buffer);
    libtorrent::detail::write_uint32(3 + error.length(), ptr);
    libtorrent::detail::write_uint8(20, ptr);
    libtorrent::detail::write_uint8(20, ptr);
    libtorrent::detail::write_uint8(2, ptr);
    libtorrent::detail::write_string(error, ptr);
    error_code ec;
    boost::asio::write(connect_socket, boost::asio::buffer(msg_buffer), ec);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: %s port\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    tcp::endpoint listen_endpoint(tcp::v4(), port);

    error_code ec;
    boost::asio::io_service ios;
    tcp::acceptor acceptor(ios, listen_endpoint, true);

    for (;;) {
        tcp::socket connect_socket(ios);
        tcp::endpoint remote_endpoint;
        acceptor.accept(connect_socket, remote_endpoint, ec);
        if (ec) {
            printf("accept failed: %s\n", ec.message().c_str());
            return 1;
        }
        printf("accept from remote address: %s:%d\n",
                remote_endpoint.address().to_string(ec).c_str(), remote_endpoint.port());
        // read handshake
        vector<char> buffer(68);
        int readed = boost::asio::read(connect_socket, boost::asio::buffer(buffer), ec);
        if (ec) {
            printf("read handshake failed: %s\n", ec.message().c_str());
            return 1;
        }
        printf("readed handshake %d bytes\n", readed);
        string protocol(buffer.begin() + 1, buffer.begin() + 20);
        string infohash;
        string peerid;
        bbts::hex_encode(string(buffer.begin() + 28, buffer.begin() + 48), &infohash);
        bbts::hex_encode(string(buffer.begin() + 48, buffer.end()), &peerid);
        printf("%u %s %s %s\n", buffer[0], protocol.c_str(), infohash.c_str(), peerid.c_str());
        for (int i = 10; i < 18; ++i) {
            printf("%u ", buffer[i]);
        }
        printf("\n");
        // write handshake
        buffer[50] = 'a';
        boost::asio::write(connect_socket, boost::asio::buffer(buffer), ec);
        if (ec) {
            printf("write handshake failed\n");
            return 1;
        }
        for (;;) {
            uint32_t msg_len = 0;
            boost::asio::read(connect_socket, boost::asio::buffer(&msg_len, 4), ec);
            if (ec) {
                printf("read msg len failed: %s\n", ec.message().c_str());
                break;
            }
            char *ptr = (char *)&msg_len;
            msg_len = libtorrent::detail::read_uint32(ptr);
            printf("read msg len: %d\n", msg_len);
            if (msg_len <= 0) {
                continue;
            }
            buffer.resize(msg_len);
            boost::asio::read(connect_socket, boost::asio::buffer(buffer), ec);
            if (ec) {
                printf("read msg failed: %s\n", ec.message().c_str());
                break;
            }
            printf("bt msg type: %u\n", buffer[0]);
            if (buffer[0] != 20) {
                continue;
            }
            printf("bt plugin type: %u\n", buffer[1]);
            if (buffer[1] != 20) {
                continue;
            }
            printf("uri plugin type: %u\n", buffer[2]);
            vector<char>::iterator it = buffer.begin() + 3;
            uint32_t piece =  libtorrent::detail::read_uint32(it);
            uint32_t piece_offset =  libtorrent::detail::read_uint32(it);
            string save_path = &(*it);
            printf("piece: %u, offset: %u save_path: %s\n", piece, piece_offset, &(*it));
            it += save_path.length() + 1;
            uint32_t piece_length = 0;
            vector<char> piece_buffer(13);
            char *piece_buffer_ptr = &(piece_buffer[4]);
            libtorrent::detail::write_uint8(7, piece_buffer_ptr);
            libtorrent::detail::write_uint32(piece, piece_buffer_ptr);
            libtorrent::detail::write_uint32(piece_offset, piece_buffer_ptr);
            stringstream error;
            bool success = false;
            while (it != buffer.end()) {
                string filename = &(*it);
                it += filename.length() + 1;
                uint64_t file_offset = libtorrent::detail::read_uint64(it);
                uint32_t file_length = libtorrent::detail::read_uint32(it);
                piece_length += file_length;
                printf("filename: %s, offset: %lu, length: %u\n",
                        filename.c_str(), file_offset, file_length);
                filename = filename.empty() ? save_path : save_path + "/" + filename;
                FILE* fp = fopen(filename.c_str(), "rb");
                if (!fp) {
                    error << filename << " open failed: " << strerror(errno);
                    write_failed_message(connect_socket, error.str());
                    break;
                }
                int piece_buffer_length = piece_buffer.size();
                piece_buffer.resize(piece_buffer_length + file_length);
                if (fseek(fp, file_offset, SEEK_SET) != 0) {
                    error << filename << " seek failed: " << strerror(errno);
                    write_failed_message(connect_socket, error.str());
                    fclose(fp);
                    break;
                }
                int readed = fread(&piece_buffer[piece_buffer_length], 1, file_length, fp);
                if (readed != (int)file_length) {
                    error << filename << " read failed: " << strerror(errno);
                    write_failed_message(connect_socket, error.str());
                    fclose(fp);
                    break;
                }
                fclose(fp);
                success = true;
            };
            if (success) {
                printf("piece length: %u, piece_buffer length: %lu\n",
                        piece_length, piece_buffer.size());
                piece_buffer_ptr = &(piece_buffer[0]);
                libtorrent::detail::write_uint32(piece_buffer.size() - 4, piece_buffer_ptr);
                boost::asio::write(connect_socket, boost::asio::buffer(piece_buffer), ec);
            }
        }
    }
    return 0;
}

