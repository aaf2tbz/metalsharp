#include <metalsharp/SecureTransport.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <unistd.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <Security/Security.h>
#include <Security/SecureTransport.h>
#pragma clang diagnostic pop

namespace metalsharp {
namespace win32 {

SecureTransport& SecureTransport::instance() {
    static SecureTransport st;
    return st;
}

void SecureTransport::initialize() {
    MS_INFO("SecureTransport: initializing TLS bridge");
}

static OSStatus sslReadCallback(SSLConnectionRef conn, void* data, size_t* len) {
    int fd = (int)(intptr_t)conn;
    ssize_t n = read(fd, data, *len);
    if (n < 0) { *len = 0; return errSSLClosedAbort; }
    *len = (size_t)n;
    return noErr;
}

static OSStatus sslWriteCallback(SSLConnectionRef conn, const void* data, size_t* len) {
    int fd = (int)(intptr_t)conn;
    ssize_t n = write(fd, data, *len);
    if (n < 0) { *len = 0; return errSSLClosedAbort; }
    *len = (size_t)n;
    return noErr;
}

void* SecureTransport::createSSLSession(int socketFd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int handle = m_nextHandle++;

    SSLSession session;
    session.socketFd = socketFd;
    session.sslContext = nullptr;
    session.connected = false;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    SSLContextRef sslCtx = SSLCreateContext(kCFAllocatorDefault, kSSLClientSide, kSSLStreamType);
    if (sslCtx) {
        SSLSetIOFuncs(sslCtx, sslReadCallback, sslWriteCallback);
        SSLSetConnection(sslCtx, reinterpret_cast<SSLConnectionRef>((intptr_t)socketFd));
        SSLSetSessionOption(sslCtx, kSSLSessionOptionBreakOnServerAuth, false);
        SSLSetSessionOption(sslCtx, kSSLSessionOptionBreakOnCertRequested, false);
        session.sslContext = sslCtx;
    }
#pragma clang diagnostic pop

    m_sessions[handle] = session;
    return reinterpret_cast<void*>(static_cast<intptr_t>(handle));
}

void SecureTransport::destroySSLSession(void* handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find((intptr_t)handle);
    if (it == m_sessions.end()) return;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if (it->second.sslContext) {
        SSLContextRef sslCtx = static_cast<SSLContextRef>(it->second.sslContext);
        SSLClose(sslCtx);
        CFRelease(sslCtx);
    }
#pragma clang diagnostic pop

    m_sessions.erase(it);
}

int SecureTransport::sslConnect(void* handle, const char* hostname) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find((intptr_t)handle);
    if (it == m_sessions.end() || !it->second.sslContext) return -1;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    SSLContextRef sslCtx = static_cast<SSLContextRef>(it->second.sslContext);

    if (hostname) {
        SSLSetPeerDomainName(sslCtx, hostname, strlen(hostname));
    }

    OSStatus status = SSLHandshake(sslCtx);
    if (status == noErr) {
        it->second.connected = true;
        return 0;
    }
#pragma clang diagnostic pop

    if (status == errSSLWouldBlock) return -1;

    MS_INFO("SecureTransport: SSLHandshake failed with %ld", (long)status);
    return -1;
}

int SecureTransport::sslRead(void* handle, char* buf, int len) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find((intptr_t)handle);
    if (it == m_sessions.end() || !it->second.sslContext) return -1;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    SSLContextRef sslCtx = static_cast<SSLContextRef>(it->second.sslContext);
    size_t processed = 0;
    OSStatus status = SSLRead(sslCtx, buf, (size_t)len, &processed);
#pragma clang diagnostic pop

    if (status == noErr || status == errSSLWouldBlock) {
        return (int)processed;
    }

    if (status == errSSLClosedGraceful) return 0;
    return -1;
}

int SecureTransport::sslWrite(void* handle, const char* buf, int len) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find((intptr_t)handle);
    if (it == m_sessions.end() || !it->second.sslContext) return -1;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    SSLContextRef sslCtx = static_cast<SSLContextRef>(it->second.sslContext);
    size_t processed = 0;
    OSStatus status = SSLWrite(sslCtx, buf, (size_t)len, &processed);
#pragma clang diagnostic pop

    if (status == noErr || status == errSSLWouldBlock) {
        return (int)processed;
    }
    return -1;
}

}
}
