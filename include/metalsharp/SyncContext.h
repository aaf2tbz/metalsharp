#pragma once

#include <metalsharp/Win32Types.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <pthread.h>
#include <condition_variable>

namespace metalsharp {
namespace win32 {

enum class SyncHandleType : uint8_t {
    Event,
    Mutex,
    Semaphore,
    Thread,
    Timer
};

struct SyncEventState {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool signaled;
    bool manualReset;
};

struct SyncMutexState {
    pthread_mutex_t mutex;
    DWORD owningThread;
    uint32_t recursionCount;
};

struct SyncSemaphoreState {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int32_t count;
    int32_t maxCount;
};

struct SyncThreadState {
    pthread_t thread;
    std::atomic<DWORD> exitCode;
    std::atomic<bool> joined;
    std::atomic<bool> finished;
    pthread_mutex_t joinMutex;
    pthread_cond_t joinCond;
};

struct SyncHandle {
    SyncHandleType type;
    void* data;
    std::string name;
};

class SyncContext {
public:
    static SyncContext& instance();

    void* createEvent(bool manualReset, bool initialState, const std::string& name);
    bool setEvent(void* handle);
    bool resetEvent(void* handle);

    void* createMutex(bool initialOwner, const std::string& name);
    bool releaseMutex(void* handle);

    void* createSemaphore(int32_t initialCount, int32_t maxCount, const std::string& name);
    bool releaseSemaphore(void* handle, int32_t releaseCount, int32_t* prevCount);

    void* createThread(pthread_t thread);
    SyncThreadState* getThreadState(void* handle);

    uint32_t waitForSingleObject(void* handle, uint32_t milliseconds);
    uint32_t waitForMultipleObjects(uint32_t count, void** handles, bool waitAll, uint32_t milliseconds);

    void destroyHandle(void* handle);

    void initialize();

private:
    SyncContext() = default;

    uint32_t waitForEvent(SyncEventState* evt, uint32_t ms);
    uint32_t waitForMutex(SyncMutexState* mtx, uint32_t ms);
    uint32_t waitForSemaphore(SyncSemaphoreState* sem, uint32_t ms);
    uint32_t waitForThread(SyncThreadState* thr, uint32_t ms);

    std::mutex m_mutex;
    std::unordered_map<intptr_t, SyncHandle> m_handles;
    std::unordered_map<std::string, intptr_t> m_namedHandles;
    int m_nextHandle = 10000;
};

}
}
