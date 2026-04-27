#pragma once

#include <metalsharp/Win32Types.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <dirent.h>

namespace metalsharp {
namespace win32 {

enum class HandleType : uint8_t {
    File,
    Find,
    Event,
    Mutex,
    Semaphore,
    Thread,
    Pipe,
    Socket,
    Null
};

struct FileState {
    int fd;
    std::string path;
    int64_t position;
};

struct FindState {
    DIR* dir;
    std::string pattern;
    std::string directory;
    bool firstCalled;
};

struct HandleEntry {
    HandleType type;
    void* data;
};

class VirtualFileSystem {
public:
    static VirtualFileSystem& instance();

    void setPrefix(const std::string& prefix);

    std::string winToHost(const std::string& winPath);
    std::string hostToWin(const std::string& hostPath);

    HANDLE allocHandle(HandleType type, void* data);
    HandleEntry* getHandle(HANDLE h);
    bool closeHandle(HANDLE h);

    HANDLE createFile(const char* lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
        DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes);
    BOOL readFile(HANDLE hFile, void* lpBuffer, DWORD nNumberOfBytesToRead, DWORD* lpNumberOfBytesRead);
    BOOL writeFile(HANDLE hFile, const void* lpBuffer, DWORD nNumberOfBytesToWrite, DWORD* lpNumberOfBytesWritten);
    DWORD getFileSize(HANDLE hFile, DWORD* lpFileSizeHigh);
    BOOL getFileSizeEx(HANDLE hFile, int64_t* lpFileSize);
    DWORD setFilePointer(HANDLE hFile, LONG lDistanceToMove, LONG* lpDistanceToMoveHigh, DWORD dwMoveMethod);
    BOOL setFilePointerEx(HANDLE hFile, int64_t liDistanceToMove, int64_t* lpNewFilePointer, DWORD dwMoveMethod);
    BOOL flushFileBuffers(HANDLE hFile);
    DWORD getFileType(HANDLE hFile);

    HANDLE findFirstFileW(const char* pattern, void* lpFindFileData);
    BOOL findNextFileW(HANDLE hFindFile, void* lpFindFileData);
    BOOL findClose(HANDLE hFindFile);

    DWORD getFileAttributes(const std::string& path);
    BOOL getFileAttributesEx(const std::string& path, void* lpFileInformation);
    BOOL getFileInformationByHandle(HANDLE hFile, void* lpFileInformation);

    std::string getFullPathName(const std::string& path);

    HANDLE registerPipeFd(int fd);

private:
    VirtualFileSystem();

    std::string m_prefix;
    uintptr_t m_nextHandle = 0x00010000;
    std::unordered_map<uintptr_t, HandleEntry> m_handles;
};

}
}
