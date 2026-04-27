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
