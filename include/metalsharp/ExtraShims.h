/// @file ExtraShims.h
/// @brief Factory functions for user32, gdi32, advapi32, and other Win32 DLL shims.
///
/// Provides ShimLibrary creation for the remaining Win32 DLLs that games commonly import:
/// user32 (window management), gdi32 (GDI drawing stubs), advapi32 (registry and security),
/// ws2_32 (networking), shell32 (shell operations), ole32/oleaut32 (COM stubs), crypt32
/// (crypto), psapi (process info), version (version info), bcrypt (cryptography), comctl32
/// (common controls), and wsock32 (legacy networking). Also includes helpers for patching
/// additional kernel32 exports and DRM-specific shims.

#pragma once

#include "PELoader.h"

namespace metalsharp {
namespace win32 {

ShimLibrary createUser32Shim();
ShimLibrary createGdi32Shim();
ShimLibrary createAdvapi32Shim();
ShimLibrary createWs2_32Shim();
ShimLibrary createShell32Shim();
ShimLibrary createOle32Shim();
ShimLibrary createOleAut32Shim();
ShimLibrary createCrypt32Shim();
ShimLibrary createPsapiShim();
ShimLibrary createVersionShim();
ShimLibrary createBcryptShim();
ShimLibrary createComCtl32Shim();
ShimLibrary createWsock32Shim();
void addMissingKernel32(ShimLibrary& lib);
void addDRMShims(ShimLibrary& kernel32, ShimLibrary& winmm);

}
}
