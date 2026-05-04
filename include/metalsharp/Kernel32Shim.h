/// @file Kernel32Shim.h
/// @brief kernel32.dll shim factory providing Win32 API implementations.
///
/// Produces a ShimLibrary populated with implementations for kernel32.dll exports —
/// memory allocation (VirtualAlloc/HeapAlloc), file I/O (CreateFile/ReadFile/WriteFile),
/// module loading (LoadLibrary/GetProcAddress), and synchronization helpers. Maintains
/// internal state for handle allocation, file handle mapping, and virtual memory tracking.
/// This is the largest and most critical Win32 shim since virtually every game imports kernel32.

#pragma once

#include <unordered_map>
#include <string>
#include <functional>
#include "PELoader.h"

namespace metalsharp {
namespace win32 {

void setExePath(const char* path);

class Kernel32Shim {
public:
    static ShimLibrary create();

    static bool s_initialized;
    static std::unordered_map<uintptr_t, size_t> s_allocations;
    static std::unordered_map<uintptr_t, std::string> s_fileHandles;
    static uintptr_t s_nextHandle;
};

}
}
