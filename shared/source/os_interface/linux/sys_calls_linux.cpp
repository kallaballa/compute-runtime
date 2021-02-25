/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/sys_calls.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <iostream>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace NEO {
namespace SysCalls {
int close(int fileDescriptor) {
    return ::close(fileDescriptor);
}
int open(const char *file, int flags) {
    return ::open(file, flags);
}
int ioctl(int fileDescriptor, unsigned long int request, void *arg) {
    return ::ioctl(fileDescriptor, request, arg);
}

void *dlopen(const char *filename, int flag) {
    return ::dlopen(filename, flag);
}

int access(const char *pathName, int mode) {
    return ::access(pathName, mode);
}

int readlink(const char *path, char *buf, size_t bufsize) {
    return static_cast<int>(::readlink(path, buf, bufsize));
}

int getDevicePath(int deviceFd, char *buf, size_t &bufSize) {
    struct stat st;
    if (fstat(deviceFd, &st)) {
        return -1;
    }

    snprintf(buf, bufSize, "/sys/dev/char/%d:%d",
             major(st.st_rdev), minor(st.st_rdev));

    return 0;
}
} // namespace SysCalls
} // namespace NEO
