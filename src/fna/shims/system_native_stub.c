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
#include <stdint.h>

typedef struct {
    int32_t Flags;
    int32_t Errno;
    int32_t Dev;
    int32_t _pad0;
    int64_t Ino;
    int32_t Mode;
    int32_t Uid;
    int32_t Gid;
    int32_t Rdev;
    int64_t Size;
    int64_t AtimSec;
    int64_t AtimNsec;
    int64_t MtimSec;
    int64_t MtimNsec;
    int64_t CtimSec;
    int64_t CtimNsec;
    int64_t Blocks;
} FileStatus;

static void fill_file_status(FileStatus* fs, const struct stat* s, int32_t err) {
    fs->Flags = 0;
    fs->Errno = err;
    if (err != 0) return;
    fs->Dev = (int32_t)s->st_dev;
    fs->Ino = (int64_t)s->st_ino;
    fs->Mode = (int32_t)s->st_mode;
    fs->Uid = (uint32_t)s->st_uid;
    fs->Gid = (uint32_t)s->st_gid;
    fs->Rdev = (int32_t)s->st_rdev;
    fs->Size = (int64_t)s->st_size;
    fs->AtimSec = (int64_t)s->st_atimespec.tv_sec;
    fs->AtimNsec = (int64_t)s->st_atimespec.tv_nsec;
    fs->MtimSec = (int64_t)s->st_mtimespec.tv_sec;
    fs->MtimNsec = (int64_t)s->st_mtimespec.tv_nsec;
    fs->CtimSec = (int64_t)s->st_ctimespec.tv_sec;
    fs->CtimNsec = (int64_t)s->st_ctimespec.tv_nsec;
    fs->Blocks = (int64_t)s->st_blocks;
}

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

int32_t SystemNative_Stat2(const char* path, int32_t* len, FileStatus* fs) {
    (void)len;
    struct stat s;
    if (stat(path, &s) != 0) {
        fill_file_status(fs, &s, errno);
        return errno;
    }
    fill_file_status(fs, &s, 0);
    return 0;
}

int32_t SystemNative_LStat(const char* path, int32_t* len, struct stat* buf) {
    (void)len;
    if (lstat(path, buf) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_LStat2(const char* path, int32_t* len, FileStatus* fs) {
    (void)len;
    struct stat s;
    if (lstat(path, &s) != 0) {
        fill_file_status(fs, &s, errno);
        return errno;
    }
    fill_file_status(fs, &s, 0);
    return 0;
}

int32_t SystemNative_FStat(int32_t fd, struct stat* buf) {
    if (fstat(fd, buf) != 0) {
        return errno;
    }
    return 0;
}

int32_t SystemNative_FStat2(int32_t fd, FileStatus* fs) {
    struct stat s;
    if (fstat(fd, &s) != 0) {
        fill_file_status(fs, &s, errno);
        return errno;
    }
    fill_file_status(fs, &s, 0);
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
