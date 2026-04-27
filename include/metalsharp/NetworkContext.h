#pragma once

#include <cstdint>
#include <unordered_map>
#include <string>
#include <mutex>
#include <functional>

namespace metalsharp {
namespace win32 {

constexpr uint32_t WSAEWOULDBLOCK = 10035;
constexpr uint32_t WSAEINPROGRESS = 10036;
constexpr uint32_t WSAEALREADY = 10037;
constexpr uint32_t WSAENOTSOCK = 10038;
constexpr uint32_t WSAEDESTADDRREQ = 10039;
constexpr uint32_t WSAEMSGSIZE = 10040;
constexpr uint32_t WSAEPROTOTYPE = 10041;
constexpr uint32_t WSAENOPROTOOPT = 10042;
constexpr uint32_t WSAEPROTONOSUPPORT = 10043;
constexpr uint32_t WSAESOCKTNOSUPPORT = 10044;
constexpr uint32_t WSAEOPNOTSUPP = 10045;
constexpr uint32_t WSAEPFNOSUPPORT = 10046;
constexpr uint32_t WSAEAFNOSUPPORT = 10047;
constexpr uint32_t WSAEADDRINUSE = 10048;
constexpr uint32_t WSAEADDRNOTAVAIL = 10049;
constexpr uint32_t WSAENETDOWN = 10050;
constexpr uint32_t WSAENETUNREACH = 10051;
constexpr uint32_t WSAENETRESET = 10052;
constexpr uint32_t WSAECONNABORTED = 10053;
constexpr uint32_t WSAECONNRESET = 10054;
constexpr uint32_t WSAENOBUFS = 10055;
constexpr uint32_t WSAEISCONN = 10056;
constexpr uint32_t WSAENOTCONN = 10057;
constexpr uint32_t WSAESHUTDOWN = 10058;
constexpr uint32_t WSAETIMEDOUT = 10060;
constexpr uint32_t WSAECONNREFUSED = 10061;
constexpr uint32_t WSAEHOSTDOWN = 10064;
constexpr uint32_t WSAEHOSTUNREACH = 10065;
constexpr uint32_t WSASYSCALLFAILURE = 10107;

constexpr uint32_t FD_READ_BIT = 0;
constexpr uint32_t FD_WRITE_BIT = 1;
constexpr uint32_t FD_ACCEPT_BIT = 3;
constexpr uint32_t FD_CONNECT_BIT = 4;
constexpr uint32_t FD_CLOSE_BIT = 5;

constexpr uint32_t FD_READ = (1 << FD_READ_BIT);
constexpr uint32_t FD_WRITE = (1 << FD_WRITE_BIT);
constexpr uint32_t FD_ACCEPT = (1 << FD_ACCEPT_BIT);
constexpr uint32_t FD_CONNECT = (1 << FD_CONNECT_BIT);
constexpr uint32_t FD_CLOSE = (1 << FD_CLOSE_BIT);

constexpr uint32_t SIO_GET_EXTENSION_FUNCTION_POINTER = 0xC8000006;

struct WSAOVERLAPPED {
    void* Internal;
    void* InternalHigh;
    union {
        struct { void* Offset; void* OffsetHigh; };
        void* Pointer;
    };
    void* hEvent;
};

struct WSABUF {
    uint32_t len;
    char* buf;
};

struct PipeInstance {
    int fds[2];
    std::string name;
    bool serverSide;
    bool connected;
};

class NetworkContext {
public:
    static NetworkContext& instance();

    int allocSocket(int fd);
    int releaseSocket(int handle);
    int getFd(int handle) const;

    void setWsaError(uint32_t err);
    uint32_t getWsaError() const;

    uint32_t mapErrnoToWsa(int err) const;

    int* allocPipePair(const std::string& name, bool server);
    int getPipeReadFd(int handle) const;
    int getPipeWriteFd(int handle) const;
    void closePipe(int handle);

    void setSocketEventMask(int handle, uint32_t mask, void* eventHandle);
    uint32_t getSocketEventMask(int handle) const;

    void initialize();

private:
    NetworkContext() = default;

    struct SocketEntry {
        int fd;
        uint32_t eventMask = 0;
        void* eventHandle = nullptr;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<int, SocketEntry> m_sockets;
    int m_nextHandle = 100;

    std::unordered_map<int, PipeInstance> m_pipes;
    int m_nextPipe = 5000;

    static thread_local uint32_t t_wsaError;
};

}
}
