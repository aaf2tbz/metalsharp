#pragma once

/*
 * libm12core public C ABI
 *
 * This header is intentionally C-compatible and POD-only.  The M12 PE DLLs,
 * winemetal.dll, winemetal.so, and the native macOS core may be built by
 * different compilers/runtimes, so this boundary must not expose C++ classes,
 * STL containers, Objective-C objects, exceptions, or ownership that depends on
 * a particular C++ ABI.  Future M12 refactors should add explicit handle-based
 * functions here rather than passing internal D3D12/DXMT/Metal objects across
 * the PE/native boundary.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define M12CORE_ABI_VERSION 1u

/* Feature flags describe which roadmap slices are implemented by the loaded
 * core.  Phase 1 is deliberately inert: it proves loader/fallback behavior
 * before any rendering ownership moves into libm12core.
 */
enum M12CoreFeatureFlags {
  M12CORE_FEATURE_INERT_LOADER = 1u << 0,
};

typedef struct M12CoreVersion {
  uint32_t abi_version;
  uint32_t feature_flags;
  uint32_t build_id_low;
  uint32_t build_id_high;
} M12CoreVersion;

/* Returns 0 on success. Non-zero values are reserved for future detailed
 * status codes once PE-side callers start depending on this ABI.
 */
int m12core_get_version(M12CoreVersion *out_version);

/* Human-readable build string for diagnostics only. The returned pointer is
 * process-lifetime static storage owned by libm12core.
 */
const char *m12core_build_string(void);

#ifdef __cplusplus
}
#endif
