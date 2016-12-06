/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   file_is_locked.cpp
 *
 * @author liuming03
 * @date   2013-11-13
 * @brief 
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <string>

bool lock_file(const std::string &filename) {
    int fd = open(filename.c_str(), O_WRONLY | O_CREAT, 0600);
    if (fd < 0) {
        return false;
    }
    struct flock flockbuf = { F_WRLCK, 0, SEEK_SET, 0, 0 };
    if (fcntl(fd, F_SETLK, &flockbuf) < 0) {
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s filename\n", argv[0]);
        return 1;
    }

    if (lock_file(argv[1])) {
        fprintf(stdout, "flock file %s success, is unlocked.\n", argv[1]);
        return 1;
    }
    fprintf(stderr, "flock file %s failed: %s\n", argv[1], strerror(errno));
    return 0;
}
