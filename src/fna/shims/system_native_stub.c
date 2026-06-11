#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

typedef struct {
    int32_t Flags;
    int32_t Mode;
    uint32_t Uid;
    uint32_t Gid;
    int64_t Size;
    int64_t ATime;
    int64_t ATimeNsec;
    int64_t MTime;
    int64_t MTimeNsec;
    int64_t CTime;
    int64_t CTimeNsec;
    int64_t BirthTime;
    int64_t BirthTimeNsec;
    int64_t Dev;
    int64_t Ino;
} FileStatus;

typedef struct {
    const char* Name;
    int32_t NameLength;
    int32_t InodeType;
} DirectoryEntry;

static void fill_file_status(FileStatus* fs, const struct stat* s) {
    memset(fs, 0, sizeof(*fs));
    fs->Flags = 0;
    fs->Mode = (int32_t)s->st_mode;
    fs->Uid = (uint32_t)s->st_uid;
    fs->Gid = (uint32_t)s->st_gid;
    fs->Size = (int64_t)s->st_size;
    fs->ATime = (int64_t)s->st_atimespec.tv_sec;
    fs->ATimeNsec = (int64_t)s->st_atimespec.tv_nsec;
    fs->MTime = (int64_t)s->st_mtimespec.tv_sec;
    fs->MTimeNsec = (int64_t)s->st_mtimespec.tv_nsec;
    fs->CTime = (int64_t)s->st_ctimespec.tv_sec;
    fs->CTimeNsec = (int64_t)s->st_ctimespec.tv_nsec;
    fs->BirthTime = (int64_t)s->st_birthtimespec.tv_sec;
    fs->BirthTimeNsec = (int64_t)s->st_birthtimespec.tv_nsec;
    fs->Dev = (int64_t)s->st_dev;
    fs->Ino = (int64_t)s->st_ino;
}

int32_t SystemNative_LChflagsCanSetHiddenFlag(void) {
    return 0;
}

void SystemNative_GetNonCryptographicallySecureRandomBytes(uint8_t* buffer, int32_t length) {
    arc4random_buf(buffer, (size_t)length);
}

int32_t SystemNative_ConvertErrorPlatformToPal(int32_t platformErrno) {
    return platformErrno;
}

int32_t SystemNative_Stat(const char* path, FileStatus* fs) {
    struct stat s;
    if (stat(path, &s) != 0) {
        return -1;
    }
    fill_file_status(fs, &s);
    return 0;
}

int32_t SystemNative_Stat2(const char* path, FileStatus* fs) {
    struct stat s;
    if (stat(path, &s) != 0) {
        return -1;
    }
    fill_file_status(fs, &s);
    return 0;
}

int32_t SystemNative_LStat(const char* path, FileStatus* fs) {
    struct stat s;
    if (lstat(path, &s) != 0) {
        return -1;
    }
    fill_file_status(fs, &s);
    return 0;
}

int32_t SystemNative_LStat2(const char* path, FileStatus* fs) {
    struct stat s;
    if (lstat(path, &s) != 0) {
        return -1;
    }
    fill_file_status(fs, &s);
    return 0;
}

int32_t SystemNative_FStat(intptr_t fd, FileStatus* fs) {
    struct stat s;
    if (fstat((int)fd, &s) != 0) {
        return -1;
    }
    fill_file_status(fs, &s);
    return 0;
}

int32_t SystemNative_FStat2(intptr_t fd, FileStatus* fs) {
    struct stat s;
    if (fstat((int)fd, &s) != 0) {
        return -1;
    }
    fill_file_status(fs, &s);
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

int32_t SystemNative_ReadLink(const char* path, char* buf, int32_t bufSize) {
    ssize_t r = readlink(path, buf, (size_t)bufSize);
    if (r < 0) {
        return -1;
    }
    return (int32_t)r;
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

int32_t SystemNative_GetReadDirRBufferSize(void) {
    return (int32_t)sizeof(struct dirent);
}

int32_t SystemNative_ReadDirR(DIR* dir, void* buffer, int32_t bufferSize, DirectoryEntry* outputEntry) {
    if (dir == NULL || buffer == NULL || outputEntry == NULL || bufferSize < (int32_t)sizeof(struct dirent)) {
        errno = EINVAL;
        return EINVAL;
    }

    errno = 0;
    struct dirent* entry = readdir(dir);
    if (entry == NULL) {
        memset(outputEntry, 0, sizeof(*outputEntry));
        return errno == 0 ? -1 : errno;
    }

    size_t reclen = entry->d_reclen;
    if (reclen > (size_t)bufferSize) {
        errno = ERANGE;
        memset(outputEntry, 0, sizeof(*outputEntry));
        return ERANGE;
    }

    memcpy(buffer, entry, reclen);
    struct dirent* copied = (struct dirent*)buffer;
    outputEntry->Name = copied->d_name;
    outputEntry->NameLength = copied->d_namlen;
    outputEntry->InodeType = copied->d_type;
    return 0;
}

DIR* SystemNative_OpenDir(const char* path) {
    return opendir(path);
}

int32_t SystemNative_CloseDir(DIR* dir) {
    return closedir(dir);
}

int32_t SystemNative_GetReadLinkSymlinkMaximumSize(void) {
    return 1024;
}

int32_t SystemNative_Open(const char* path, int32_t flags, int32_t mode) {
    int fd = open(path, flags, (mode_t)mode);
    if (fd < 0) {
        return -1;
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
        return -1;
    }
    return r;
}
