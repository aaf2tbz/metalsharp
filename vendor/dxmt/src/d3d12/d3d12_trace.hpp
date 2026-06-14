#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>

static inline bool DXMTD3D12TraceEnabled() {
  static int enabled = []() {
    const char *value = std::getenv("DXMT_D3D12_TRACE");
    return value && value[0] && std::strcmp(value, "0") != 0;
  }();
  return enabled != 0;
}

static inline bool DXMTD3D12TraceComponentEnabled(const char *component) {
  const char *filter = std::getenv("DXMT_D3D12_TRACE_COMPONENTS");
  if (!filter || !filter[0])
    return true;
  if (!component || !component[0])
    return false;
  return std::strstr(filter, component) != nullptr;
}

static inline long DXMTD3D12TraceMaxBytes() {
  static long max_bytes = []() {
    const char *value = std::getenv("DXMT_D3D12_TRACE_MAX_MB");
    long mb = value && value[0] ? std::strtol(value, nullptr, 10) : 64;
    if (mb <= 0)
      mb = 64;
    return mb * 1024L * 1024L;
  }();
  return max_bytes;
}

static inline long DXMTD3D12TimingMinMs() {
  static long min_ms = []() {
    const char *value = std::getenv("DXMT_D3D12_TIMING_MIN_MS");
    long parsed = value && value[0] ? std::strtol(value, nullptr, 10) : 2;
    if (parsed < 0)
      parsed = 0;
    return parsed;
  }();
  return min_ms;
}

static inline void DXMTD3D12Trace(const char *component, const char *fmt, ...) {
  if (!DXMTD3D12TraceEnabled())
    return;
  if (!DXMTD3D12TraceComponentEnabled(component))
    return;

  FILE *f = fopen("Z:\\tmp\\dxmt_d3d12_trace.log", "a+");
  if (!f)
    return;

  fseek(f, 0, SEEK_END);
  if (ftell(f) >= DXMTD3D12TraceMaxBytes()) {
    fclose(f);
    return;
  }

  fprintf(f, "[pid=%lu tid=%lu] %s: ", GetCurrentProcessId(),
          GetCurrentThreadId(), component ? component : "d3d12");

  va_list args;
  va_start(args, fmt);
  vfprintf(f, fmt, args);
  va_end(args);

  fputc('\n', f);
  fclose(f);
}

class DXMTD3D12ScopedTimer {
public:
  DXMTD3D12ScopedTimer(const char *component, const char *label)
      : component_(component), label_(label),
        start_(std::chrono::steady_clock::now()) {}

  void SetDetail(const char *fmt, ...) {
    if (!fmt)
      return;
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(detail_, sizeof(detail_), fmt, args);
    va_end(args);
  }

  long long ElapsedMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start_)
        .count();
  }

  void TraceNow() const {
    if (!DXMTD3D12TraceEnabled())
      return;
    long long elapsed_ms = ElapsedMs();
    if (elapsed_ms < DXMTD3D12TimingMinMs())
      return;
    if (detail_[0]) {
      DXMTD3D12Trace(component_, "%s elapsed_ms=%lld %s", label_, elapsed_ms,
                     detail_);
    } else {
      DXMTD3D12Trace(component_, "%s elapsed_ms=%lld", label_, elapsed_ms);
    }
  }

  ~DXMTD3D12ScopedTimer() { TraceNow(); }

private:
  const char *component_;
  const char *label_;
  std::chrono::steady_clock::time_point start_;
  char detail_[256] = {};
};

static inline uint64_t DXMTD3D12Hash64(const void *data, size_t size) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  uint64_t hash = 1469598103934665603ull;
  for (size_t i = 0; i < size; i++) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  return hash;
}
