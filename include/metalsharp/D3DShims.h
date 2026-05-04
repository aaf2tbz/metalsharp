/// @file D3DShims.h
/// @brief Factory functions for D3D/DXGI shim libraries.
///
/// Creates ShimLibrary instances that intercept Direct3D 11, Direct3D 12,
/// and DXGI calls. Each factory produces a name→function mapping that the
/// PELoader uses to resolve imports when a game loads d3d11.dll, d3d12.dll,
/// or dxgi.dll.

#pragma once

#include "PELoader.h"

namespace metalsharp {
namespace win32 {

ShimLibrary createD3D11Shim();
ShimLibrary createD3D12Shim();
ShimLibrary createDxgiShim();

}
}
