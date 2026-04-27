#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>

namespace metalsharp {
namespace win32 {

struct SSLSession {
    int socketFd;
    void* sslContext;
    bool connected;
};

class SecureTransport {
public:
    static SecureTransport& instance();

    void* createSSLSession(int socketFd);
    void destroySSLSession(void* handle);
    int sslRead(void* handle, char* buf, int len);
    int sslWrite(void* handle, const char* buf, int len);
    int sslConnect(void* handle, const char* hostname);

    void initialize();

private:
    SecureTransport() = default;

    mutable std::mutex m_mutex;
    std::unordered_map<intptr_t, SSLSession> m_sessions;
    int m_nextHandle = 10000;
};

}
}
