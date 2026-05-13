/// @file NtdllShim.h
/// @brief ntdll.dll shim factory for low-level NT API implementations.
///
/// Produces a ShimLibrary for ntdll.dll exports used by games that bypass kernel32 and
/// call NT native APIs directly. Covers Rtl* memory functions, NtCreateFile/NtReadFile,
/// and the NT exception handling interface. Thin wrapper over the Kernel32Shim and
/// SyncContext infrastructure since most NT APIs map to POSIX equivalents.

#pragma once

#include "PELoader.h"

namespace metalsharp {
namespace win32 {

ShimLibrary createNtdllShim();

}
} // namespace metalsharp
