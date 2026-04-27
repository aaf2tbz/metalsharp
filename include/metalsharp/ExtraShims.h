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

}
}
