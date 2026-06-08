#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

int32_t SystemNative_LChflagsCanSetHiddenFlag(void) {
    return 0;
}

int32_t SystemNative_Stat(const char* path, int32_t* len, struct stat* buf) {
    (void)len;
    if (stat(path, buf) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_LStat(const char* path, int32_t* len, struct stat* buf) {
    (void)len;
    if (lstat(path, buf) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_FStat(int32_t fd, struct stat* buf) {
    if (fstat(fd, buf) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_Chmod(const char* path, int32_t mode) {
    if (chmod(path, (mode_t)mode) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_FChmod(int32_t fd, int32_t mode) {
    if (fchmod(fd, (mode_t)mode) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_Link(const char* source, const char* target) {
    if (link(source, target) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_Symlink(const char* target, const char* linkpath) {
    if (symlink(target, linkpath) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_ReadLink(const char* path, char* buf, int32_t bufSize, int32_t* written) {
    ssize_t r = readlink(path, buf, (size_t)bufSize);
    if (r < 0) {
        return errno;
    }
    *written = (int32_t)r;
    return 0;
}

int32_t SystemNative_Unlink(const char* path) {
    if (unlink(path) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_Rename(const char* oldPath, const char* newPath) {
    if (rename(oldPath, newPath) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_Rmdir(const char* path) {
    if (rmdir(path) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_Mkdir(const char* path, int32_t mode) {
    if (mkdir(path, (mode_t)mode) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_Access(const char* path, int32_t mode) {
    if (access(path, mode) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_Utimes(const char* path, int64_t atime, int64_t mtime) {
    struct timeval tv[2];
    tv[0].tv_sec = atime / 1000000;
    tv[0].tv_usec = (suseconds_t)(atime % 1000000);
    tv[1].tv_sec = mtime / 1000000;
    tv[1].tv_usec = (suseconds_t)(mtime % 1000000);
    if (utimes(path, tv) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_Truncate(const char* path, int64_t length) {
    if (truncate(path, length) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_TruncateFile(int32_t fd, int64_t length) {
    if (ftruncate(fd, length) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_GetReadLinkSymlinkMaximumSize(void) {
    return 1024;
}

int32_t SystemNative_Open(const char* path, int32_t flags, int32_t mode) {
    int fd = open(path, flags, (mode_t)mode);
    if (fd < 0) {
        return -errno;
    }
    return fd;
}

int32_t SystemNative_Close(int32_t fd) {
    if (close(fd) != 0) {
        return errno;
    }
    return 0;
}

int64_t SystemNative_LSeek(int32_t fd, int64_t offset, int32_t whence) {
    off_t r = lseek(fd, offset, whence);
    if (r < 0) {
        return -errno;
    }
    return r;
}
