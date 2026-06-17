#include "m12core.h"

#include <cstddef>

/*
 * Phase 1 native M12 core
 *
 * This file is intentionally tiny and inert.  It exists to prove that the
 * dxmt_m12 runtime can stage, load, version-check, and fall back from a unified
 * native core library before shader/PSO/binding ownership is migrated.  Keep
 * comments explicit: future refactor passes should be able to identify which
 * code moved into libm12core and which code remains a compatibility shim.
 */

namespace {

constexpr uint32_t kBuildIdLow = 0x4d313243u;  // "M12C" marker.
constexpr uint32_t kBuildIdHigh = 0x00000001u; // Phase-1 inert loader build.

} // namespace

extern "C" int m12core_get_version(M12CoreVersion *out_version) {
  if (!out_version)
    return 1;

  out_version->abi_version = M12CORE_ABI_VERSION;
  out_version->feature_flags = M12CORE_FEATURE_INERT_LOADER;
  out_version->build_id_low = kBuildIdLow;
  out_version->build_id_high = kBuildIdHigh;
  return 0;
}

extern "C" const char *m12core_build_string(void) {
  return "libm12core phase1 inert-loader abi=1";
}
