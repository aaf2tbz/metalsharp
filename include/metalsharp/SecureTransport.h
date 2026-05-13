/// @file SecureTransport.h
/// @brief SSL/TLS implementation via Apple's SecureTransport framework.
///
/// Wraps Apple's SecureTransport/SSLContext API to provide TLS session management for
/// games that use HTTPS or encrypted socket connections. Supports session creation,
/// connect/read/write operations, and cleanup — all mapped from Winsock's SSL patterns
/// to Apple's native TLS stack. Thread-safe session table keyed by opaque handles.

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

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

} // namespace win32
} // namespace metalsharp
