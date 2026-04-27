#pragma once

#include "PELoader.h"

namespace metalsharp {
namespace win32 {

ShimLibrary createD3D11Shim();
ShimLibrary createD3D12Shim();
ShimLibrary createDxgiShim();

}
}
