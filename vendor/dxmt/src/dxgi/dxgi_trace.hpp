#pragma once

#include "util_env.hpp"
#include <cstdarg>
#include <cstdio>
#include <windows.h>

namespace dxmt {

static inline bool DXMTDXGITraceEnabled() {
  static bool enabled = [] {
    auto value = env::getEnvVar("DXMT_DXGI_TRACE");
    return value == "1" || value == "true" || value == "TRUE";
  }();
  return enabled;
}

static inline void DXMTDXGITrace(const char *component, const char *fmt, ...) {
  if (!DXMTDXGITraceEnabled())
    return;

  FILE *f = fopen("Z:\\tmp\\dxmt_dxgi_trace.log", "a");
  if (!f)
    return;

  fprintf(f, "%s::", component ? component : "DXGI");
  va_list args;
  va_start(args, fmt);
  vfprintf(f, fmt, args);
  va_end(args);
  fputc('\n', f);
  fclose(f);
}

} // namespace dxmt
