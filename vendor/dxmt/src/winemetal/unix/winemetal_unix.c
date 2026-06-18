#include <stdatomic.h>
#include <dlfcn.h>
#import <Cocoa/Cocoa.h>
#import <ColorSync/ColorSync.h>
#import <CoreFoundation/CFRunLoop.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <QuartzCore/QuartzCore.h>
#include "objc/objc-runtime.h"
#include <bootstrap.h>
#include <mach/mach_port.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WINEMETAL_API
#include "../winemetal_thunks.h"
#include "../airconv_thunks.h"
#include "../../m12core/m12core.h"

typedef int NTSTATUS;
#define STATUS_SUCCESS 0
#define STATUS_UNSUCCESSFUL 0xC0000001

static int
winemetal_debug_enabled(void) {
  const char *value = getenv("DXMT_WINEMETAL_DEBUG");
  return value && value[0] && strcmp(value, "0") != 0;
}

static FILE *
winemetal_open_log(const char *fallback_name) {
  const char *root = getenv("DXMT_LOG_PATH");
  const char *file = fallback_name && fallback_name[0] ? fallback_name : "winemetal-unix.log";
  char path[4096];

  if (!root || !root[0])
    return fopen(file, "a");

  snprintf(
      path, sizeof(path), "%s%s%s", root, (root[strlen(root) - 1] == '/' || root[strlen(root) - 1] == '\\') ? "" : "/",
      file
  );
  path[sizeof(path) - 1] = '\0';
  return fopen(path, "a");
}

static FILE *
winemetal_debug_log(void) {
  return winemetal_debug_enabled() ? winemetal_open_log("winemetal-unix-debug.log") : NULL;
}

static FILE *
winemetal_critical_log(void) {
  return winemetal_open_log("winemetal-unix-debug.log");
}

/*
 * Phase-1 libm12core loader
 *
 * The unified M12 core roadmap starts with an inert native dylib that is loaded
 * from the Unix side of winemetal.  Loading is env-gated so the current M12
 * renderer remains recoverable while we prove staging, version checks, and
 * fallback behavior.  Future phases should replace scattered local ownership
 * with handle-based calls through m12core.h, but this loader intentionally does
 * not move rendering work yet.
 */
typedef int (*PFN_m12core_get_version)(M12CoreVersion *out_version);
typedef const char *(*PFN_m12core_build_string)(void);
typedef int (*PFN_m12core_record_counter)(uint32_t counter_id, uint64_t delta);
typedef int (*PFN_m12core_get_counters)(M12CoreCounterSnapshot *out_snapshot);
typedef int (*PFN_m12core_hash_shader_bytecode)(
    const void *bytecode, uint64_t bytecode_size, uint32_t stage, M12CoreShaderBytecodeInfo *out_info
);
typedef int (*PFN_m12core_format_shader_cache_paths)(
    const char *cache_root, uint64_t shader_hash, M12CoreShaderCachePaths *out_paths
);
typedef int (*PFN_m12core_probe_shader_cache)(
    const char *cache_root, uint64_t shader_hash, uint32_t force_source_compile, M12CoreShaderCacheLookup *out_lookup
);
typedef int (*PFN_m12core_parse_shader_reflection)(
    const char *reflection_text, uint64_t reflection_text_size, M12CoreShaderReflectionSummary *out_summary
);
typedef int (*PFN_m12core_make_pipeline_cache_key)(
    const M12CorePipelineCacheKeyInput *input, M12CorePipelineCacheKey *out_key
);
typedef int (*PFN_m12core_create_shader_function)(
    const M12CoreShaderFunctionDesc *desc, M12CoreShaderFunctionResult *out_result
);
typedef int (*PFN_m12core_lower_dxil_to_msl)(
    const M12CoreDXILToMSLDesc *desc, char *out_source, uint64_t out_source_capacity, M12CoreDXILToMSLResult *out_result
);
typedef int (*PFN_m12core_reflect_sm50_shader)(
    const void *bytecode, uint64_t bytecode_size, uint32_t options, M12CoreSM50ShaderReflection *out_reflection,
    M12CoreSM50ShaderArgument *out_constant_buffers, uint32_t constant_buffer_capacity,
    M12CoreSM50ShaderArgument *out_arguments, uint32_t argument_capacity, M12CoreSM50ReflectionResult *out_result
);
typedef int (*PFN_m12core_lookup_pipeline_cache)(
    const M12CorePipelineCacheQuery *query, M12CorePipelineCacheResult *out_result
);
typedef int (*PFN_m12core_store_pipeline_cache)(const M12CorePipelineCacheQuery *query, uint64_t pipeline_handle);
typedef int (*PFN_m12core_make_pipeline_cache_key_from_fields)(
    const M12CorePipelineKeyFields *input, M12CorePipelineCacheKey *out_key
);
typedef int (*PFN_m12core_create_pipeline_state)(
    const M12CorePipelineCreateDesc *desc, M12CorePipelineCreateResult *out_result
);
typedef int (*PFN_m12core_summarize_root_signature)(
    const M12CoreRootSignatureDesc *desc, M12CoreRootSignatureSummary *out_summary
);
typedef int (*PFN_m12core_build_root_binding_plan)(
    const M12CoreRootBindingPlanDesc *desc, M12CoreRootBindingPlanSummary *out_summary
);
typedef int (*PFN_m12core_lookup_root_binding)(
    const M12CoreRootBindingLookupDesc *desc, M12CoreRootBindingLookupResult *out_result
);
typedef int (*PFN_m12core_summarize_prewarm_pack)(
    const M12CorePrewarmPackDesc *desc, M12CorePrewarmPackSummary *out_summary
);
typedef int (*PFN_m12core_build_draw_plan)(const M12CoreDrawPlanDesc *desc, M12CoreDrawPlanSummary *out_summary);
typedef int (*PFN_m12core_build_present_plan)(
    const M12CorePresentPlanDesc *desc, M12CorePresentPlanSummary *out_summary
);
typedef int (*PFN_m12core_build_replay_plan)(const M12CoreReplayPlanDesc *desc, M12CoreReplayPlanSummary *out_summary);
typedef int (*PFN_m12core_validate_command_stream)(
    const M12CoreCommandStreamDesc *desc, M12CoreCommandStreamSummary *out_summary
);
typedef int (*PFN_m12core_plan_render_pass)(
    const M12CoreRenderPassPlanDesc *desc, M12CoreRenderPassPlanSummary *out_summary
);
typedef int (*PFN_m12core_plan_present_execute)(
    const M12CorePresentExecuteDesc *desc, M12CorePresentExecuteSummary *out_summary
);
typedef int (*PFN_m12core_plan_replay_execute)(
    const M12CoreReplayExecuteDesc *desc, M12CoreReplayExecuteSummary *out_summary
);
typedef int (*PFN_m12core_validate_command_packet_stream)(
    const M12CoreCommandPacketStreamDesc *desc, M12CoreCommandPacketStreamSummary *out_summary
);
typedef int (*PFN_m12core_make_cache_compatibility_key)(
    const M12CoreCacheCompatibilityDesc *desc, M12CoreCacheCompatibilityKey *out_key
);
typedef int (*PFN_m12core_register_handle)(
    const M12CoreHandleRegistryDesc *desc, M12CoreHandleRegistryResult *out_result
);
typedef int (*PFN_m12core_validate_handle)(
    const M12CoreHandleValidationDesc *desc, M12CoreHandleValidationResult *out_result
);
typedef int (*PFN_m12core_classify_packet_support)(
    const M12CorePacketSupportDesc *desc, M12CorePacketSupportSummary *out_summary
);
typedef int (*PFN_m12core_execute_replay_packet_stream)(
    const M12CoreReplayPacketExecuteDesc *desc, M12CoreReplayPacketExecuteSummary *out_summary
);
typedef int (*PFN_m12core_plan_encoder_ownership)(
    const M12CoreEncoderOwnershipDesc *desc, M12CoreEncoderOwnershipSummary *out_summary
);
typedef int (*PFN_m12core_plan_native_present_ownership)(
    const M12CoreNativePresentOwnershipDesc *desc, M12CoreNativePresentOwnershipSummary *out_summary
);
typedef int (*PFN_m12core_plan_cache_warm_start)(
    const M12CoreCacheWarmStartDesc *desc, M12CoreCacheWarmStartSummary *out_summary
);

static void *m12core_handle;
static M12CoreVersion m12core_version;
static PFN_m12core_get_version p_m12core_get_version;
static PFN_m12core_build_string p_m12core_build_string;
static PFN_m12core_record_counter p_m12core_record_counter;
static PFN_m12core_get_counters p_m12core_get_counters;
static PFN_m12core_hash_shader_bytecode p_m12core_hash_shader_bytecode;
static PFN_m12core_format_shader_cache_paths p_m12core_format_shader_cache_paths;
static PFN_m12core_probe_shader_cache p_m12core_probe_shader_cache;
static PFN_m12core_parse_shader_reflection p_m12core_parse_shader_reflection;
static PFN_m12core_make_pipeline_cache_key p_m12core_make_pipeline_cache_key;
static PFN_m12core_create_shader_function p_m12core_create_shader_function;
static PFN_m12core_lower_dxil_to_msl p_m12core_lower_dxil_to_msl;
static PFN_m12core_reflect_sm50_shader p_m12core_reflect_sm50_shader;
static PFN_m12core_lookup_pipeline_cache p_m12core_lookup_pipeline_cache;
static PFN_m12core_store_pipeline_cache p_m12core_store_pipeline_cache;
static PFN_m12core_make_pipeline_cache_key_from_fields p_m12core_make_pipeline_cache_key_from_fields;
static PFN_m12core_create_pipeline_state p_m12core_create_pipeline_state;
static PFN_m12core_summarize_root_signature p_m12core_summarize_root_signature;
static PFN_m12core_build_root_binding_plan p_m12core_build_root_binding_plan;
static PFN_m12core_lookup_root_binding p_m12core_lookup_root_binding;
static PFN_m12core_summarize_prewarm_pack p_m12core_summarize_prewarm_pack;
static PFN_m12core_build_draw_plan p_m12core_build_draw_plan;
static PFN_m12core_build_present_plan p_m12core_build_present_plan;
static PFN_m12core_build_replay_plan p_m12core_build_replay_plan;
static PFN_m12core_validate_command_stream p_m12core_validate_command_stream;
static PFN_m12core_plan_render_pass p_m12core_plan_render_pass;
static PFN_m12core_plan_present_execute p_m12core_plan_present_execute;
static PFN_m12core_plan_replay_execute p_m12core_plan_replay_execute;
static PFN_m12core_validate_command_packet_stream p_m12core_validate_command_packet_stream;
static PFN_m12core_make_cache_compatibility_key p_m12core_make_cache_compatibility_key;
static PFN_m12core_register_handle p_m12core_register_handle;
static PFN_m12core_validate_handle p_m12core_validate_handle;
static PFN_m12core_classify_packet_support p_m12core_classify_packet_support;
static PFN_m12core_execute_replay_packet_stream p_m12core_execute_replay_packet_stream;
static PFN_m12core_plan_encoder_ownership p_m12core_plan_encoder_ownership;
static PFN_m12core_plan_native_present_ownership p_m12core_plan_native_present_ownership;
static PFN_m12core_plan_cache_warm_start p_m12core_plan_cache_warm_start;
static _Atomic uint64_t m12core_bridge_batches;
static _Atomic uint64_t m12core_bridge_delta_total;
static _Atomic uint64_t m12core_shader_function_calls;
static _Atomic uint64_t m12core_dxil_to_msl_calls;
static _Atomic uint64_t m12core_sm50_reflection_calls;
static _Atomic uint64_t m12core_pipeline_cache_calls;
static _Atomic uint64_t m12core_pipeline_create_calls;

static bool
m12core_env_enabled(const char *name) {
  const char *value = getenv(name);
  return value && value[0] && strcmp(value, "0") != 0;
}

static void
m12core_log_line(const char *message) {
  FILE *log = winemetal_critical_log();
  if (!log)
    return;
  fprintf(log, "[m12core] %s\n", message ? message : "(null)");
  fclose(log);
}

static void
m12core_log_version(const char *path, const char *build_string) {
  FILE *log = winemetal_critical_log();
  if (!log)
    return;
  fprintf(
      log, "[m12core] loaded path=%s abi=%u features=0x%x build_id=%08x:%08x build=%s\n", path ? path : "(null)",
      m12core_version.abi_version, m12core_version.feature_flags, m12core_version.build_id_high,
      m12core_version.build_id_low, build_string ? build_string : "(null)"
  );
  fclose(log);
}

__attribute__((constructor)) static void
m12core_try_load(void) {
  if (!m12core_env_enabled("DXMT_M12CORE_ENABLE"))
    return;

  const char *path = getenv("DXMT_M12CORE_PATH");
  if (!path || !path[0])
    path = "@loader_path/libm12core.dylib";

  m12core_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!m12core_handle) {
    const char *error = dlerror();
    FILE *log = winemetal_critical_log();
    if (log) {
      fprintf(log, "[m12core] dlopen failed path=%s error=%s\n", path, error ? error : "unknown");
      fclose(log);
    }
    if (m12core_env_enabled("DXMT_M12CORE_REQUIRED"))
      abort();
    return;
  }

  p_m12core_get_version = (PFN_m12core_get_version)dlsym(m12core_handle, "m12core_get_version");
  p_m12core_build_string = (PFN_m12core_build_string)dlsym(m12core_handle, "m12core_build_string");
  p_m12core_record_counter = (PFN_m12core_record_counter)dlsym(m12core_handle, "m12core_record_counter");
  p_m12core_get_counters = (PFN_m12core_get_counters)dlsym(m12core_handle, "m12core_get_counters");
  p_m12core_hash_shader_bytecode =
      (PFN_m12core_hash_shader_bytecode)dlsym(m12core_handle, "m12core_hash_shader_bytecode");
  p_m12core_format_shader_cache_paths =
      (PFN_m12core_format_shader_cache_paths)dlsym(m12core_handle, "m12core_format_shader_cache_paths");
  p_m12core_probe_shader_cache = (PFN_m12core_probe_shader_cache)dlsym(m12core_handle, "m12core_probe_shader_cache");
  p_m12core_parse_shader_reflection =
      (PFN_m12core_parse_shader_reflection)dlsym(m12core_handle, "m12core_parse_shader_reflection");
  p_m12core_make_pipeline_cache_key =
      (PFN_m12core_make_pipeline_cache_key)dlsym(m12core_handle, "m12core_make_pipeline_cache_key");
  p_m12core_create_shader_function =
      (PFN_m12core_create_shader_function)dlsym(m12core_handle, "m12core_create_shader_function");
  p_m12core_lower_dxil_to_msl = (PFN_m12core_lower_dxil_to_msl)dlsym(m12core_handle, "m12core_lower_dxil_to_msl");
  p_m12core_reflect_sm50_shader = (PFN_m12core_reflect_sm50_shader)dlsym(m12core_handle, "m12core_reflect_sm50_shader");
  p_m12core_lookup_pipeline_cache =
      (PFN_m12core_lookup_pipeline_cache)dlsym(m12core_handle, "m12core_lookup_pipeline_cache");
  p_m12core_store_pipeline_cache =
      (PFN_m12core_store_pipeline_cache)dlsym(m12core_handle, "m12core_store_pipeline_cache");
  p_m12core_make_pipeline_cache_key_from_fields = (PFN_m12core_make_pipeline_cache_key_from_fields)dlsym(
      m12core_handle, "m12core_make_pipeline_cache_key_from_fields"
  );
  p_m12core_create_pipeline_state =
      (PFN_m12core_create_pipeline_state)dlsym(m12core_handle, "m12core_create_pipeline_state");
  p_m12core_summarize_root_signature =
      (PFN_m12core_summarize_root_signature)dlsym(m12core_handle, "m12core_summarize_root_signature");
  p_m12core_build_root_binding_plan =
      (PFN_m12core_build_root_binding_plan)dlsym(m12core_handle, "m12core_build_root_binding_plan");
  p_m12core_lookup_root_binding = (PFN_m12core_lookup_root_binding)dlsym(m12core_handle, "m12core_lookup_root_binding");
  p_m12core_summarize_prewarm_pack =
      (PFN_m12core_summarize_prewarm_pack)dlsym(m12core_handle, "m12core_summarize_prewarm_pack");
  p_m12core_build_draw_plan = (PFN_m12core_build_draw_plan)dlsym(m12core_handle, "m12core_build_draw_plan");
  p_m12core_build_present_plan = (PFN_m12core_build_present_plan)dlsym(m12core_handle, "m12core_build_present_plan");
  p_m12core_build_replay_plan = (PFN_m12core_build_replay_plan)dlsym(m12core_handle, "m12core_build_replay_plan");
  p_m12core_validate_command_stream =
      (PFN_m12core_validate_command_stream)dlsym(m12core_handle, "m12core_validate_command_stream");
  p_m12core_plan_render_pass = (PFN_m12core_plan_render_pass)dlsym(m12core_handle, "m12core_plan_render_pass");
  p_m12core_plan_present_execute =
      (PFN_m12core_plan_present_execute)dlsym(m12core_handle, "m12core_plan_present_execute");
  p_m12core_plan_replay_execute = (PFN_m12core_plan_replay_execute)dlsym(m12core_handle, "m12core_plan_replay_execute");
  p_m12core_validate_command_packet_stream =
      (PFN_m12core_validate_command_packet_stream)dlsym(m12core_handle, "m12core_validate_command_packet_stream");
  p_m12core_make_cache_compatibility_key =
      (PFN_m12core_make_cache_compatibility_key)dlsym(m12core_handle, "m12core_make_cache_compatibility_key");
  p_m12core_register_handle = (PFN_m12core_register_handle)dlsym(m12core_handle, "m12core_register_handle");
  p_m12core_validate_handle = (PFN_m12core_validate_handle)dlsym(m12core_handle, "m12core_validate_handle");
  p_m12core_classify_packet_support =
      (PFN_m12core_classify_packet_support)dlsym(m12core_handle, "m12core_classify_packet_support");
  p_m12core_execute_replay_packet_stream =
      (PFN_m12core_execute_replay_packet_stream)dlsym(m12core_handle, "m12core_execute_replay_packet_stream");
  p_m12core_plan_encoder_ownership =
      (PFN_m12core_plan_encoder_ownership)dlsym(m12core_handle, "m12core_plan_encoder_ownership");
  p_m12core_plan_native_present_ownership =
      (PFN_m12core_plan_native_present_ownership)dlsym(m12core_handle, "m12core_plan_native_present_ownership");
  p_m12core_plan_cache_warm_start =
      (PFN_m12core_plan_cache_warm_start)dlsym(m12core_handle, "m12core_plan_cache_warm_start");
  if (!p_m12core_get_version || p_m12core_get_version(&m12core_version) != 0 ||
      m12core_version.abi_version != M12CORE_ABI_VERSION) {
    m12core_log_line("version check failed; unloading inert core");
    dlclose(m12core_handle);
    m12core_handle = NULL;
    if (m12core_env_enabled("DXMT_M12CORE_REQUIRED"))
      abort();
    return;
  }

  if (p_m12core_record_counter)
    p_m12core_record_counter(M12CORE_COUNTER_LOADER_LOAD_SUCCESS, 1);
  m12core_log_version(path, p_m12core_build_string ? p_m12core_build_string() : NULL);
}

__attribute__((destructor)) static void
m12core_dump_counters_at_exit(void) {
  if (!m12core_env_enabled("DXMT_M12CORE_DUMP_COUNTERS") || !p_m12core_get_counters)
    return;

  M12CoreCounterSnapshot snapshot;
  memset(&snapshot, 0, sizeof(snapshot));
  if (p_m12core_get_counters(&snapshot) != 0)
    return;

  FILE *log = winemetal_critical_log();
  if (!log)
    return;
  fprintf(
      log, "[m12core] counter_snapshot abi=%u count=%u bridge_batches=%llu bridge_deltas=%llu", snapshot.abi_version,
      snapshot.counter_count, (unsigned long long)atomic_load(&m12core_bridge_batches),
      (unsigned long long)atomic_load(&m12core_bridge_delta_total)
  );
  uint32_t count = snapshot.counter_count;
  if (count > M12CORE_COUNTER_COUNT)
    count = M12CORE_COUNTER_COUNT;
  for (uint32_t i = 0; i < count; i++) {
    if (snapshot.values[i])
      fprintf(log, " c%u=%llu", i, (unsigned long long)snapshot.values[i]);
  }
  fprintf(log, "\n");
  fclose(log);
}

static NTSTATUS
_WMTM12CoreRecordCounters(void *obj) {
  struct unixcall_m12core_record_counters *params = obj;
  if (!params || !p_m12core_record_counter)
    return STATUS_SUCCESS;

  /* This is the PE->native diagnostics bridge for Phase 2.  It is explicitly
   * best-effort: libm12core is optional, and counter sync failures must never
   * affect rendering.  The PE side batches deltas before crossing this unixcall
   * boundary so AC6's hot PSO paths do not pay a syscall-style cost per event.
   */
  uint32_t count = params->counter_count;
  if (count > M12CORE_COUNTER_COUNT)
    count = M12CORE_COUNTER_COUNT;

  uint64_t delta_total = 0;
  for (uint32_t i = 0; i < count; i++) {
    if (params->deltas[i]) {
      delta_total += params->deltas[i];
      p_m12core_record_counter(i, params->deltas[i]);
    }
  }

  uint64_t batch = atomic_fetch_add(&m12core_bridge_batches, 1) + 1;
  atomic_fetch_add(&m12core_bridge_delta_total, delta_total);
  if (batch == 1 && m12core_env_enabled("DXMT_M12CORE_DUMP_COUNTERS")) {
    FILE *log = winemetal_critical_log();
    if (log) {
      fprintf(
          log, "[m12core] counter_bridge first_batch counters=%u delta_total=%llu\n", count,
          (unsigned long long)delta_total
      );
      fclose(log);
    }
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreHashShaderBytecode(void *obj) {
  struct unixcall_m12core_hash_shader_bytecode *params = obj;
  if (!params || !p_m12core_hash_shader_bytecode)
    return STATUS_SUCCESS;

  /* Phase 3 shader-key bridge.  Only bytecode bytes and a stable stage enum
   * cross the ABI; Metal objects and reflection stay on the old path until the
   * later cache/compile ownership slice is validated.
   */
  params->ret_success =
      p_m12core_hash_shader_bytecode(params->bytecode.ptr, params->bytecode_size, params->stage, &params->ret_info) ==
      0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreFormatShaderCachePaths(void *obj) {
  struct unixcall_m12core_format_shader_cache_paths *params = obj;
  if (!params || !p_m12core_format_shader_cache_paths)
    return STATUS_SUCCESS;

  /* Phase 3.1 shader cache lookup policy bridge.  Only deterministic path
   * formatting lives here; PE-side file IO and Metal object creation remain
   * unchanged so this can be validated independently.
   */
  params->ret_success =
      p_m12core_format_shader_cache_paths(params->cache_root.ptr, params->shader_hash, &params->ret_paths) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreProbeShaderCache(void *obj) {
  struct unixcall_m12core_probe_shader_cache *params = obj;
  if (!params || !p_m12core_probe_shader_cache)
    return STATUS_SUCCESS;

  /* Phase 3.2 shader cache lookup bridge.  libm12core determines whether a
   * metallib cache entry should be used, while PE-side code still performs the
   * file read and Metal library creation for compatibility.
   */
  params->ret_success =
      p_m12core_probe_shader_cache(
          params->cache_root.ptr, params->shader_hash, params->force_source_compile, &params->ret_lookup
      ) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreParseShaderReflection(void *obj) {
  struct unixcall_m12core_parse_shader_reflection *params = obj;
  if (!params || !p_m12core_parse_shader_reflection)
    return STATUS_SUCCESS;

  /* Phase 3.3 reflection bridge.  Only the compact cache-side reflection JSON
   * summary crosses here; full SM50 reflection handles stay PE-owned.
   */
  params->ret_success = p_m12core_parse_shader_reflection(
                            params->reflection_text.ptr, params->reflection_text_size, &params->ret_summary
                        ) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreLowerDXILToMSL(void *obj) {
  struct unixcall_m12core_lower_dxil_to_msl *params = obj;
  if (!params || !p_m12core_lower_dxil_to_msl)
    return STATUS_SUCCESS;

  /* Phase 3 DXIL->MSL bridge.  libm12core owns DXIL container parsing, LLVM
   * bitcode parsing, typed MSL lowering, and fallback DXILToMSL conversion.
   * File diagnostics and Metal function creation remain separate seams.
   */
  M12CoreDXILToMSLDesc desc = {0};
  desc.abi_version = params->abi_version;
  desc.stage = params->stage;
  desc.dxil_container = params->dxil_container.ptr;
  desc.dxil_container_size = params->dxil_container_size;
  desc.vertex_inputs = params->vertex_inputs.ptr;
  desc.vertex_input_count = params->vertex_input_count;

  params->ret_success =
      p_m12core_lower_dxil_to_msl(&desc, params->out_source.ptr, params->out_source_capacity, &params->ret_result) == 0;
  uint64_t calls = atomic_fetch_add(&m12core_dxil_to_msl_calls, 1) + 1;
  if (calls == 1 && m12core_env_enabled("DXMT_M12CORE_DUMP_COUNTERS")) {
    FILE *log = winemetal_debug_log();
    if (log) {
      fprintf(
          log, "[m12core] dxil_to_msl first_call stage=%u bytes=%llu status=%u required=%llu success=%u\n",
          params->stage, (unsigned long long)params->dxil_container_size, params->ret_result.status,
          (unsigned long long)params->ret_result.required_source_size, params->ret_success
      );
      fclose(log);
    }
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreLookupPipelineCache(void *obj) {
  struct unixcall_m12core_lookup_pipeline_cache *params = obj;
  if (!params || !p_m12core_lookup_pipeline_cache)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_lookup_pipeline_cache(&params->query, &params->ret_result) == 0;
  uint64_t calls = atomic_fetch_add(&m12core_pipeline_cache_calls, 1) + 1;
  if (calls == 1 && m12core_env_enabled("DXMT_M12CORE_DUMP_COUNTERS")) {
    FILE *log = winemetal_debug_log();
    if (log) {
      fprintf(
          log, "[m12core] pipeline_cache first_lookup kind=%u key=0x%llx hit=%u success=%u\n", params->query.kind,
          (unsigned long long)params->query.key, params->ret_result.hit, params->ret_success
      );
      fclose(log);
    }
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreStorePipelineCache(void *obj) {
  struct unixcall_m12core_store_pipeline_cache *params = obj;
  if (!params || !p_m12core_store_pipeline_cache)
    return STATUS_SUCCESS;
  params->ret_success = p_m12core_store_pipeline_cache(&params->query, params->pipeline_handle) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreReflectSM50Shader(void *obj) {
  struct unixcall_m12core_reflect_sm50_shader *params = obj;
  if (!params || !p_m12core_reflect_sm50_shader)
    return STATUS_SUCCESS;

  /* Phase 3 reflection compatibility bridge.  libm12core owns SM50 reflection
   * and argument extraction; D3D12 maps the POD result back to its current
   * binding structs until the Phase 5 descriptor/root mapper migration.
   */
  params->ret_success = p_m12core_reflect_sm50_shader(
                            params->bytecode.ptr, params->bytecode_size, params->options, params->out_reflection.ptr,
                            params->out_constant_buffers.ptr, params->constant_buffer_capacity,
                            params->out_arguments.ptr, params->argument_capacity, &params->ret_result
                        ) == 0;
  uint64_t calls = atomic_fetch_add(&m12core_sm50_reflection_calls, 1) + 1;
  if (calls == 1 && m12core_env_enabled("DXMT_M12CORE_DUMP_COUNTERS")) {
    FILE *log = winemetal_debug_log();
    if (log) {
      fprintf(
          log, "[m12core] sm50_reflection first_call bytes=%llu status=%u cb=%u args=%u success=%u\n",
          (unsigned long long)params->bytecode_size, params->ret_result.status,
          params->ret_result.required_constant_buffers, params->ret_result.required_arguments, params->ret_success
      );
      fclose(log);
    }
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreCreateShaderFunction(void *obj) {
  struct unixcall_m12core_create_shader_function *params = obj;
  if (!params || !p_m12core_create_shader_function)
    return STATUS_SUCCESS;

  /* Phase 3 high-risk shader-function bridge.  libm12core now owns Metal
   * library creation, function fallback lookup, and native function caching for
   * DXIL cached metallibs/generated MSL.  The PE side still owns DXIL->MSL
   * lowering and D3D12 reflection compatibility until later slices.
   */
  M12CoreShaderFunctionDesc desc = {0};
  desc.abi_version = M12CORE_ABI_VERSION;
  desc.stage = params->stage;
  desc.input_kind = params->input_kind;
  desc.shader_hash = params->shader_hash;
  desc.device_handle = params->device;
  desc.input_data = params->input_data.ptr;
  desc.input_size = params->input_size;
  snprintf(desc.entry_point, sizeof(desc.entry_point), "%s", params->entry_point);

  params->ret_success = p_m12core_create_shader_function(&desc, &params->ret_result) == 0;
  uint64_t calls = atomic_fetch_add(&m12core_shader_function_calls, 1) + 1;
  if (calls == 1 && m12core_env_enabled("DXMT_M12CORE_DUMP_COUNTERS")) {
    FILE *log = winemetal_debug_log();
    if (log) {
      fprintf(
          log, "[m12core] shader_function first_call stage=%u kind=%u hash=0x%llx status=%u cache_hit=%u success=%u\n",
          params->stage, params->input_kind, (unsigned long long)params->shader_hash, params->ret_result.status,
          params->ret_result.cache_hit, params->ret_success
      );
      fclose(log);
    }
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreMakePipelineCacheKey(void *obj) {
  struct unixcall_m12core_make_pipeline_cache_key *params = obj;
  if (!params || !p_m12core_make_pipeline_cache_key)
    return STATUS_SUCCESS;

  /* Phase 4 compatibility bridge retained for earlier cache-key callers. */
  params->ret_success = p_m12core_make_pipeline_cache_key(&params->input, &params->ret_key) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreMakePipelineCacheKeyFromFields(void *obj) {
  struct unixcall_m12core_make_pipeline_cache_key_from_fields *params = obj;
  if (!params || !p_m12core_make_pipeline_cache_key_from_fields)
    return STATUS_SUCCESS;

  /* Phase 4 normalized-key bridge.  The PE side passes a stable scalar field
   * stream; libm12core performs the ordered accumulation and final namespace
   * combine so future PSO descriptors can migrate without changing key policy.
   */
  M12CorePipelineKeyFields input = {0};
  input.abi_version = params->abi_version;
  input.kind = params->kind;
  input.base_hash = params->base_hash;
  input.device_id = params->device_id;
  input.flags = params->flags;
  input.fields = params->fields.ptr;
  input.field_count = params->field_count;
  params->ret_success = p_m12core_make_pipeline_cache_key_from_fields(&input, &params->ret_key) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreCreatePipelineState(void *obj) {
  struct unixcall_m12core_create_pipeline_state *params = obj;
  if (!params || !p_m12core_create_pipeline_state)
    return STATUS_SUCCESS;

  /* Phase 4 native PSO creation bridge.  This moves actual Metal pipeline
   * creation/cache insertion behind libm12core while D3D12 keeps a direct WMT
   * fallback when the feature is disabled or returns no pipeline.
   */
  M12CorePipelineCreateDesc desc = {0};
  desc.abi_version = M12CORE_ABI_VERSION;
  desc.kind = params->kind;
  desc.device_handle = params->device;
  desc.cache_key = params->cache_key;
  desc.pipeline_info = params->pipeline_info.ptr;
  desc.pipeline_info_size = params->pipeline_info_size;
  params->ret_success = p_m12core_create_pipeline_state(&desc, &params->ret_result) == 0;
  uint64_t calls = atomic_fetch_add(&m12core_pipeline_create_calls, 1) + 1;
  if (calls == 1 && m12core_env_enabled("DXMT_M12CORE_DUMP_COUNTERS")) {
    FILE *log = winemetal_debug_log();
    if (log) {
      fprintf(
          log, "[m12core] pipeline_create first_call kind=%u key=0x%llx status=%u cache_hit=%u pso=%p success=%u\n",
          params->kind, (unsigned long long)params->cache_key, params->ret_result.status, params->ret_result.cache_hit,
          (void *)(uintptr_t)params->ret_result.pipeline_handle, params->ret_success
      );
      fclose(log);
    }
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreSummarizeRootSignature(void *obj) {
  struct unixcall_m12core_summarize_root_signature *params = obj;
  if (!params || !p_m12core_summarize_root_signature)
    return STATUS_SUCCESS;

  /* Phase 5 root-signature summary bridge.  Reconstruct the public C ABI desc
   * from fixed-width thunk fields; do not pass pointer-bearing structs from PE
   * memory directly across the unixcall boundary.
   */
  M12CoreRootSignatureDesc desc = {0};
  desc.abi_version = params->abi_version;
  desc.parameter_count = params->parameter_count;
  desc.static_sampler_count = params->static_sampler_count;
  desc.flags = params->flags;
  desc.blob_hash = params->blob_hash;
  desc.fields = params->fields.ptr;
  desc.field_count = params->field_count;
  params->ret_success = p_m12core_summarize_root_signature(&desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreBuildRootBindingPlan(void *obj) {
  struct unixcall_m12core_build_root_binding_plan *params = obj;
  if (!params || !p_m12core_build_root_binding_plan)
    return STATUS_SUCCESS;

  /* Phase 5 binding-plan bridge.  Rehydrate the public POD array pointers on
   * the unix side, but leave live D3D12 binding on the existing PE fallback.
   */
  M12CoreRootBindingPlanDesc desc = {0};
  desc.abi_version = params->abi_version;
  desc.flags = params->flags;
  desc.root_signature_key = params->root_signature_key;
  desc.parameters = params->parameters.ptr;
  desc.parameter_count = params->parameter_count;
  desc.ranges = params->ranges.ptr;
  desc.range_count = params->range_count;
  desc.static_samplers = params->static_samplers.ptr;
  desc.static_sampler_count = params->static_sampler_count;
  params->ret_success = p_m12core_build_root_binding_plan(&desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreLookupRootBinding(void *obj) {
  struct unixcall_m12core_lookup_root_binding *params = obj;
  if (!params || !p_m12core_lookup_root_binding)
    return STATUS_SUCCESS;

  M12CoreRootBindingLookupDesc desc = {0};
  desc.abi_version = params->abi_version;
  desc.lookup_kind = params->lookup_kind;
  desc.range_type = params->range_type;
  desc.shader_register = params->shader_register;
  desc.register_space = params->register_space;
  desc.shader_visibility = params->shader_visibility;
  desc.parameters = params->parameters.ptr;
  desc.parameter_count = params->parameter_count;
  desc.ranges = params->ranges.ptr;
  desc.range_count = params->range_count;
  desc.static_samplers = params->static_samplers.ptr;
  desc.static_sampler_count = params->static_sampler_count;
  params->ret_success = p_m12core_lookup_root_binding(&desc, &params->ret_result) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreSummarizePrewarmPack(void *obj) {
  struct unixcall_m12core_summarize_prewarm_pack *params = obj;
  if (!params || !p_m12core_summarize_prewarm_pack)
    return STATUS_SUCCESS;

  M12CorePrewarmPackDesc desc = {0};
  desc.abi_version = params->abi_version;
  desc.flags = params->flags;
  desc.appid = params->appid;
  desc.profile_key = params->profile_key;
  desc.source_pack_key = params->source_pack_key;
  desc.pipelines = params->pipelines.ptr;
  desc.pipeline_count = params->pipeline_count;
  desc.stages = params->stages.ptr;
  desc.stage_count = params->stage_count;
  params->ret_success = p_m12core_summarize_prewarm_pack(&desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreBuildDrawPlan(void *obj) {
  struct unixcall_m12core_build_draw_plan *params = obj;
  if (!params || !p_m12core_build_draw_plan)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_build_draw_plan(&params->desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreBuildPresentPlan(void *obj) {
  struct unixcall_m12core_build_present_plan *params = obj;
  if (!params || !p_m12core_build_present_plan)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_build_present_plan(&params->desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreBuildReplayPlan(void *obj) {
  struct unixcall_m12core_build_replay_plan *params = obj;
  if (!params || !p_m12core_build_replay_plan)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_build_replay_plan(&params->desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CorePlanReplayExecute(void *obj) {
  struct unixcall_m12core_plan_replay_execute *params = obj;
  if (!params || !p_m12core_plan_replay_execute)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_plan_replay_execute(&params->desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreValidateCommandStream(void *obj) {
  struct unixcall_m12core_validate_command_stream *params = obj;
  if (!params || !p_m12core_validate_command_stream)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_validate_command_stream(&params->desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreValidateCommandPacketStream(void *obj) {
  struct unixcall_m12core_validate_command_packet_stream *params = obj;
  if (!params || !p_m12core_validate_command_packet_stream)
    return STATUS_SUCCESS;

  M12CoreCommandPacketStreamDesc desc;
  memset(&desc, 0, sizeof(desc));
  desc.abi_version = params->abi_version;
  desc.packet_count = params->packet_count;
  desc.queue_type = params->queue_type;
  desc.command_list_index = params->command_list_index;
  desc.command_list_id = params->command_list_id;
  desc.queue_serial = params->queue_serial;
  desc.packets = (const M12CoreCommandPacket *)params->packets.ptr;
  params->ret_success = p_m12core_validate_command_packet_stream(&desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreMakeCacheCompatibilityKey(void *obj) {
  struct unixcall_m12core_make_cache_compatibility_key *params = obj;
  if (!params || !p_m12core_make_cache_compatibility_key)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_make_cache_compatibility_key(&params->desc, &params->ret_key) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreRegisterHandle(void *obj) {
  struct unixcall_m12core_register_handle *params = obj;
  if (!params || !p_m12core_register_handle)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_register_handle(&params->desc, &params->ret_result) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreValidateHandle(void *obj) {
  struct unixcall_m12core_validate_handle *params = obj;
  if (!params || !p_m12core_validate_handle)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_validate_handle(&params->desc, &params->ret_result) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreClassifyPacketSupport(void *obj) {
  struct unixcall_m12core_classify_packet_support *params = obj;
  if (!params || !p_m12core_classify_packet_support)
    return STATUS_SUCCESS;

  M12CorePacketSupportDesc desc;
  memset(&desc, 0, sizeof(desc));
  desc.abi_version = params->abi_version;
  desc.packet_count = params->packet_count;
  desc.queue_type = params->queue_type;
  desc.flags = params->flags;
  desc.stream_key = params->stream_key;
  desc.packet_sequence_xor = params->packet_sequence_xor;
  desc.packets = (const M12CoreCommandPacket *)params->packets.ptr;
  params->ret_success = p_m12core_classify_packet_support(&desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreExecuteReplayPacketStream(void *obj) {
  struct unixcall_m12core_execute_replay_packet_stream *params = obj;
  if (!params || !p_m12core_execute_replay_packet_stream)
    return STATUS_SUCCESS;

  M12CoreReplayPacketExecuteDesc desc;
  memset(&desc, 0, sizeof(desc));
  desc.abi_version = params->abi_version;
  desc.flags = params->flags;
  desc.packet_count = params->packet_count;
  desc.queue_type = params->queue_type;
  desc.command_list_index = params->command_list_index;
  desc.command_list_id = params->command_list_id;
  desc.queue_serial = params->queue_serial;
  desc.stream_key = params->stream_key;
  desc.packet_sequence_xor = params->packet_sequence_xor;
  desc.packets = (const M12CoreCommandPacket *)params->packets.ptr;
  params->ret_success = p_m12core_execute_replay_packet_stream(&desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CorePlanEncoderOwnership(void *obj) {
  struct unixcall_m12core_plan_encoder_ownership *params = obj;
  if (!params || !p_m12core_plan_encoder_ownership)
    return STATUS_SUCCESS;

  M12CoreEncoderOwnershipDesc desc;
  memset(&desc, 0, sizeof(desc));
  desc.abi_version = params->abi_version;
  desc.flags = params->flags;
  desc.packet_count = params->packet_count;
  desc.queue_type = params->queue_type;
  desc.command_list_index = params->command_list_index;
  desc.render_target_count = params->render_target_count;
  desc.descriptor_heap_count = params->descriptor_heap_count;
  desc.command_list_id = params->command_list_id;
  desc.queue_serial = params->queue_serial;
  desc.stream_key = params->stream_key;
  desc.packet_sequence_xor = params->packet_sequence_xor;
  desc.replay_execute_key = params->replay_execute_key;
  desc.resource_layout_key = params->resource_layout_key;
  desc.packets = (const M12CoreCommandPacket *)params->packets.ptr;
  params->ret_success = p_m12core_plan_encoder_ownership(&desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CorePlanNativePresentOwnership(void *obj) {
  struct unixcall_m12core_plan_native_present_ownership *params = obj;
  if (!params || !p_m12core_plan_native_present_ownership)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_plan_native_present_ownership(&params->desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CorePlanCacheWarmStart(void *obj) {
  struct unixcall_m12core_plan_cache_warm_start *params = obj;
  if (!params || !p_m12core_plan_cache_warm_start)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_plan_cache_warm_start(&params->desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CorePlanRenderPass(void *obj) {
  struct unixcall_m12core_plan_render_pass *params = obj;
  if (!params || !p_m12core_plan_render_pass)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_plan_render_pass(&params->desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CorePlanPresentExecute(void *obj) {
  struct unixcall_m12core_plan_present_execute *params = obj;
  if (!params || !p_m12core_plan_present_execute)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_plan_present_execute(&params->desc, &params->ret_summary) == 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTM12CoreExecutePresentBlit(void *obj) {
  struct unixcall_m12core_execute_present_blit *params = obj;
  if (!params || !p_m12core_plan_present_execute)
    return STATUS_SUCCESS;

  params->ret_success = p_m12core_plan_present_execute(&params->desc, &params->ret_summary) == 0;
  if (!params->ret_success)
    return STATUS_SUCCESS;
  if (!(params->ret_summary.flags & M12CORE_PRESENT_EXECUTE_SUMMARY_SUPPORTED))
    return STATUS_SUCCESS;
  if (!params->command_buffer || !params->source_texture || !params->destination_texture || !params->drawable) {
    params->ret_summary.flags &= ~M12CORE_PRESENT_EXECUTE_SUMMARY_SUPPORTED;
    params->ret_summary.fallback_reason = M12CORE_PRESENT_EXECUTE_FALLBACK_MISSING_DRAWABLE;
    params->ret_summary.validation_flags |= (1u << 1);
    return STATUS_SUCCESS;
  }

  id<MTLCommandBuffer> cmdbuf = (id<MTLCommandBuffer>)params->command_buffer;
  id<MTLTexture> source = (id<MTLTexture>)params->source_texture;
  id<MTLTexture> destination = (id<MTLTexture>)params->destination_texture;
  id<CAMetalDrawable> drawable = (id<CAMetalDrawable>)params->drawable;
  id<MTLBlitCommandEncoder> blit = [cmdbuf blitCommandEncoder];
  if (!blit) {
    params->ret_summary.flags &= ~M12CORE_PRESENT_EXECUTE_SUMMARY_SUPPORTED;
    params->ret_summary.fallback_reason = M12CORE_PRESENT_EXECUTE_FALLBACK_NON_RAW_PATH;
    params->ret_summary.validation_flags |= (1u << 2);
    return STATUS_SUCCESS;
  }

  [blit copyFromTexture:source
            sourceSlice:0
            sourceLevel:0
           sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake(params->desc.width, params->desc.height, 1)
              toTexture:destination
       destinationSlice:0
       destinationLevel:0
      destinationOrigin:MTLOriginMake(0, 0, 0)];
  [blit endEncoding];
  [cmdbuf presentDrawable:drawable];
  params->ret_summary.flags |= M12CORE_PRESENT_EXECUTE_SUMMARY_EXECUTED;
  return STATUS_SUCCESS;
}

static bool
winemetal_end_encoder_safely(id<MTLCommandEncoder> encoder, const char *label) {
  if (!encoder)
    return true;

  @try {
    [encoder endEncoding];
    return true;
  } @catch(NSException * exception) {
    FILE *el = winemetal_critical_log();
    if (el) {
      const char *name = [[exception name] UTF8String];
      const char *reason = [[exception reason] UTF8String];
      fprintf(
          el, "encoder_end_exception label=%s encoder=%p exception=%s reason=%s\n", label ? label : "(null)", encoder,
          name ? name : "(null)", reason ? reason : "(null)"
      );
      fclose(el);
    }
    return false;
  }
}

static const char *
winemetal_nsstring_utf8(NSString *value) {
  return value ? [value UTF8String] : "<nil>";
}

static NSArray *
winemetal_array_from_handles(const obj_handle_t *handles, uint32_t count, const char *label) {
  if (!handles || !count)
    return nil;

  NSMutableArray *array = [NSMutableArray arrayWithCapacity:count];
  uint32_t skipped = 0;
  for (uint32_t i = 0; i < count; i++) {
    id object = (id)handles[i];
    if (object) {
      [array addObject:object];
    } else {
      skipped++;
      FILE *dl = winemetal_critical_log();
      if (dl) {
        fprintf(
            dl, "[winemetal] filtered nil NSArray object label=%s index=%u count=%u\n", label ? label : "<unknown>", i,
            count
        );
        fclose(dl);
      }
    }
  }

  if (!array.count) {
    if (skipped) {
      FILE *dl = winemetal_critical_log();
      if (dl) {
        fprintf(
            dl, "[winemetal] NSArray construction skipped all nil objects label=%s count=%u\n",
            label ? label : "<unknown>", count
        );
        fclose(dl);
      }
    }
    return nil;
  }

  return array;
}

static const char *
winemetal_render_command_name(uint16_t type) {
  switch ((enum WMTRenderCommandType)type) {
  case WMTRenderCommandNop:
    return "Nop";
  case WMTRenderCommandUseResource:
    return "UseResource";
  case WMTRenderCommandSetVertexBuffer:
    return "SetVertexBuffer";
  case WMTRenderCommandSetVertexBufferOffset:
    return "SetVertexBufferOffset";
  case WMTRenderCommandSetFragmentBuffer:
    return "SetFragmentBuffer";
  case WMTRenderCommandSetFragmentBufferOffset:
    return "SetFragmentBufferOffset";
  case WMTRenderCommandSetMeshBuffer:
    return "SetMeshBuffer";
  case WMTRenderCommandSetMeshBufferOffset:
    return "SetMeshBufferOffset";
  case WMTRenderCommandSetObjectBuffer:
    return "SetObjectBuffer";
  case WMTRenderCommandSetObjectBufferOffset:
    return "SetObjectBufferOffset";
  case WMTRenderCommandSetFragmentTexture:
    return "SetFragmentTexture";
  case WMTRenderCommandSetFragmentSamplerState:
    return "SetFragmentSamplerState";
  case WMTRenderCommandSetFragmentBytes:
    return "SetFragmentBytes";
  case WMTRenderCommandSetRasterizerState:
    return "SetRasterizerState";
  case WMTRenderCommandSetViewports:
    return "SetViewports";
  case WMTRenderCommandSetScissorRects:
    return "SetScissorRects";
  case WMTRenderCommandSetPSO:
    return "SetPSO";
  case WMTRenderCommandSetDSSO:
    return "SetDSSO";
  case WMTRenderCommandSetBlendFactorAndStencilRef:
    return "SetBlendFactorAndStencilRef";
  case WMTRenderCommandSetVisibilityMode:
    return "SetVisibilityMode";
  case WMTRenderCommandDraw:
    return "Draw";
  case WMTRenderCommandDrawIndexed:
    return "DrawIndexed";
  case WMTRenderCommandDrawIndirect:
    return "DrawIndirect";
  case WMTRenderCommandDrawIndexedIndirect:
    return "DrawIndexedIndirect";
  case WMTRenderCommandDrawMeshThreadgroups:
    return "DrawMeshThreadgroups";
  case WMTRenderCommandDrawMeshThreadgroupsIndirect:
    return "DrawMeshThreadgroupsIndirect";
  case WMTRenderCommandMemoryBarrier:
    return "MemoryBarrier";
  case WMTRenderCommandDXMTGeometryDraw:
    return "DXMTGeometryDraw";
  case WMTRenderCommandDXMTGeometryDrawIndexed:
    return "DXMTGeometryDrawIndexed";
  case WMTRenderCommandDXMTGeometryDrawIndirect:
    return "DXMTGeometryDrawIndirect";
  case WMTRenderCommandDXMTGeometryDrawIndexedIndirect:
    return "DXMTGeometryDrawIndexedIndirect";
  case WMTRenderCommandWaitForFence:
    return "WaitForFence";
  case WMTRenderCommandUpdateFence:
    return "UpdateFence";
  case WMTRenderCommandSetViewport:
    return "SetViewport";
  case WMTRenderCommandSetScissorRect:
    return "SetScissorRect";
  case WMTRenderCommandDXMTTessellationMeshDraw:
    return "DXMTTessellationMeshDraw";
  case WMTRenderCommandDXMTTessellationMeshDrawIndexed:
    return "DXMTTessellationMeshDrawIndexed";
  case WMTRenderCommandDXMTTessellationMeshDrawIndirect:
    return "DXMTTessellationMeshDrawIndirect";
  case WMTRenderCommandDXMTTessellationMeshDrawIndexedIndirect:
    return "DXMTTessellationMeshDrawIndexedIndirect";
  case WMTRenderCommandDispatchThreadsPerTile:
    return "DispatchThreadsPerTile";
  default:
    return "Unknown";
  }
}

static void
winemetal_log_render_command(
    FILE *dl, size_t index, id<MTLRenderCommandEncoder> encoder, const struct wmtcmd_base *cmd
) {
  if (!dl || !cmd) {
    return;
  }

  fprintf(
      dl, "render_cmd[%zu] encoder=%p cmd=%p type=%u(%s) next=%p", index, encoder, cmd, cmd->type,
      winemetal_render_command_name(cmd->type), cmd->next.ptr
  );

  switch ((enum WMTRenderCommandType)cmd->type) {
  case WMTRenderCommandSetPSO: {
    const struct wmtcmd_render_setpso *body = (const struct wmtcmd_render_setpso *)cmd;
    fprintf(dl, " pso=%p", (void *)body->pso);
    break;
  }
  case WMTRenderCommandSetDSSO: {
    const struct wmtcmd_render_setdsso *body = (const struct wmtcmd_render_setdsso *)cmd;
    fprintf(dl, " dsso=%p stencil_ref=%u", (void *)body->dsso, body->stencil_ref);
    break;
  }
  case WMTRenderCommandSetVertexBuffer:
  case WMTRenderCommandSetFragmentBuffer:
  case WMTRenderCommandSetMeshBuffer:
  case WMTRenderCommandSetObjectBuffer: {
    const struct wmtcmd_render_setbuffer *body = (const struct wmtcmd_render_setbuffer *)cmd;
    fprintf(dl, " buffer=%p offset=%llu index=%u", (void *)body->buffer, (unsigned long long)body->offset, body->index);
    break;
  }
  case WMTRenderCommandSetVertexBufferOffset:
  case WMTRenderCommandSetFragmentBufferOffset:
  case WMTRenderCommandSetMeshBufferOffset:
  case WMTRenderCommandSetObjectBufferOffset: {
    const struct wmtcmd_render_setbufferoffset *body = (const struct wmtcmd_render_setbufferoffset *)cmd;
    fprintf(dl, " offset=%llu index=%u", (unsigned long long)body->offset, body->index);
    break;
  }
  case WMTRenderCommandSetFragmentTexture: {
    const struct wmtcmd_render_settexture *body = (const struct wmtcmd_render_settexture *)cmd;
    fprintf(dl, " texture=%p index=%u", (void *)body->texture, body->index);
    break;
  }
  case WMTRenderCommandDraw: {
    const struct wmtcmd_render_draw *body = (const struct wmtcmd_render_draw *)cmd;
    fprintf(
        dl, " primitive=%u vertex_start=%llu vertex_count=%llu instances=%u base_instance=%u", body->primitive_type,
        (unsigned long long)body->vertex_start, (unsigned long long)body->vertex_count, body->instance_count,
        body->base_instance
    );
    break;
  }
  case WMTRenderCommandDrawIndexed: {
    const struct wmtcmd_render_draw_indexed *body = (const struct wmtcmd_render_draw_indexed *)cmd;
    fprintf(
        dl,
        " primitive=%u index_type=%u index_count=%llu index_buffer=%p index_offset=%llu instances=%u base_vertex=%d base_instance=%u",
        body->primitive_type, body->index_type, (unsigned long long)body->index_count, (void *)body->index_buffer,
        (unsigned long long)body->index_buffer_offset, body->instance_count, body->base_vertex, body->base_instance
    );
    break;
  }
  default:
    break;
  }

  fprintf(dl, "\n");
}

void
execute_on_main(dispatch_block_t block) {
  if ([NSThread isMainThread]) {
    block();
  } else {
    dispatch_sync(dispatch_get_main_queue(), block);
  }
}

static NTSTATUS
_NSObject_retain(NSObject **obj) {
  [*obj retain];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSObject_release(NSObject **obj) {
  [*obj release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSArray_object(void *obj) {
  struct unixcall_generic_obj_uint64_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(NSArray *)params->handle objectAtIndex:params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSArray_count(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(NSArray *)params->handle count];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCopyAllDevices(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  params->ret = (obj_handle_t)MTLCopyAllDevices();
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_recommendedMaxWorkingSetSize(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle recommendedMaxWorkingSetSize];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_currentAllocatedSize(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle currentAllocatedSize];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_name(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLDevice>)params->handle name];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSString_getCString(void *obj) {
  struct unixcall_nsstring_getcstring *params = obj;
  params->ret = (uint32_t)[(NSString *)params->str getCString:(char *)params->buffer_ptr
                                                    maxLength:params->max_length
                                                     encoding:params->encoding];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newCommandQueue(void *obj) {
  struct unixcall_generic_obj_uint64_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLDevice>)params->handle newCommandQueueWithMaxCommandBufferCount:params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSAutoreleasePool_alloc_init(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  params->ret = (obj_handle_t)[[NSAutoreleasePool alloc] init];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandQueue_commandBuffer(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLCommandQueue>)params->handle commandBuffer];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_commit(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;
  [(id<MTLCommandBuffer>)params->handle commit];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_waitUntilCompleted(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;
  [(id<MTLCommandBuffer>)params->handle waitUntilCompleted];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_status(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLCommandBuffer>)params->handle status];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newSharedEvent(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLDevice>)params->handle newSharedEvent];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLSharedEvent_signaledValue(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLSharedEvent>)params->handle signaledValue];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_encodeSignalEvent(void *obj) {
  struct unixcall_generic_obj_obj_uint64_noret *params = obj;
  [(id<MTLCommandBuffer>)params->handle encodeSignalEvent:(id<MTLSharedEvent>)params->arg0 value:params->arg1];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newBuffer(void *obj) {
  struct unixcall_mtldevice_newbuffer *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  struct WMTBufferInfo *info = params->info.ptr;
  id<MTLBuffer> buffer;
  if (info->memory.ptr) {
    buffer = [device newBufferWithBytesNoCopy:info->memory.ptr
                                       length:info->length
                                      options:(enum MTLResourceOptions)info->options
                                  deallocator:NULL];
  } else {
    buffer = [device newBufferWithLength:info->length options:(enum MTLResourceOptions)info->options];
    info->memory.ptr = [buffer storageMode] == MTLStorageModePrivate ? NULL : [buffer contents];
  }
  params->ret = (obj_handle_t)buffer;
  info->gpu_address = [buffer gpuAddress];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newSamplerState(void *obj) {
  struct unixcall_mtldevice_newsamplerstate *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  struct WMTSamplerInfo *info = params->info.ptr;

  MTLSamplerDescriptor *sampler_desc = [[MTLSamplerDescriptor alloc] init];
  sampler_desc.borderColor = (MTLSamplerBorderColor)info->border_color;
  sampler_desc.rAddressMode = (MTLSamplerAddressMode)info->r_address_mode;
  sampler_desc.sAddressMode = (MTLSamplerAddressMode)info->s_address_mode;
  sampler_desc.tAddressMode = (MTLSamplerAddressMode)info->t_address_mode;
  sampler_desc.magFilter = (MTLSamplerMinMagFilter)info->mag_filter;
  sampler_desc.minFilter = (MTLSamplerMinMagFilter)info->min_filter;
  sampler_desc.mipFilter = (MTLSamplerMipFilter)info->mip_filter;
  sampler_desc.compareFunction = (MTLCompareFunction)info->compare_function;
  sampler_desc.lodMaxClamp = info->lod_max_clamp;
  sampler_desc.lodMinClamp = info->lod_min_clamp;
  sampler_desc.maxAnisotropy = info->max_anisotroy;
  sampler_desc.lodAverage = info->lod_average;
  sampler_desc.normalizedCoordinates = info->normalized_coords;
  sampler_desc.supportArgumentBuffers = info->support_argument_buffers;

  id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:sampler_desc];
  info->gpu_resource_id = info->support_argument_buffers ? [sampler gpuResourceID]._impl : 0;
  params->ret = (obj_handle_t)sampler;
  [sampler_desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newDepthStencilState(void *obj) {
  struct unixcall_mtldevice_newdepthstencilstate *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  const struct WMTDepthStencilInfo *info = params->info.ptr;

  MTLDepthStencilDescriptor *desc = [[MTLDepthStencilDescriptor alloc] init];
  desc.depthCompareFunction = (MTLCompareFunction)info->depth_compare_function;
  desc.depthWriteEnabled = info->depth_write_enabled;

  if (info->front_stencil.enabled) {
    desc.frontFaceStencil.depthStencilPassOperation = (MTLStencilOperation)info->front_stencil.depth_stencil_pass_op;
    desc.frontFaceStencil.depthFailureOperation = (MTLStencilOperation)info->front_stencil.depth_fail_op;
    desc.frontFaceStencil.stencilFailureOperation = (MTLStencilOperation)info->front_stencil.stencil_fail_op;
    desc.frontFaceStencil.stencilCompareFunction = (MTLCompareFunction)info->front_stencil.stencil_compare_function;
    desc.frontFaceStencil.writeMask = info->front_stencil.write_mask;
    desc.frontFaceStencil.readMask = info->front_stencil.read_mask;
  }

  if (info->back_stencil.enabled) {
    desc.backFaceStencil.depthStencilPassOperation = (MTLStencilOperation)info->back_stencil.depth_stencil_pass_op;
    desc.backFaceStencil.depthFailureOperation = (MTLStencilOperation)info->back_stencil.depth_fail_op;
    desc.backFaceStencil.stencilFailureOperation = (MTLStencilOperation)info->back_stencil.stencil_fail_op;
    desc.backFaceStencil.stencilCompareFunction = (MTLCompareFunction)info->back_stencil.stencil_compare_function;
    desc.backFaceStencil.writeMask = info->back_stencil.write_mask;
    desc.backFaceStencil.readMask = info->back_stencil.read_mask;
  }

  params->ret = (obj_handle_t)[device newDepthStencilStateWithDescriptor:desc];
  [desc release];
  return STATUS_SUCCESS;
}

MTLPixelFormat
to_metal_pixel_format(enum WMTPixelFormat format) {
  return (MTLPixelFormat)ORIGINAL_FORMAT(format);
}

void
fill_texture_descriptor(MTLTextureDescriptor *desc, struct WMTTextureInfo *info) {
  desc.textureType = (MTLTextureType)info->type;
  desc.pixelFormat = to_metal_pixel_format(info->pixel_format);
  desc.width = info->width;
  desc.height = info->height;
  desc.depth = info->depth;
  desc.arrayLength = info->array_length;
  desc.mipmapLevelCount = info->mipmap_level_count;
  desc.sampleCount = info->sample_count;
  desc.usage = (MTLTextureUsage)info->usage;
  desc.resourceOptions = (MTLResourceOptions)info->options;
};

void
extract_texture_descriptor(id<MTLTexture> desc, struct WMTTextureInfo *info) {
  info->type = desc.textureType;
  info->pixel_format = desc.pixelFormat;
  info->width = desc.width;
  info->height = desc.height;
  info->depth = desc.depth;
  info->array_length = desc.arrayLength;
  info->mipmap_level_count = desc.mipmapLevelCount;
  info->sample_count = desc.sampleCount;
  info->usage = desc.usage;
  info->options = (enum WMTResourceOptions)desc.resourceOptions;
  info->reserved = 0;
};

static NTSTATUS
_MTLDevice_newTexture(void *obj) {
  struct unixcall_mtldevice_newtexture *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  struct WMTTextureInfo *info = params->info.ptr;
  MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
  fill_texture_descriptor(desc, info);

  id<MTLTexture> ret = [device newTextureWithDescriptor:desc];
  params->ret = (obj_handle_t)ret;
  info->gpu_resource_id = [ret gpuResourceID]._impl;
  info->mach_port = 0;

  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLBuffer_newTexture(void *obj) {
  struct unixcall_mtlbuffer_newtexture *params = obj;
  id<MTLBuffer> buffer = (id<MTLBuffer>)params->buffer;
  struct WMTTextureInfo *info = params->info.ptr;
  MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
  fill_texture_descriptor(desc, info);

  id<MTLTexture> ret = [buffer newTextureWithDescriptor:desc offset:params->offset bytesPerRow:params->bytes_per_row];
  params->ret = (obj_handle_t)ret;
  info->gpu_resource_id = [ret gpuResourceID]._impl;
  info->mach_port = 0;

  [desc release];
  return STATUS_SUCCESS;
}

static inline MTLTextureSwizzleChannels
to_metal_swizzle(struct WMTTextureSwizzleChannels swizzle, enum WMTPixelFormat format) {
  if (format & WMTPixelFormatRGB1Swizzle) {
    return MTLTextureSwizzleChannelsMake(
        (MTLTextureSwizzle)swizzle.r, (MTLTextureSwizzle)swizzle.g, (MTLTextureSwizzle)swizzle.b, MTLTextureSwizzleOne
    );
  }
  if (format & WMTPixelFormatR001Swizzle) {
    return MTLTextureSwizzleChannelsMake(
        (MTLTextureSwizzle)swizzle.r, MTLTextureSwizzleZero, MTLTextureSwizzleZero, MTLTextureSwizzleOne
    );
  }
  if (format & WMTPixelFormat0R01Swizzle) {
    return MTLTextureSwizzleChannelsMake(
        MTLTextureSwizzleOne, (MTLTextureSwizzle)swizzle.r, MTLTextureSwizzleOne, MTLTextureSwizzleOne
    );
  }
  if (format & WMTPixelFormatGBARSwizzle) {
    return MTLTextureSwizzleChannelsMake(
        (MTLTextureSwizzle)swizzle.g, (MTLTextureSwizzle)swizzle.b, (MTLTextureSwizzle)swizzle.a,
        (MTLTextureSwizzle)swizzle.r
    );
  }
  return MTLTextureSwizzleChannelsMake(
      (MTLTextureSwizzle)swizzle.r, (MTLTextureSwizzle)swizzle.g, (MTLTextureSwizzle)swizzle.b,
      (MTLTextureSwizzle)swizzle.a
  );
}

static NTSTATUS
_MTLTexture_newTextureView(void *obj) {
  struct unixcall_mtltexture_newtextureview *params = obj;
  id<MTLTexture> texture = (id<MTLTexture>)params->texture;

  id<MTLTexture> ret = [texture newTextureViewWithPixelFormat:to_metal_pixel_format(params->format)
                                                  textureType:(MTLTextureType)params->texture_type
                                                       levels:NSMakeRange(params->level_start, params->level_count)
                                                       slices:NSMakeRange(params->slice_start, params->slice_count)
                                                      swizzle:to_metal_swizzle(params->swizzle, params->format)];
  params->ret = (obj_handle_t)ret;
  params->gpu_resource_id = [ret gpuResourceID]._impl;
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_minimumLinearTextureAlignmentForPixelFormat(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  params->ret =
      [(id<MTLDevice>)params->handle minimumLinearTextureAlignmentForPixelFormat:to_metal_pixel_format(params->arg)];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newLibrary(void *obj) {
  struct unixcall_mtldevice_newlibrary *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  NSError *err = NULL;
  params->ret_library = (obj_handle_t)[device newLibraryWithData:(dispatch_data_t)params->data error:&err];
  params->ret_error = (obj_handle_t)err;
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newLibraryWithSource(void *obj) {
  struct unixcall_mtldevice_newlibrary_source *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  NSError *err = NULL;
  NSString *source =
      [[NSString alloc] initWithBytes:params->source.ptr length:params->source_length encoding:NSUTF8StringEncoding];
  FILE *dl = winemetal_debug_log();
  if (!source) {
    if (dl) {
      fprintf(
          dl, "[winemetal] newLibraryWithSource: NSString creation FAILED for %llu bytes\n",
          (unsigned long long)params->source_length
      );
      fclose(dl);
    }
    params->ret_library = 0;
    params->ret_error = 0;
    return STATUS_SUCCESS;
  }
  MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
  [options setLanguageVersion:MTLLanguageVersion3_1];
  params->ret_library = (obj_handle_t)[device newLibraryWithSource:source options:options error:&err];
  params->ret_error = (obj_handle_t)err;
  if (dl) {
    fprintf(
        dl, "[winemetal] newLibraryWithSource: lib=%p err=%p source_len=%llu\n", (void *)params->ret_library,
        (void *)params->ret_error, (unsigned long long)params->source_length
    );
    if (err) {
      fprintf(dl, "[winemetal] compile error: %s\n", [[err localizedDescription] UTF8String]);
    }
    fclose(dl);
  }
  [source release];
  [options release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLLibrary_newFunction(void *obj) {
  struct unixcall_generic_obj_uint64_obj_ret *params = obj;
  id<MTLLibrary> library = (id<MTLLibrary>)params->handle;
  FILE *dl = winemetal_debug_log();
  if (!library) {
    if (dl) {
      fprintf(dl, "[winemetal] newFunction called with NULL library!\n");
      fclose(dl);
    }
    params->ret = 0;
    return STATUS_SUCCESS;
  }
  NSString *name = [[NSString alloc] initWithCString:(char *)params->arg encoding:NSUTF8StringEncoding];
  params->ret = (obj_handle_t)[library newFunctionWithName:name];
  if (!params->ret) {
    NSArray<NSString *> *names = [library functionNames];
    if (dl) {
      fprintf(
          dl, "[winemetal] newFunction(%s) returned NULL, library has %lu functions:\n", (char *)params->arg,
          (unsigned long)names.count
      );
      for (NSString *n in names) {
        fprintf(dl, "[winemetal]   - %s\n", [n UTF8String]);
      }
    }
  } else {
    if (dl) {
      fprintf(dl, "[winemetal] newFunction(%s) -> %p OK\n", (char *)params->arg, (void *)params->ret);
    }
  }
  if (dl)
    fclose(dl);
  [name release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLFunction_copyVertexAttributes(void *obj) {
  struct unixcall_mtlfunction_vertex_attributes *params = obj;
  id<MTLFunction> function = (id<MTLFunction>)params->function;
  struct WMTFunctionVertexAttribute *out = (struct WMTFunctionVertexAttribute *)(uintptr_t)params->attributes;
  params->ret_count = 0;
  if (!function || !out || !params->max_attributes)
    return STATUS_SUCCESS;

  NSArray<MTLVertexAttribute *> *attributes = [function vertexAttributes];
  uint32_t count = 0;
  for (MTLVertexAttribute *attribute in attributes) {
    if (count >= params->max_attributes)
      break;
    struct WMTFunctionVertexAttribute *dst = &out[count];
    memset(dst, 0, sizeof(*dst));
    dst->attribute_index = (uint32_t)[attribute attributeIndex];
    dst->attribute_type = (uint32_t)[attribute attributeType];
    dst->active = [attribute isActive] ? true : false;
    dst->patch_data = [attribute isPatchData] ? true : false;
    dst->patch_control_point_data = [attribute isPatchControlPointData] ? true : false;
    const char *name = [[attribute name] UTF8String];
    if (name)
      snprintf(dst->name, sizeof(dst->name), "%s", name);
    count++;
  }
  params->ret_count = count;
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSString_lengthOfBytesUsingEncoding(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  params->ret = (uint64_t)[(NSString *)params->handle lengthOfBytesUsingEncoding:(NSStringEncoding)params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSObject_description(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(NSObject *)params->handle description];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newComputePipelineState(void *obj) {
  struct unixcall_mtldevice_newcomputepso *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  const struct WMTComputePipelineInfo *info = params->info.ptr;
  MTLComputePipelineDescriptor *descriptor = [[MTLComputePipelineDescriptor alloc] init];
  NSError *err = NULL;
  descriptor.computeFunction = (id<MTLFunction>)info->compute_function;
  descriptor.threadGroupSizeIsMultipleOfThreadExecutionWidth = info->tgsize_is_multiple_of_sgwidth;
  for (unsigned i = 0; i < 31; i++) {
    if (info->immutable_buffers & (1 << i))
      descriptor.buffers[i].mutability = MTLMutabilityImmutable;
  }
  if (info->num_binary_archives_for_lookup && info->binary_archives_for_lookup.ptr)
    descriptor.binaryArchives = winemetal_array_from_handles(
        (const obj_handle_t *)info->binary_archives_for_lookup.ptr, info->num_binary_archives_for_lookup,
        "compute.binaryArchives"
    );
  MTLPipelineOption options =
      info->fail_on_binary_archive_miss ? MTLPipelineOptionFailOnBinaryArchiveMiss : MTLPipelineOptionNone;
  @try {
    params->ret_pso = (obj_handle_t)
        [device newComputePipelineStateWithDescriptor:descriptor options:options reflection:nil error:&err];
  } @catch(NSException * exception) {
    params->ret_pso = 0;
    FILE *dl = winemetal_critical_log();
    if (dl) {
      fprintf(
          dl, "[winemetal] compute PSO ObjC exception: %s reason=%s\n", winemetal_nsstring_utf8([exception name]),
          winemetal_nsstring_utf8([exception reason])
      );
      fclose(dl);
    }
  }
  params->ret_error = (obj_handle_t)err;
  if (params->ret_pso && !err && info->binary_archive_for_serialization) {
    [(id<MTLBinaryArchive>)info->binary_archive_for_serialization addComputePipelineFunctionsWithDescriptor:descriptor
                                                                                                      error:&err];
  }
  [descriptor release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_blitCommandEncoder(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLCommandBuffer>)params->handle blitCommandEncoder];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_computeCommandEncoder(void *obj) {
  struct unixcall_generic_obj_uint64_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLCommandBuffer>)params->handle
      computeCommandEncoderWithDispatchType:params->arg ? MTLDispatchTypeConcurrent : MTLDispatchTypeSerial];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_renderCommandEncoder(void *obj) {
  struct unixcall_generic_obj_uint64_obj_ret *params = obj;
  struct WMTRenderPassInfo *info = (struct WMTRenderPassInfo *)params->arg;
  MTLRenderPassDescriptor *descriptor = [[MTLRenderPassDescriptor alloc] init];
  for (unsigned i = 0; i < 8; i++) {
    descriptor.colorAttachments[i].clearColor = MTLClearColorMake(
        info->colors[i].clear_color.r, info->colors[i].clear_color.g, info->colors[i].clear_color.b,
        info->colors[i].clear_color.a
    );
    descriptor.colorAttachments[i].level = info->colors[i].level;
    descriptor.colorAttachments[i].slice = info->colors[i].slice;
    descriptor.colorAttachments[i].depthPlane = info->colors[i].depth_plane;
    descriptor.colorAttachments[i].texture = (id<MTLTexture>)info->colors[i].texture;
    descriptor.colorAttachments[i].loadAction = (MTLLoadAction)info->colors[i].load_action;
    descriptor.colorAttachments[i].storeAction = (MTLStoreAction)info->colors[i].store_action;
    descriptor.colorAttachments[i].resolveTexture = (id<MTLTexture>)info->colors[i].resolve_texture;
    descriptor.colorAttachments[i].resolveLevel = info->colors[i].resolve_level;
    descriptor.colorAttachments[i].resolveSlice = info->colors[i].resolve_slice;
    descriptor.colorAttachments[i].resolveDepthPlane = info->colors[i].resolve_depth_plane;
  }

  if (info->depth.texture) {
    descriptor.depthAttachment.clearDepth = info->depth.clear_depth;
    descriptor.depthAttachment.depthPlane = info->depth.depth_plane;
    descriptor.depthAttachment.level = info->depth.level;
    descriptor.depthAttachment.slice = info->depth.slice;
    descriptor.depthAttachment.texture = (id<MTLTexture>)info->depth.texture;
    descriptor.depthAttachment.loadAction = (MTLLoadAction)info->depth.load_action;
    descriptor.depthAttachment.storeAction = (MTLStoreAction)info->depth.store_action;
  }

  if (info->stencil.texture) {
    descriptor.stencilAttachment.clearStencil = info->stencil.clear_stencil;
    descriptor.stencilAttachment.depthPlane = info->stencil.depth_plane;
    descriptor.stencilAttachment.level = info->stencil.level;
    descriptor.stencilAttachment.slice = info->stencil.slice;
    descriptor.stencilAttachment.texture = (id<MTLTexture>)info->stencil.texture;
    descriptor.stencilAttachment.loadAction = (MTLLoadAction)info->stencil.load_action;
    descriptor.stencilAttachment.storeAction = (MTLStoreAction)info->stencil.store_action;
  }

  descriptor.defaultRasterSampleCount = info->default_raster_sample_count;
  descriptor.renderTargetArrayLength = info->render_target_array_length;
  descriptor.renderTargetHeight = info->render_target_height;
  descriptor.renderTargetWidth = info->render_target_width;
  descriptor.visibilityResultBuffer = (id<MTLBuffer>)info->visibility_buffer;

  if (info->tile_height && info->tile_width) {
    descriptor.tileWidth = info->tile_width;
    descriptor.tileHeight = info->tile_height;
  }

  params->ret = (obj_handle_t)[(id<MTLCommandBuffer>)params->handle renderCommandEncoderWithDescriptor:descriptor];

  [descriptor release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandEncoder_endEncoding(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;
  return winemetal_end_encoder_safely((id<MTLCommandEncoder>)params->handle, "explicit") ? STATUS_SUCCESS
                                                                                         : STATUS_UNSUCCESSFUL;
}

#ifndef DXMT_NO_PRIVATE_API

typedef NS_ENUM(NSUInteger, MTLLogicOperation) {
  MTLLogicOperationClear,
  MTLLogicOperationSet,
  MTLLogicOperationCopy,
  MTLLogicOperationCopyInverted,
  MTLLogicOperationNoop,
  MTLLogicOperationInvert,
  MTLLogicOperationAnd,
  MTLLogicOperationNand,
  MTLLogicOperationOr,
  MTLLogicOperationNor,
  MTLLogicOperationXor,
  MTLLogicOperationEquivalence,
  MTLLogicOperationAndReverse,
  MTLLogicOperationAndInverted,
  MTLLogicOperationOrReverse,
  MTLLogicOperationOrInverted,
};

@interface MTLRenderPipelineDescriptor ()

- (void)setLogicOperationEnabled:(BOOL)enable;
- (void)setLogicOperation:(MTLLogicOperation)op;

@end

@interface MTLMeshRenderPipelineDescriptor ()

- (void)setLogicOperationEnabled:(BOOL)enable;
- (void)setLogicOperation:(MTLLogicOperation)op;

@end

#endif

static NTSTATUS
_MTLDevice_newRenderPipelineState(void *obj) {
  struct unixcall_mtldevice_newrenderpso *params = obj;
  const struct WMTRenderPipelineInfo *info = params->info.ptr;
  MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];

  for (unsigned i = 0; i < 8; i++) {
    descriptor.colorAttachments[i].pixelFormat = to_metal_pixel_format(info->colors[i].pixel_format);
    descriptor.colorAttachments[i].blendingEnabled = info->colors[i].blending_enabled;
    descriptor.colorAttachments[i].writeMask = (MTLColorWriteMask)info->colors[i].write_mask;

    descriptor.colorAttachments[i].alphaBlendOperation = (MTLBlendOperation)info->colors[i].alpha_blend_operation;
    descriptor.colorAttachments[i].rgbBlendOperation = (MTLBlendOperation)info->colors[i].rgb_blend_operation;

    descriptor.colorAttachments[i].sourceRGBBlendFactor = (MTLBlendFactor)info->colors[i].src_rgb_blend_factor;
    descriptor.colorAttachments[i].sourceAlphaBlendFactor = (MTLBlendFactor)info->colors[i].src_alpha_blend_factor;
    descriptor.colorAttachments[i].destinationRGBBlendFactor = (MTLBlendFactor)info->colors[i].dst_rgb_blend_factor;
    descriptor.colorAttachments[i].destinationAlphaBlendFactor = (MTLBlendFactor)info->colors[i].dst_alpha_blend_factor;
  }

  for (unsigned i = 0; i < 31; i++) {
    if (info->immutable_fragment_buffers & (1 << i))
      descriptor.fragmentBuffers[i].mutability = MTLMutabilityImmutable;
    if (info->immutable_vertex_buffers & (1 << i))
      descriptor.vertexBuffers[i].mutability = MTLMutabilityImmutable;
  }

#ifndef DXMT_NO_PRIVATE_API
  [descriptor setLogicOperationEnabled:info->logic_operation_enabled];
  [descriptor setLogicOperation:(MTLLogicOperation)info->logic_operation];
#endif
  descriptor.depthAttachmentPixelFormat = to_metal_pixel_format(info->depth_pixel_format);
  descriptor.stencilAttachmentPixelFormat = to_metal_pixel_format(info->stencil_pixel_format);
  descriptor.alphaToCoverageEnabled = info->alpha_to_coverage_enabled;
  descriptor.rasterizationEnabled = info->rasterization_enabled;
  descriptor.rasterSampleCount = info->raster_sample_count;
  descriptor.inputPrimitiveTopology = (MTLPrimitiveTopologyClass)info->input_primitive_topology;
  descriptor.tessellationPartitionMode = (MTLTessellationPartitionMode)info->tessellation_partition_mode;
  descriptor.tessellationFactorStepFunction = (MTLTessellationFactorStepFunction)info->tessellation_factor_step;
  descriptor.tessellationOutputWindingOrder = (MTLWinding)info->tessellation_output_winding_order;
  descriptor.maxTessellationFactor = info->max_tessellation_factor;

  descriptor.vertexFunction = (id<MTLFunction>)info->vertex_function;
  descriptor.fragmentFunction = (id<MTLFunction>)info->fragment_function;

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 130000
  if (@available(macOS 13, *)) {
    if (info->num_vertex_linked_functions && info->vertex_linked_functions.ptr) {
      MTLLinkedFunctions *linked = [[MTLLinkedFunctions alloc] init];
      linked.functions = winemetal_array_from_handles(
          (const obj_handle_t *)info->vertex_linked_functions.ptr, info->num_vertex_linked_functions,
          "render.vertexLinkedFunctions"
      );
      descriptor.vertexLinkedFunctions = linked;
      [linked release];
    }
    if (info->num_fragment_linked_functions && info->fragment_linked_functions.ptr) {
      MTLLinkedFunctions *linked = [[MTLLinkedFunctions alloc] init];
      linked.functions = winemetal_array_from_handles(
          (const obj_handle_t *)info->fragment_linked_functions.ptr, info->num_fragment_linked_functions,
          "render.fragmentLinkedFunctions"
      );
      descriptor.fragmentLinkedFunctions = linked;
      [linked release];
    }
  }
#endif

  if (info->vertex_descriptor &&
      (info->vertex_descriptor->attribute_count > 0 || info->vertex_descriptor->layout_count > 0)) {
    MTLVertexDescriptor *vd = [[MTLVertexDescriptor alloc] init];
    for (uint32_t i = 0; i < info->vertex_descriptor->layout_count && i < WMT_MAX_VERTEX_BUFFER_LAYOUTS; i++) {
      struct WMTVertexBufferLayoutDesc l = info->vertex_descriptor->layouts[i];
      if (l.stride > 0) {
        vd.layouts[i].stride = l.stride;
        vd.layouts[i].stepFunction = (MTLVertexStepFunction)l.step_function;
        vd.layouts[i].stepRate = l.step_rate;
      }
    }
    for (uint32_t i = 0; i < info->vertex_descriptor->attribute_count && i < WMT_MAX_VERTEX_ATTRIBUTES; i++) {
      struct WMTVertexAttributeDesc a = info->vertex_descriptor->attributes[i];
      if (a.format != WMTAttributeFormatInvalid) {
        vd.attributes[i].format = (MTLVertexFormat)a.format;
        vd.attributes[i].offset = a.offset;
        vd.attributes[i].bufferIndex = a.buffer_index;
      }
    }
    descriptor.vertexDescriptor = vd;
    [vd release];
  }

  if (info->num_binary_archives_for_lookup && info->binary_archives_for_lookup.ptr)
    descriptor.binaryArchives = winemetal_array_from_handles(
        (const obj_handle_t *)info->binary_archives_for_lookup.ptr, info->num_binary_archives_for_lookup,
        "render.binaryArchives"
    );
  NSError *err = NULL;
  MTLPipelineOption options =
      info->fail_on_binary_archive_miss ? MTLPipelineOptionFailOnBinaryArchiveMiss : MTLPipelineOptionNone;
  @try {
    params->ret_pso = (obj_handle_t)[(id<MTLDevice>)params->device newRenderPipelineStateWithDescriptor:descriptor
                                                                                                options:options
                                                                                             reflection:nil
                                                                                                  error:&err];
  } @catch(NSException * exception) {
    params->ret_pso = 0;
    FILE *dl = winemetal_critical_log();
    if (dl) {
      fprintf(
          dl,
          "[winemetal] render PSO ObjC exception: %s reason=%s vertex=%p fragment=%p rt0=%u depth=%u stencil=%u samples=%u\n",
          winemetal_nsstring_utf8([exception name]), winemetal_nsstring_utf8([exception reason]),
          (void *)info->vertex_function, (void *)info->fragment_function, info->colors[0].pixel_format,
          info->depth_pixel_format, info->stencil_pixel_format, info->raster_sample_count
      );
      fclose(dl);
    }
  }
  params->ret_error = (obj_handle_t)err;
  if (params->ret_pso && !err && info->binary_archive_for_serialization) {
    [(id<MTLBinaryArchive>)info->binary_archive_for_serialization addRenderPipelineFunctionsWithDescriptor:descriptor
                                                                                                     error:&err];
  }
  [descriptor release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newMeshRenderPipelineState(void *obj) {
  struct unixcall_mtldevice_newmeshrenderpso *params = obj;
  const struct WMTMeshRenderPipelineInfo *info = params->info.ptr;
  MTLMeshRenderPipelineDescriptor *descriptor = [[MTLMeshRenderPipelineDescriptor alloc] init];

  for (unsigned i = 0; i < 8; i++) {
    descriptor.colorAttachments[i].pixelFormat = to_metal_pixel_format(info->colors[i].pixel_format);
    descriptor.colorAttachments[i].blendingEnabled = info->colors[i].blending_enabled;
    descriptor.colorAttachments[i].writeMask = (MTLColorWriteMask)info->colors[i].write_mask;

    descriptor.colorAttachments[i].alphaBlendOperation = (MTLBlendOperation)info->colors[i].alpha_blend_operation;
    descriptor.colorAttachments[i].rgbBlendOperation = (MTLBlendOperation)info->colors[i].rgb_blend_operation;

    descriptor.colorAttachments[i].sourceRGBBlendFactor = (MTLBlendFactor)info->colors[i].src_rgb_blend_factor;
    descriptor.colorAttachments[i].sourceAlphaBlendFactor = (MTLBlendFactor)info->colors[i].src_alpha_blend_factor;
    descriptor.colorAttachments[i].destinationRGBBlendFactor = (MTLBlendFactor)info->colors[i].dst_rgb_blend_factor;
    descriptor.colorAttachments[i].destinationAlphaBlendFactor = (MTLBlendFactor)info->colors[i].dst_alpha_blend_factor;
  }

  for (unsigned i = 0; i < 31; i++) {
    if (info->immutable_fragment_buffers & (1 << i))
      descriptor.fragmentBuffers[i].mutability = MTLMutabilityImmutable;
    if (info->immutable_mesh_buffers & (1 << i))
      descriptor.meshBuffers[i].mutability = MTLMutabilityImmutable;
    if (info->immutable_object_buffers & (1 << i))
      descriptor.objectBuffers[i].mutability = MTLMutabilityImmutable;
  }

#ifndef DXMT_NO_PRIVATE_API
  [descriptor setLogicOperationEnabled:info->logic_operation_enabled];
  [descriptor setLogicOperation:(MTLLogicOperation)info->logic_operation];
#endif
  descriptor.depthAttachmentPixelFormat = to_metal_pixel_format(info->depth_pixel_format);
  descriptor.stencilAttachmentPixelFormat = to_metal_pixel_format(info->stencil_pixel_format);
  descriptor.alphaToCoverageEnabled = info->alpha_to_coverage_enabled;
  descriptor.rasterizationEnabled = info->rasterization_enabled;
  descriptor.rasterSampleCount = info->raster_sample_count;

  descriptor.objectFunction = (id<MTLFunction>)info->object_function;
  descriptor.meshFunction = (id<MTLFunction>)info->mesh_function;
  descriptor.fragmentFunction = (id<MTLFunction>)info->fragment_function;
  descriptor.payloadMemoryLength = info->payload_memory_length;

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 130000
  if (@available(macOS 13, *)) {
    if (info->num_object_linked_functions && info->object_linked_functions.ptr) {
      MTLLinkedFunctions *linked = [[MTLLinkedFunctions alloc] init];
      linked.functions = winemetal_array_from_handles(
          (const obj_handle_t *)info->object_linked_functions.ptr, info->num_object_linked_functions,
          "mesh.objectLinkedFunctions"
      );
      descriptor.objectLinkedFunctions = linked;
      [linked release];
    }
    if (info->num_mesh_linked_functions && info->mesh_linked_functions.ptr) {
      MTLLinkedFunctions *linked = [[MTLLinkedFunctions alloc] init];
      linked.functions = winemetal_array_from_handles(
          (const obj_handle_t *)info->mesh_linked_functions.ptr, info->num_mesh_linked_functions,
          "mesh.meshLinkedFunctions"
      );
      descriptor.meshLinkedFunctions = linked;
      [linked release];
    }
    if (info->num_fragment_linked_functions && info->fragment_linked_functions.ptr) {
      MTLLinkedFunctions *linked = [[MTLLinkedFunctions alloc] init];
      linked.functions = winemetal_array_from_handles(
          (const obj_handle_t *)info->fragment_linked_functions.ptr, info->num_fragment_linked_functions,
          "mesh.fragmentLinkedFunctions"
      );
      descriptor.fragmentLinkedFunctions = linked;
      [linked release];
    }
  }
#endif

  descriptor.meshThreadgroupSizeIsMultipleOfThreadExecutionWidth = info->mesh_tgsize_is_multiple_of_sgwidth;
  descriptor.objectThreadgroupSizeIsMultipleOfThreadExecutionWidth = info->object_tgsize_is_multiple_of_sgwidth;

  MTLPipelineOption options = MTLPipelineOptionNone;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
  if (@available(macOS 15, *)) {
    if (info->num_binary_archives_for_lookup && info->binary_archives_for_lookup.ptr)
      descriptor.binaryArchives = winemetal_array_from_handles(
          (const obj_handle_t *)info->binary_archives_for_lookup.ptr, info->num_binary_archives_for_lookup,
          "mesh.binaryArchives"
      );
    options = info->fail_on_binary_archive_miss ? MTLPipelineOptionFailOnBinaryArchiveMiss : MTLPipelineOptionNone;
  }
#endif
  NSError *err = NULL;
  @try {
    params->ret_pso = (obj_handle_t)[(id<MTLDevice>)params->device newRenderPipelineStateWithMeshDescriptor:descriptor
                                                                                                    options:options
                                                                                                 reflection:nil
                                                                                                      error:&err];
  } @catch(NSException * exception) {
    params->ret_pso = 0;
    FILE *dl = winemetal_critical_log();
    if (dl) {
      fprintf(
          dl, "[winemetal] mesh PSO ObjC exception: %s reason=%s\n", winemetal_nsstring_utf8([exception name]),
          winemetal_nsstring_utf8([exception reason])
      );
      fclose(dl);
    }
  }
  params->ret_error = (obj_handle_t)err;
  if (!params->ret_pso) {
    FILE *dl = winemetal_critical_log();
    if (dl) {
      fprintf(
          dl,
          "[winemetal] mesh PSO failed: err=%s object=%s mesh=%s fragment=%s "
          "payload=%u rt0=%u depth=%u stencil=%u samples=%u raster=%d "
          "obj_linked=%u mesh_linked=%u frag_linked=%u "
          "obj_mut=0x%08x mesh_mut=0x%08x frag_mut=0x%08x\n",
          err ? winemetal_nsstring_utf8([err localizedDescription]) : "<nil>",
          winemetal_nsstring_utf8([descriptor.objectFunction name]),
          winemetal_nsstring_utf8([descriptor.meshFunction name]),
          winemetal_nsstring_utf8([descriptor.fragmentFunction name]), info->payload_memory_length,
          info->colors[0].pixel_format, info->depth_pixel_format, info->stencil_pixel_format, info->raster_sample_count,
          info->rasterization_enabled ? 1 : 0, info->num_object_linked_functions, info->num_mesh_linked_functions,
          info->num_fragment_linked_functions, info->immutable_object_buffers, info->immutable_mesh_buffers,
          info->immutable_fragment_buffers
      );
      if (err)
        fprintf(dl, "[winemetal] mesh PSO userInfo: %s\n", winemetal_nsstring_utf8([[err userInfo] description]));
      fclose(dl);
    }
  }
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
  if (@available(macOS 15, *)) {
    if (params->ret_pso && !err && info->binary_archive_for_serialization) {
      [(id<MTLBinaryArchive>)info->binary_archive_for_serialization
          addMeshRenderPipelineFunctionsWithDescriptor:descriptor
                                                 error:&err];
    }
  }
#endif
  [descriptor release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLBlitCommandEncoder_encodeCommands(void *obj) {
  struct unixcall_generic_obj_cmd_noret *params = obj;
  const struct wmtcmd_base *next = params->cmd_head.ptr;
  id<MTLBlitCommandEncoder> encoder = (id<MTLBlitCommandEncoder>)params->encoder;
  while (next) {
    switch ((enum WMTBlitCommandType)next->type) {
    default:
      assert(!next->type && "unhandled blit command type");
      break;
    case WMTBlitCommandCopyFromBufferToBuffer: {
      struct wmtcmd_blit_copy_from_buffer_to_buffer *body = (struct wmtcmd_blit_copy_from_buffer_to_buffer *)next;
      [encoder copyFromBuffer:(id<MTLBuffer>)body->src
                 sourceOffset:body->src_offset
                     toBuffer:(id<MTLBuffer>)body->dst
            destinationOffset:body->dst_offset
                         size:body->copy_length];
      break;
    }
    case WMTBlitCommandCopyFromBufferToTexture: {
      struct wmtcmd_blit_copy_from_buffer_to_texture *body = (struct wmtcmd_blit_copy_from_buffer_to_texture *)next;
      [encoder copyFromBuffer:(id<MTLBuffer>)body->src
                 sourceOffset:body->src_offset
            sourceBytesPerRow:body->bytes_per_row
          sourceBytesPerImage:body->bytes_per_image
                   sourceSize:MTLSizeMake(body->size.width, body->size.height, body->size.depth)
                    toTexture:(id<MTLTexture>)body->dst
             destinationSlice:body->slice
             destinationLevel:body->level
            destinationOrigin:MTLOriginMake(body->origin.x, body->origin.y, body->origin.z)];
      break;
    }
    case WMTBlitCommandCopyFromBufferToTextureWithBlitOption: {
      struct wmtcmd_blit_copy_from_buffer_to_texture_withblitoption *body =
          (struct wmtcmd_blit_copy_from_buffer_to_texture_withblitoption *)next;
      [encoder copyFromBuffer:(id<MTLBuffer>)body->src
                 sourceOffset:body->src_offset
            sourceBytesPerRow:body->bytes_per_row
          sourceBytesPerImage:body->bytes_per_image
                   sourceSize:MTLSizeMake(body->size.width, body->size.height, body->size.depth)
                    toTexture:(id<MTLTexture>)body->dst
             destinationSlice:body->slice
             destinationLevel:body->level
            destinationOrigin:MTLOriginMake(body->origin.x, body->origin.y, body->origin.z)
                      options:(MTLBlitOption)body->options];
      break;
    }
    case WMTBlitCommandCopyFromTextureToBuffer: {
      struct wmtcmd_blit_copy_from_texture_to_buffer *body = (struct wmtcmd_blit_copy_from_texture_to_buffer *)next;
      [encoder copyFromTexture:(id<MTLTexture>)body->src
                       sourceSlice:body->slice
                       sourceLevel:body->level
                      sourceOrigin:MTLOriginMake(body->origin.x, body->origin.y, body->origin.z)
                        sourceSize:MTLSizeMake(body->size.width, body->size.height, body->size.depth)
                          toBuffer:(id<MTLBuffer>)body->dst
                 destinationOffset:body->offset
            destinationBytesPerRow:body->bytes_per_row
          destinationBytesPerImage:body->bytes_per_image];
      break;
    }
    case WMTBlitCommandCopyFromTextureToTexture: {
      struct wmtcmd_blit_copy_from_texture_to_texture *body = (struct wmtcmd_blit_copy_from_texture_to_texture *)next;
      [encoder copyFromTexture:(id<MTLTexture>)body->src
                   sourceSlice:body->src_slice
                   sourceLevel:body->src_level
                  sourceOrigin:MTLOriginMake(body->src_origin.x, body->src_origin.y, body->src_origin.z)
                    sourceSize:MTLSizeMake(body->src_size.width, body->src_size.height, body->src_size.depth)
                     toTexture:(id<MTLTexture>)body->dst
              destinationSlice:body->dst_slice
              destinationLevel:body->dst_level
             destinationOrigin:MTLOriginMake(body->dst_origin.x, body->dst_origin.y, body->dst_origin.z)];
      break;
    }
    case WMTBlitCommandGenerateMipmaps: {
      struct wmtcmd_blit_generate_mipmaps *body = (struct wmtcmd_blit_generate_mipmaps *)next;
      [encoder generateMipmapsForTexture:(id<MTLTexture>)body->texture];
      break;
    }
    case WMTBlitCommandUpdateFence: {
      struct wmtcmd_blit_fence_op *body = (struct wmtcmd_blit_fence_op *)next;
      [encoder updateFence:(id<MTLFence>)body->fence];
      break;
    }
    case WMTBlitCommandWaitForFence: {
      struct wmtcmd_blit_fence_op *body = (struct wmtcmd_blit_fence_op *)next;
      [encoder waitForFence:(id<MTLFence>)body->fence];
      break;
    }
    case WMTBlitCommandFillBuffer: {
      struct wmtcmd_blit_fillbuffer *body = (struct wmtcmd_blit_fillbuffer *)next;
      [encoder fillBuffer:(id<MTLBuffer>)body->buffer range:NSMakeRange(body->offset, body->length) value:body->value];
      break;
    }
    case WMTBlitCommandResolveCounters: {
      struct wmtcmd_blit_resolvecounters *body = (struct wmtcmd_blit_resolvecounters *)next;
      [encoder resolveCounters:(id<MTLCounterSampleBuffer>)body->sample_buffer
                       inRange:NSMakeRange(body->start, body->len)
             destinationBuffer:(id<MTLBuffer>)body->dst_buffer
             destinationOffset:body->dst_offset];
      break;
    }
    }

    next = next->next.ptr;
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLComputeCommandEncoder_encodeCommands(void *obj) {
  struct unixcall_generic_obj_cmd_noret *params = obj;
  const struct wmtcmd_base *next = params->cmd_head.ptr;
  id<MTLComputeCommandEncoder> encoder = (id<MTLComputeCommandEncoder>)params->encoder;
  MTLSize threadgroup_size = {0, 0, 0};
  while (next) {
    switch ((enum WMTComputeCommandType)next->type) {
    default:
      assert(!next->type && "unhandled compute command type");
      break;
    case WMTComputeCommandDispatch: {
      struct wmtcmd_compute_dispatch *body = (struct wmtcmd_compute_dispatch *)next;
      [encoder dispatchThreadgroups:MTLSizeMake(body->size.width, body->size.height, body->size.depth)
              threadsPerThreadgroup:threadgroup_size];
      break;
    }
    case WMTComputeCommandDispatchThreads: {
      struct wmtcmd_compute_dispatch *body = (struct wmtcmd_compute_dispatch *)next;
      [encoder dispatchThreads:MTLSizeMake(body->size.width, body->size.height, body->size.depth)
          threadsPerThreadgroup:threadgroup_size];
      break;
    }
    case WMTComputeCommandDispatchIndirect: {
      struct wmtcmd_compute_dispatch_indirect *body = (struct wmtcmd_compute_dispatch_indirect *)next;
      [encoder dispatchThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                                 indirectBufferOffset:body->indirect_args_offset
                                threadsPerThreadgroup:threadgroup_size];
      break;
    }
    case WMTComputeCommandSetPSO: {
      struct wmtcmd_compute_setpso *body = (struct wmtcmd_compute_setpso *)next;
      [encoder setComputePipelineState:(id<MTLComputePipelineState>)body->pso];
      threadgroup_size.width = body->threadgroup_size.width;
      threadgroup_size.height = body->threadgroup_size.height;
      threadgroup_size.depth = body->threadgroup_size.depth;
      break;
    }
    case WMTComputeCommandSetBuffer: {
      struct wmtcmd_compute_setbuffer *body = (struct wmtcmd_compute_setbuffer *)next;
      [encoder setBuffer:(id<MTLBuffer>)body->buffer offset:body->offset atIndex:body->index];
      break;
    }
    case WMTComputeCommandSetBufferOffset: {
      struct wmtcmd_compute_setbufferoffset *body = (struct wmtcmd_compute_setbufferoffset *)next;
      [encoder setBufferOffset:body->offset atIndex:body->index];
      break;
    }
    case WMTComputeCommandUseResource: {
      struct wmtcmd_compute_useresource *body = (struct wmtcmd_compute_useresource *)next;
      [encoder useResource:(id<MTLResource>)body->resource usage:(MTLResourceUsage)body->usage];
      break;
    }
    case WMTComputeCommandSetBytes: {
      struct wmtcmd_compute_setbytes *body = (struct wmtcmd_compute_setbytes *)next;
      [encoder setBytes:body->bytes.ptr length:body->length atIndex:body->index];
      break;
    }
    case WMTComputeCommandSetTexture: {
      struct wmtcmd_compute_settexture *body = (struct wmtcmd_compute_settexture *)next;
      [encoder setTexture:(id<MTLTexture>)body->texture atIndex:body->index];
      break;
    }
    case WMTComputeCommandSetSamplerState: {
      struct wmtcmd_compute_setsamplerstate *body = (struct wmtcmd_compute_setsamplerstate *)next;
      [encoder setSamplerState:(id<MTLSamplerState>)body->sampler atIndex:body->index];
      break;
    }
    case WMTComputeCommandUpdateFence: {
      struct wmtcmd_compute_fence_op *body = (struct wmtcmd_compute_fence_op *)next;
      [encoder updateFence:(id<MTLFence>)body->fence];
      break;
    }
    case WMTComputeCommandWaitForFence: {
      struct wmtcmd_compute_fence_op *body = (struct wmtcmd_compute_fence_op *)next;
      [encoder waitForFence:(id<MTLFence>)body->fence];
      break;
    }
    case WMTComputeCommandMemoryBarrier: {
      struct wmtcmd_compute_memory_barrier *body = (struct wmtcmd_compute_memory_barrier *)next;
      [encoder memoryBarrierWithScope:(MTLBarrierScope)body->scope];
      break;
    }
    }

    next = next->next.ptr;
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLRenderCommandEncoder_encodeCommands(void *obj) {
  struct unixcall_generic_obj_cmd_noret *params = obj;
  const struct wmtcmd_base *next = params->cmd_head.ptr;
  id<MTLRenderCommandEncoder> encoder = (id<MTLRenderCommandEncoder>)params->encoder;
  FILE *dl = winemetal_debug_log();
  size_t command_index = 0;

  if (dl) {
    fprintf(dl, "render_encode_begin encoder=%p cmd_head=%p\n", encoder, next);
  }

  while (next) {
    winemetal_log_render_command(dl, command_index, encoder, next);

    @try {
      switch ((enum WMTRenderCommandType)next->type) {
      default: {
        FILE *el = winemetal_critical_log();
        if (el) {
          fprintf(
              el, "render_cmd_unknown index=%zu encoder=%p cmd=%p type=%u next=%p\n", command_index, encoder, next,
              next->type, next->next.ptr
          );
          fclose(el);
        }
        if (dl) {
          fclose(dl);
        }
        winemetal_end_encoder_safely((id<MTLCommandEncoder>)encoder, "render_unknown_command");
        return STATUS_UNSUCCESSFUL;
      }
      case WMTRenderCommandNop:
        break;
      case WMTRenderCommandUseResource: {
        struct wmtcmd_render_useresource *body = (struct wmtcmd_render_useresource *)next;
        [encoder useResource:(id<MTLResource>)body->resource
                       usage:(MTLResourceUsage)body->usage
                      stages:(MTLRenderStages)body->stages];
        break;
      }
      case WMTRenderCommandSetVertexBuffer: {
        struct wmtcmd_render_setbuffer *body = (struct wmtcmd_render_setbuffer *)next;
        [encoder setVertexBuffer:(id<MTLBuffer>)body->buffer offset:body->offset atIndex:body->index];
        break;
      }
      case WMTRenderCommandSetVertexBufferOffset: {
        struct wmtcmd_render_setbufferoffset *body = (struct wmtcmd_render_setbufferoffset *)next;
        [encoder setVertexBufferOffset:body->offset atIndex:body->index];
        break;
      }
      case WMTRenderCommandSetFragmentBuffer: {
        struct wmtcmd_render_setbuffer *body = (struct wmtcmd_render_setbuffer *)next;
        [encoder setFragmentBuffer:(id<MTLBuffer>)body->buffer offset:body->offset atIndex:body->index];
        break;
      }
      case WMTRenderCommandSetFragmentBufferOffset: {
        struct wmtcmd_render_setbufferoffset *body = (struct wmtcmd_render_setbufferoffset *)next;
        [encoder setFragmentBufferOffset:body->offset atIndex:body->index];
        break;
      }
      case WMTRenderCommandSetMeshBuffer: {
        struct wmtcmd_render_setbuffer *body = (struct wmtcmd_render_setbuffer *)next;
        [encoder setMeshBuffer:(id<MTLBuffer>)body->buffer offset:body->offset atIndex:body->index];
        break;
      }
      case WMTRenderCommandSetMeshBufferOffset: {
        struct wmtcmd_render_setbufferoffset *body = (struct wmtcmd_render_setbufferoffset *)next;
        [encoder setMeshBufferOffset:body->offset atIndex:body->index];
        break;
      }
      case WMTRenderCommandSetObjectBuffer: {
        struct wmtcmd_render_setbuffer *body = (struct wmtcmd_render_setbuffer *)next;
        [encoder setObjectBuffer:(id<MTLBuffer>)body->buffer offset:body->offset atIndex:body->index];
        break;
      }
      case WMTRenderCommandSetObjectBufferOffset: {
        struct wmtcmd_render_setbufferoffset *body = (struct wmtcmd_render_setbufferoffset *)next;
        [encoder setObjectBufferOffset:body->offset atIndex:body->index];
        break;
      }
      case WMTRenderCommandSetFragmentBytes: {
        struct wmtcmd_render_setbytes *body = (struct wmtcmd_render_setbytes *)next;
        [encoder setFragmentBytes:body->bytes.ptr length:body->length atIndex:body->index];
        break;
      }
      case WMTRenderCommandSetFragmentTexture: {
        struct wmtcmd_render_settexture *body = (struct wmtcmd_render_settexture *)next;
        [encoder setFragmentTexture:(id<MTLTexture>)body->texture atIndex:body->index];
        break;
      }
      case WMTRenderCommandSetFragmentSamplerState: {
        struct wmtcmd_render_setsamplerstate *body = (struct wmtcmd_render_setsamplerstate *)next;
        [encoder setFragmentSamplerState:(id<MTLSamplerState>)body->sampler atIndex:body->index];
        break;
      }
      case WMTRenderCommandSetRasterizerState: {
        struct wmtcmd_render_setrasterizerstate *body = (struct wmtcmd_render_setrasterizerstate *)next;
        [encoder setTriangleFillMode:(MTLTriangleFillMode)body->fill_mode];
        [encoder setCullMode:(MTLCullMode)body->cull_mode];
        [encoder setDepthClipMode:(MTLDepthClipMode)body->depth_clip_mode];
        [encoder setDepthBias:body->depth_bias slopeScale:body->scole_scale clamp:body->depth_bias_clamp];
        [encoder setFrontFacingWinding:(MTLWinding)body->winding];
        break;
      }
      case WMTRenderCommandSetViewports: {
        struct wmtcmd_render_setviewports *body = (struct wmtcmd_render_setviewports *)next;
        [encoder setViewports:(const MTLViewport *)body->viewports.ptr count:body->viewport_count];
        break;
      }
      case WMTRenderCommandSetScissorRects: {
        struct wmtcmd_render_setscissorrects *body = (struct wmtcmd_render_setscissorrects *)next;
        [encoder setScissorRects:(const MTLScissorRect *)body->scissor_rects.ptr count:body->rect_count];
        break;
      }
      case WMTRenderCommandSetPSO: {
        struct wmtcmd_render_setpso *body = (struct wmtcmd_render_setpso *)next;
        [encoder setRenderPipelineState:(id<MTLRenderPipelineState>)body->pso];
        break;
      }
      case WMTRenderCommandSetDSSO: {
        struct wmtcmd_render_setdsso *body = (struct wmtcmd_render_setdsso *)next;
        [encoder setDepthStencilState:(id<MTLDepthStencilState>)body->dsso];
        [encoder setStencilReferenceValue:body->stencil_ref];
        break;
      }
      case WMTRenderCommandSetBlendFactorAndStencilRef: {
        struct wmtcmd_render_setblendcolor *body = (struct wmtcmd_render_setblendcolor *)next;
        [encoder setBlendColorRed:body->red green:body->green blue:body->blue alpha:body->alpha];
        [encoder setStencilReferenceValue:body->stencil_ref];
        break;
      }
      case WMTRenderCommandSetVisibilityMode: {
        struct wmtcmd_render_setvisibilitymode *body = (struct wmtcmd_render_setvisibilitymode *)next;
        [encoder setVisibilityResultMode:(MTLVisibilityResultMode)body->mode offset:body->offset];
        break;
      }
      case WMTRenderCommandDraw: {
        struct wmtcmd_render_draw *body = (struct wmtcmd_render_draw *)next;
        [encoder drawPrimitives:(MTLPrimitiveType)body->primitive_type
                    vertexStart:body->vertex_start
                    vertexCount:body->vertex_count
                  instanceCount:body->instance_count
                   baseInstance:body->base_instance];
        break;
      }
      case WMTRenderCommandDrawIndexed: {
        struct wmtcmd_render_draw_indexed *body = (struct wmtcmd_render_draw_indexed *)next;
        [encoder drawIndexedPrimitives:(MTLPrimitiveType)body->primitive_type
                            indexCount:body->index_count
                             indexType:(MTLIndexType)body->index_type
                           indexBuffer:(id<MTLBuffer>)body->index_buffer
                     indexBufferOffset:body->index_buffer_offset
                         instanceCount:body->instance_count
                            baseVertex:body->base_vertex
                          baseInstance:body->base_instance];
        break;
      }
      case WMTRenderCommandDrawIndirect: {
        struct wmtcmd_render_draw_indirect *body = (struct wmtcmd_render_draw_indirect *)next;
        [encoder drawPrimitives:(MTLPrimitiveType)body->primitive_type
                  indirectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
            indirectBufferOffset:body->indirect_args_offset];
        break;
      }
      case WMTRenderCommandDrawIndexedIndirect: {
        struct wmtcmd_render_draw_indexed_indirect *body = (struct wmtcmd_render_draw_indexed_indirect *)next;
        [encoder drawIndexedPrimitives:(MTLPrimitiveType)body->primitive_type
                             indexType:(MTLIndexType)body->index_type
                           indexBuffer:(id<MTLBuffer>)body->index_buffer
                     indexBufferOffset:body->index_buffer_offset
                        indirectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                  indirectBufferOffset:body->indirect_args_offset];
        break;
      }
      case WMTRenderCommandDrawMeshThreadgroups: {
        struct wmtcmd_render_draw_meshthreadgroups *body = (struct wmtcmd_render_draw_meshthreadgroups *)next;
        [encoder drawMeshThreadgroups:MTLSizeMake(
                                          body->threadgroup_per_grid.width, body->threadgroup_per_grid.height,
                                          body->threadgroup_per_grid.depth
                                      )
            threadsPerObjectThreadgroup:MTLSizeMake(
                                            body->object_threadgroup_size.width, body->object_threadgroup_size.height,
                                            body->object_threadgroup_size.depth
                                        )
              threadsPerMeshThreadgroup:MTLSizeMake(
                                            body->mesh_threadgroup_size.width, body->mesh_threadgroup_size.height,
                                            body->mesh_threadgroup_size.depth
                                        )];
        break;
      }
      case WMTRenderCommandDrawMeshThreadgroupsIndirect: {
        struct wmtcmd_render_draw_meshthreadgroups_indirect *body =
            (struct wmtcmd_render_draw_meshthreadgroups_indirect *)next;
        [encoder drawMeshThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                                   indirectBufferOffset:body->indirect_args_offset
                            threadsPerObjectThreadgroup:MTLSizeMake(
                                                            body->object_threadgroup_size.width,
                                                            body->object_threadgroup_size.height,
                                                            body->object_threadgroup_size.depth
                                                        )
                              threadsPerMeshThreadgroup:MTLSizeMake(
                                                            body->mesh_threadgroup_size.width,
                                                            body->mesh_threadgroup_size.height,
                                                            body->mesh_threadgroup_size.depth
                                                        )];
        break;
      }
      case WMTRenderCommandMemoryBarrier: {
        struct wmtcmd_render_memory_barrier *body = (struct wmtcmd_render_memory_barrier *)next;
        [encoder memoryBarrierWithScope:(MTLBarrierScope)body->scope
                            afterStages:(MTLRenderStages)body->stages_after
                           beforeStages:(MTLRenderStages)body->stages_before];
        break;
      }
      case WMTRenderCommandDXMTGeometryDraw: {
        struct wmtcmd_render_dxmt_geometry_draw *body = (struct wmtcmd_render_dxmt_geometry_draw *)next;
        [encoder setObjectBufferOffset:body->draw_arguments_offset atIndex:21];
        [encoder drawMeshThreadgroups:MTLSizeMake(body->warp_count, body->instance_count, 1)
            threadsPerObjectThreadgroup:MTLSizeMake(body->vertex_per_warp, 1, 1)
              threadsPerMeshThreadgroup:MTLSizeMake(1, 1, 1)];
        break;
      }
      case WMTRenderCommandDXMTGeometryDrawIndexed: {
        struct wmtcmd_render_dxmt_geometry_draw_indexed *body = (struct wmtcmd_render_dxmt_geometry_draw_indexed *)next;
        [encoder setObjectBuffer:(id<MTLBuffer>)body->index_buffer offset:body->index_buffer_offset atIndex:20];
        [encoder setObjectBufferOffset:body->draw_arguments_offset atIndex:21];
        [encoder drawMeshThreadgroups:MTLSizeMake(body->warp_count, body->instance_count, 1)
            threadsPerObjectThreadgroup:MTLSizeMake(body->vertex_per_warp, 1, 1)
              threadsPerMeshThreadgroup:MTLSizeMake(1, 1, 1)];
        break;
      }
      case WMTRenderCommandDXMTGeometryDrawIndirect: {
        struct wmtcmd_render_dxmt_geometry_draw_indirect *body =
            (struct wmtcmd_render_dxmt_geometry_draw_indirect *)next;
        [encoder setObjectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                          offset:body->indirect_args_offset
                         atIndex:21];
        [encoder drawMeshThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)body->dispatch_args_buffer
                                   indirectBufferOffset:body->dispatch_args_offset
                            threadsPerObjectThreadgroup:MTLSizeMake(body->vertex_per_warp, 1, 1)
                              threadsPerMeshThreadgroup:MTLSizeMake(1, 1, 1)];
        [encoder setObjectBuffer:(id<MTLBuffer>)body->imm_draw_arguments offset:0 atIndex:21];
        break;
      }
      case WMTRenderCommandDXMTGeometryDrawIndexedIndirect: {
        struct wmtcmd_render_dxmt_geometry_draw_indexed_indirect *body =
            (struct wmtcmd_render_dxmt_geometry_draw_indexed_indirect *)next;
        [encoder setObjectBuffer:(id<MTLBuffer>)body->index_buffer offset:body->index_buffer_offset atIndex:20];
        [encoder setObjectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                          offset:body->indirect_args_offset
                         atIndex:21];
        [encoder drawMeshThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)body->dispatch_args_buffer
                                   indirectBufferOffset:body->dispatch_args_offset
                            threadsPerObjectThreadgroup:MTLSizeMake(body->vertex_per_warp, 1, 1)
                              threadsPerMeshThreadgroup:MTLSizeMake(1, 1, 1)];
        [encoder setObjectBuffer:(id<MTLBuffer>)body->imm_draw_arguments offset:0 atIndex:21];
        break;
      }
      case WMTRenderCommandDXMTTessellationMeshDraw: {
        struct wmtcmd_render_dxmt_tessellation_mesh_draw *body =
            (struct wmtcmd_render_dxmt_tessellation_mesh_draw *)next;
        [encoder setObjectBufferOffset:body->draw_arguments_offset atIndex:21];
        [encoder drawMeshThreadgroups:MTLSizeMake(body->patch_per_mesh_instance, body->instance_count, 1)
            threadsPerObjectThreadgroup:MTLSizeMake(body->threads_per_patch, body->patch_per_group, 1)
              threadsPerMeshThreadgroup:MTLSizeMake(32, 1, 1)];
        break;
      }
      case WMTRenderCommandDXMTTessellationMeshDrawIndexed: {
        struct wmtcmd_render_dxmt_tessellation_mesh_draw_indexed *body =
            (struct wmtcmd_render_dxmt_tessellation_mesh_draw_indexed *)next;
        [encoder setObjectBuffer:(id<MTLBuffer>)body->index_buffer offset:body->index_buffer_offset atIndex:20];
        [encoder setObjectBufferOffset:body->draw_arguments_offset atIndex:21];
        [encoder drawMeshThreadgroups:MTLSizeMake(body->patch_per_mesh_instance, body->instance_count, 1)
            threadsPerObjectThreadgroup:MTLSizeMake(body->threads_per_patch, body->patch_per_group, 1)
              threadsPerMeshThreadgroup:MTLSizeMake(32, 1, 1)];
        break;
      }

      case WMTRenderCommandDXMTTessellationMeshDrawIndirect: {
        struct wmtcmd_render_dxmt_tessellation_mesh_draw_indirect *body =
            (struct wmtcmd_render_dxmt_tessellation_mesh_draw_indirect *)next;
        [encoder setObjectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                          offset:body->indirect_args_offset
                         atIndex:21];
        [encoder drawMeshThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)body->dispatch_args_buffer
                                   indirectBufferOffset:body->dispatch_args_offset
                            threadsPerObjectThreadgroup:MTLSizeMake(body->threads_per_patch, body->patch_per_group, 1)
                              threadsPerMeshThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder setObjectBuffer:(id<MTLBuffer>)body->imm_draw_arguments offset:0 atIndex:21];
        break;
      }
      case WMTRenderCommandDXMTTessellationMeshDrawIndexedIndirect: {
        struct wmtcmd_render_dxmt_tessellation_mesh_draw_indexed_indirect *body =
            (struct wmtcmd_render_dxmt_tessellation_mesh_draw_indexed_indirect *)next;
        [encoder setObjectBuffer:(id<MTLBuffer>)body->index_buffer offset:body->index_buffer_offset atIndex:20];
        [encoder setObjectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                          offset:body->indirect_args_offset
                         atIndex:21];
        [encoder drawMeshThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)body->dispatch_args_buffer
                                   indirectBufferOffset:body->dispatch_args_offset
                            threadsPerObjectThreadgroup:MTLSizeMake(body->threads_per_patch, body->patch_per_group, 1)
                              threadsPerMeshThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder setObjectBuffer:(id<MTLBuffer>)body->imm_draw_arguments offset:0 atIndex:21];
        break;
      }
      case WMTRenderCommandUpdateFence: {
        struct wmtcmd_render_fence_op *body = (struct wmtcmd_render_fence_op *)next;
        [encoder updateFence:(id<MTLFence>)body->fence afterStages:(MTLRenderStages)body->stages];
        break;
      }
      case WMTRenderCommandWaitForFence: {
        struct wmtcmd_render_fence_op *body = (struct wmtcmd_render_fence_op *)next;
        [encoder waitForFence:(id<MTLFence>)body->fence beforeStages:(MTLRenderStages)body->stages];
        break;
      }
      case WMTRenderCommandSetViewport: {
        struct wmtcmd_render_setviewport *body = (struct wmtcmd_render_setviewport *)next;
        union {
          struct WMTViewport src;
          MTLViewport dst;
        } u = {.src = body->viewport};
        [encoder setViewport:u.dst];
        break;
      }
      case WMTRenderCommandSetScissorRect: {
        struct wmtcmd_render_setscissorrect *body = (struct wmtcmd_render_setscissorrect *)next;
        union {
          struct WMTScissorRect src;
          MTLScissorRect dst;
        } u = {.src = body->scissor_rect};
        [encoder setScissorRect:u.dst];
        break;
      }
      case WMTRenderCommandDispatchThreadsPerTile: {
        struct wmtcmd_render_dispatch_threads_per_tile *body = (struct wmtcmd_render_dispatch_threads_per_tile *)next;
        [encoder dispatchThreadsPerTile:MTLSizeMake(body->width, body->height, 1)];
        break;
      }
      }
    } @catch(NSException * exception) {
      FILE *el = winemetal_critical_log();
      if (el) {
        const char *name = [[exception name] UTF8String];
        const char *reason = [[exception reason] UTF8String];
        fprintf(
            el, "render_cmd_exception index=%zu encoder=%p cmd=%p type=%u(%s) exception=%s reason=%s next=%p\n",
            command_index, encoder, next, next->type, winemetal_render_command_name(next->type), name ? name : "(null)",
            reason ? reason : "(null)", next->next.ptr
        );
        fclose(el);
      }
      if (dl) {
        fclose(dl);
      }
      winemetal_end_encoder_safely((id<MTLCommandEncoder>)encoder, "render_encode_exception");
      return STATUS_UNSUCCESSFUL;
    }
    next = next->next.ptr;
    command_index++;
  }
  if (dl) {
    fprintf(dl, "render_encode_end encoder=%p commands=%zu\n", encoder, command_index);
    fclose(dl);
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_pixelFormat(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLTexture>)params->handle pixelFormat];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_width(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLTexture>)params->handle width];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_height(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLTexture>)params->handle height];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_depth(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLTexture>)params->handle depth];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_arrayLength(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLTexture>)params->handle arrayLength];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_mipmapLevelCount(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLTexture>)params->handle mipmapLevelCount];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_replaceRegion(void *obj) {
  struct unixcall_mtltexture_replaceregion *params = obj;
  [(id<MTLTexture>)params->texture replaceRegion:MTLRegionMake3D(
                                                     params->origin.x, params->origin.y, params->origin.z,
                                                     params->size.width, params->size.height, params->size.depth
                                                 )
                                     mipmapLevel:params->level
                                           slice:params->slice
                                       withBytes:params->data.ptr
                                     bytesPerRow:params->bytes_per_row
                                   bytesPerImage:params->bytes_per_image];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLBuffer_didModifyRange(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  [(id<MTLBuffer>)params->handle didModifyRange:NSMakeRange(params->arg, params->ret)];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_presentDrawable(void *obj) {
  struct unixcall_generic_obj_obj_noret *params = obj;
  [(id<MTLCommandBuffer>)params->handle presentDrawable:(id<MTLDrawable>)params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_presentDrawableAfterMinimumDuration(void *obj) {
  struct unixcall_generic_obj_obj_double_noret *params = obj;
  [(id<MTLCommandBuffer>)params->handle presentDrawable:(id<MTLDrawable>)params->arg0
                                   afterMinimumDuration:params->arg1];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_supportsFamily(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle supportsFamily:(MTLGPUFamily)params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_supportsBCTextureCompression(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle supportsBCTextureCompression];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_supportsTextureSampleCount(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle supportsTextureSampleCount:params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_hasUnifiedMemory(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle hasUnifiedMemory];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCaptureManager_sharedCaptureManager(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  params->ret = (obj_handle_t)[MTLCaptureManager sharedCaptureManager];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCaptureManager_startCapture(void *obj) {
  struct unixcall_mtlcapturemanager_startcapture *params = obj;
  MTLCaptureDescriptor *desc = [[MTLCaptureDescriptor alloc] init];
  const struct WMTCaptureInfo *info = params->info.ptr;
  desc.destination = (MTLCaptureDestination)info->destination;
  desc.captureObject = (id)info->capture_object;
  NSString *path_str = [[NSString alloc] initWithCString:info->output_url.ptr encoding:NSUTF8StringEncoding];
  NSURL *url = [[NSURL alloc] initFileURLWithPath:path_str];
  desc.outputURL = url;
  [(MTLCaptureManager *)params->capture_manager startCaptureWithDescriptor:desc error:nil];
  [url release];
  [path_str release];
  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCaptureManager_stopCapture(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;
  [(MTLCaptureManager *)params->handle stopCapture];
  return STATUS_SUCCESS;
}

#include <signal.h>

void
temp_handler(int signum) {
  fprintf(stderr, "received signal %d in temp_handler(), and it may cause problem!\n", signum);
}

static const int SIGNALS[] = {
    SIGHUP,
    SIGINT,
    SIGTERM,
    SIGUSR2,
    SIGILL,
    SIGTRAP,
    SIGABRT,
    SIGFPE,
    SIGBUS,
    SIGSEGV,
    SIGQUIT
#ifdef SIGSYS
    ,
    SIGSYS
#endif
#ifdef SIGXCPU
    ,
    SIGXCPU
#endif
#ifdef SIGXFSZ
    ,
    SIGXFSZ
#endif
#ifdef SIGEMT
    ,
    SIGEMT
#endif
    ,
    SIGUSR1
#ifdef SIGINFO
    ,
    SIGINFO
#endif
};

static NTSTATUS
_MTLDevice_newTemporalScaler(void *obj) {
  struct unixcall_mtldevice_newfxtemporalscaler *params = obj;
  MTLFXTemporalScalerDescriptor *desc = [[MTLFXTemporalScalerDescriptor alloc] init];
  const struct WMTFXTemporalScalerInfo *info = params->info.ptr;
  desc.colorTextureFormat = to_metal_pixel_format(info->color_format);
  desc.outputTextureFormat = to_metal_pixel_format(info->output_format);
  desc.depthTextureFormat = to_metal_pixel_format(info->depth_format);
  desc.motionTextureFormat = to_metal_pixel_format(info->motion_format);
  desc.inputWidth = info->input_width;
  desc.inputHeight = info->input_height;
  desc.outputWidth = info->output_width;
  desc.outputHeight = info->output_height;
  desc.inputContentMaxScale = info->input_content_max_scale;
  desc.inputContentMinScale = info->input_content_min_scale;
  desc.inputContentPropertiesEnabled = info->input_content_properties_enabled;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
  if (@available(macOS 15, *)) {
    desc.requiresSynchronousInitialization = info->requires_synchronous_initialization;
  }
#endif
  desc.autoExposureEnabled = info->auto_exposure;

  struct sigaction old_action[sizeof(SIGNALS) / sizeof(int)], new_action;
  if (@available(macOS 16, *)) {
  } else {
    new_action.sa_handler = temp_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    for (unsigned int i = 0; i < sizeof(SIGNALS) / sizeof(int); i++)
      sigaction(SIGNALS[i], &new_action, &old_action[i]);
  }

  params->ret = (obj_handle_t)[desc newTemporalScalerWithDevice:(id<MTLDevice>)params->device];

  if (@available(macOS 16, *)) {
  } else {
    for (unsigned int i = 0; i < sizeof(SIGNALS) / sizeof(int); i++)
      sigaction(SIGNALS[i], &old_action[i], NULL);
  }

  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newSpatialScaler(void *obj) {
  struct unixcall_mtldevice_newfxspatialscaler *params = obj;
  MTLFXSpatialScalerDescriptor *desc = [[MTLFXSpatialScalerDescriptor alloc] init];
  const struct WMTFXSpatialScalerInfo *info = params->info.ptr;
  desc.colorTextureFormat = to_metal_pixel_format(info->color_format);
  desc.outputTextureFormat = to_metal_pixel_format(info->output_format);
  desc.inputWidth = info->input_width;
  desc.inputHeight = info->input_height;
  desc.outputWidth = info->output_width;
  desc.outputHeight = info->output_height;
  params->ret = (obj_handle_t)[desc newSpatialScalerWithDevice:(id<MTLDevice>)params->device];
  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_encodeTemporalScale(void *obj) {
  struct unixcall_mtlcommandbuffer_temporal_scale *params = obj;
  id<MTLCommandBuffer> cmdbuf = (id<MTLCommandBuffer>)params->cmdbuf;
  id<MTLFXTemporalScaler> scaler = (id<MTLFXTemporalScaler>)params->scaler;
  scaler.colorTexture = (id<MTLTexture>)params->color;
  scaler.outputTexture = (id<MTLTexture>)params->output;
  scaler.depthTexture = (id<MTLTexture>)params->depth;
  scaler.motionTexture = (id<MTLTexture>)params->motion;
  scaler.exposureTexture = (id<MTLTexture>)params->exposure;
  scaler.fence = (id<MTLFence>)params->fence;
  const struct WMTFXTemporalScalerProps *props = params->props.ptr;
  scaler.inputContentWidth = props->input_content_width;
  scaler.inputContentHeight = props->input_content_height;
  scaler.reset = props->reset;
  scaler.depthReversed = props->depth_reversed;
  scaler.motionVectorScaleX = props->motion_vector_scale_x;
  scaler.motionVectorScaleY = props->motion_vector_scale_y;
  scaler.jitterOffsetX = props->jitter_offset_x;
  scaler.jitterOffsetY = props->jitter_offset_y;
  scaler.preExposure = props->pre_exposure;
  [scaler encodeToCommandBuffer:cmdbuf];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_encodeSpatialScale(void *obj) {
  struct unixcall_mtlcommandbuffer_spatial_scale *params = obj;
  id<MTLCommandBuffer> cmdbuf = (id<MTLCommandBuffer>)params->cmdbuf;
  id<MTLFXSpatialScaler> scaler = (id<MTLFXSpatialScaler>)params->scaler;
  scaler.colorTexture = (id<MTLTexture>)params->color;
  scaler.outputTexture = (id<MTLTexture>)params->output;
  scaler.fence = (id<MTLFence>)params->fence;
  [scaler encodeToCommandBuffer:cmdbuf];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSString_string(void *obj) {
  struct unixcall_nsstring_string *params = obj;
  NSString *str = [NSString stringWithCString:params->buffer_ptr.ptr encoding:(NSStringEncoding)params->encoding];
  params->ret = (obj_handle_t)str;
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSString_alloc_init(void *obj) {
  struct unixcall_nsstring_string *params = obj;
  NSString *str = [[NSString alloc] initWithCString:params->buffer_ptr.ptr encoding:(NSStringEncoding)params->encoding];
  params->ret = (obj_handle_t)str;
  return STATUS_SUCCESS;
}

static NTSTATUS
_DeveloperHUDProperties_instance(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  params->ret =
      (obj_handle_t)((id (*)(id, SEL))objc_msgSend)(objc_lookUpClass("_CADeveloperHUDProperties"), @selector(instance));
  return STATUS_SUCCESS;
}

static NTSTATUS
_DeveloperHUDProperties_addLabel(void *obj) {
  struct unixcall_generic_obj_obj_obj_uint64_ret *params = obj;
  params->ret = ((bool (*)(id, SEL, id, id))objc_msgSend)(
      (id)params->handle, @selector(addLabel:after:), (id)params->arg0, (id)params->arg1
  );
  return STATUS_SUCCESS;
}

static NTSTATUS
_DeveloperHUDProperties_updateLabel(void *obj) {
  struct unixcall_generic_obj_obj_obj_noret *params = obj;
  ((void (*)(id, SEL, id, id))objc_msgSend)(
      (id)params->handle, @selector(updateLabel:value:), (id)params->arg0, (id)params->arg1
  );
  return STATUS_SUCCESS;
}

static NTSTATUS
_DeveloperHUDProperties_remove(void *obj) {
  struct unixcall_generic_obj_obj_noret *params = obj;
  ((void (*)(id, SEL, id))objc_msgSend)((id)params->handle, @selector(remove:), (id)params->arg);
  return STATUS_SUCCESS;
}

static NTSTATUS
_MetalDrawable_texture(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<CAMetalDrawable>)params->handle texture];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MetalLayer_nextDrawable(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(CAMetalLayer *)params->handle nextDrawable];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_supportsFXSpatialScaler(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [MTLFXSpatialScalerDescriptor supportsDevice:(id<MTLDevice>)params->handle];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_supportsFXTemporalScaler(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [MTLFXTemporalScalerDescriptor supportsDevice:(id<MTLDevice>)params->handle];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MetalLayer_setProps(void *obj) {
  struct unixcall_generic_obj_constptr_noret *params = obj;
  CAMetalLayer *layer = (CAMetalLayer *)params->handle;
  const struct WMTLayerProps *props = params->arg.ptr;
  execute_on_main(^{
    layer.device = (id<MTLDevice>)props->device;
    layer.opaque = props->opaque;
    layer.framebufferOnly = props->framebuffer_only;
    layer.contentsScale = props->contents_scale;
    layer.displaySyncEnabled = props->display_sync_enabled;
    layer.drawableSize = CGSizeMake(props->drawable_width, props->drawable_height);
    layer.pixelFormat = to_metal_pixel_format(props->pixel_format);
  });
  return STATUS_SUCCESS;
}

static NTSTATUS
_MetalLayer_getProps(void *obj) {
  struct unixcall_generic_obj_ptr_noret *params = obj;
  CAMetalLayer *layer = (CAMetalLayer *)params->handle;
  struct WMTLayerProps *props = params->arg.ptr;
  props->device = (obj_handle_t)layer.device;
  props->opaque = layer.opaque;
  props->framebuffer_only = layer.framebufferOnly;
  props->contents_scale = layer.contentsScale;
  props->display_sync_enabled = layer.displaySyncEnabled;
  props->drawable_height = layer.drawableSize.height;
  props->drawable_width = layer.drawableSize.width;
  props->pixel_format = layer.pixelFormat;
  return STATUS_SUCCESS;
}

typedef struct macdrv_opaque_metal_device *macdrv_metal_device;
typedef struct macdrv_opaque_metal_view *macdrv_metal_view;
typedef struct macdrv_opaque_metal_layer *macdrv_metal_layer;
typedef struct macdrv_opaque_view *macdrv_view;
typedef struct macdrv_opaque_window *macdrv_window;
typedef struct macdrv_opaque_window_data *macdrv_window_data;
typedef struct opaque_window_surface *window_surface;
typedef struct opaque_HWND *HWND;
typedef struct macdrv_client_surface *macdrv_client_surface;
struct macdrv_win_data {
  HWND hwnd; /* hwnd that this private data belongs to */
  macdrv_window cocoa_window;
  macdrv_view client_view;
};

struct macdrv_functions_t {
  void (*macdrv_init_display_devices)(BOOL);
  struct macdrv_win_data *(*get_win_data)(HWND hwnd);
  void (*release_win_data)(struct macdrv_win_data *data);
  macdrv_window (*macdrv_get_cocoa_window)(HWND hwnd, BOOL require_on_screen);
  macdrv_metal_device (*macdrv_create_metal_device)(void);
  void (*macdrv_release_metal_device)(macdrv_metal_device d);
  macdrv_metal_view (*macdrv_view_create_metal_view)(macdrv_view v, macdrv_metal_device d);
  macdrv_metal_layer (*macdrv_view_get_metal_layer)(macdrv_metal_view v);
  void (*macdrv_view_release_metal_view)(macdrv_metal_view v);
  void (*on_main_thread)(dispatch_block_t b);
};

static NTSTATUS
_CreateMetalViewFromHWND(void *obj) {
  struct unixcall_create_metal_view_from_hwnd *params = obj;
  FILE *dl = winemetal_debug_log();

  struct macdrv_win_data *(*pfn_get_win_data)(HWND hwnd) = NULL;
  void (*pfn_release_win_data)(struct macdrv_win_data *data) = NULL;
  macdrv_window (*pfn_macdrv_get_cocoa_window)(HWND hwnd, BOOL require_on_screen) = NULL;
  macdrv_client_surface (*pfn_macdrv_client_surface_create)(HWND hwnd) = NULL;
  macdrv_metal_view (*pfn_macdrv_view_create_metal_view)(macdrv_view v, macdrv_metal_device d) = NULL;
  macdrv_metal_layer (*pfn_macdrv_view_get_metal_layer)(macdrv_metal_view v) = NULL;

  struct macdrv_functions_t *macdrv_functions;
  if ((macdrv_functions = dlsym(RTLD_DEFAULT, "macdrv_functions"))) {
    pfn_get_win_data = macdrv_functions->get_win_data;
    pfn_release_win_data = macdrv_functions->release_win_data;
    pfn_macdrv_get_cocoa_window = macdrv_functions->macdrv_get_cocoa_window;
    pfn_macdrv_client_surface_create = dlsym(RTLD_DEFAULT, "macdrv_client_surface_create");
    pfn_macdrv_view_create_metal_view = macdrv_functions->macdrv_view_create_metal_view;
    pfn_macdrv_view_get_metal_layer = macdrv_functions->macdrv_view_get_metal_layer;
    if (dl) {
      fprintf(
          dl, "[winemetal] CreateMetalViewFromHWND hwnd=%p device=%p via macdrv_functions=%p\n", (void *)params->hwnd,
          (void *)params->device, macdrv_functions
      );
    }
  } else {
    pfn_get_win_data = dlsym(RTLD_DEFAULT, "get_win_data");
    pfn_release_win_data = dlsym(RTLD_DEFAULT, "release_win_data");
    pfn_macdrv_get_cocoa_window = dlsym(RTLD_DEFAULT, "macdrv_get_cocoa_window");
    pfn_macdrv_client_surface_create = dlsym(RTLD_DEFAULT, "macdrv_client_surface_create");
    pfn_macdrv_view_create_metal_view = dlsym(RTLD_DEFAULT, "macdrv_view_create_metal_view");
    pfn_macdrv_view_get_metal_layer = dlsym(RTLD_DEFAULT, "macdrv_view_get_metal_layer");
    if (dl) {
      fprintf(
          dl, "[winemetal] CreateMetalViewFromHWND hwnd=%p device=%p via RTLD_DEFAULT symbols\n", (void *)params->hwnd,
          (void *)params->device
      );
    }
  }

  params->ret_view = 0;
  params->ret_layer = 0;

  if (!pfn_get_win_data || !pfn_release_win_data || !pfn_macdrv_view_create_metal_view ||
      !pfn_macdrv_view_get_metal_layer) {
    if (dl) {
      fprintf(
          dl, "[winemetal] CreateMetalViewFromHWND missing bridge get=%p release=%p create=%p layer=%p\n",
          pfn_get_win_data, pfn_release_win_data, pfn_macdrv_view_create_metal_view, pfn_macdrv_view_get_metal_layer
      );
      fclose(dl);
    }
    return STATUS_SUCCESS;
  }

  struct macdrv_win_data *win_data = pfn_get_win_data((HWND)params->hwnd);
  macdrv_window fallback_cocoa_window = NULL;
  if (!win_data && pfn_macdrv_get_cocoa_window) {
    fallback_cocoa_window = pfn_macdrv_get_cocoa_window((HWND)params->hwnd, TRUE);
    if (dl) {
      fprintf(
          dl, "[winemetal] CreateMetalViewFromHWND get_win_data NULL, direct cocoa_window=%p\n", fallback_cocoa_window
      );
    }
  }
  if (!win_data && !fallback_cocoa_window) {
    if (!dl)
      dl = winemetal_critical_log();
    if (dl) {
      fprintf(dl, "[winemetal] CreateMetalViewFromHWND get_win_data returned NULL and no cocoa fallback\n");
      fclose(dl);
    }
    return STATUS_SUCCESS;
  }

  if (dl && win_data) {
    fprintf(
        dl, "[winemetal] CreateMetalViewFromHWND win_data=%p cocoa_window=%p client_view=%p\n", win_data,
        win_data->cocoa_window, win_data->client_view
    );
  }

  if (win_data && !win_data->client_view && pfn_macdrv_client_surface_create) {
    pfn_release_win_data(win_data);
    win_data = NULL;
    macdrv_client_surface surface = pfn_macdrv_client_surface_create((HWND)params->hwnd);
    if (dl) {
      fprintf(dl, "[winemetal] CreateMetalViewFromHWND created client_surface=%p\n", surface);
    }
    win_data = pfn_get_win_data((HWND)params->hwnd);
    if (!win_data) {
      if (dl) {
        fprintf(dl, "[winemetal] CreateMetalViewFromHWND get_win_data after client surface returned NULL\n");
        fclose(dl);
      }
      return STATUS_SUCCESS;
    }
    if (dl) {
      fprintf(dl, "[winemetal] CreateMetalViewFromHWND after client surface client_view=%p\n", win_data->client_view);
    }
  }

  macdrv_window cocoa_window_handle = win_data ? win_data->cocoa_window : fallback_cocoa_window;
  if (!cocoa_window_handle && pfn_macdrv_get_cocoa_window) {
    cocoa_window_handle = pfn_macdrv_get_cocoa_window((HWND)params->hwnd, TRUE);
    if (dl) {
      fprintf(dl, "[winemetal] CreateMetalViewFromHWND late cocoa fallback=%p\n", cocoa_window_handle);
    }
  }

  macdrv_view target_view = win_data ? win_data->client_view : NULL;
  if (!target_view && cocoa_window_handle) {
    id cocoa_window = (__bridge id)cocoa_window_handle;
    if ([cocoa_window respondsToSelector:@selector(contentView)]) {
      id content_view = [cocoa_window contentView];
      if (content_view) {
        target_view = (macdrv_view)(__bridge void *)content_view;
        if (dl) {
          fprintf(dl, "[winemetal] CreateMetalViewFromHWND using cocoa contentView=%p fallback\n", target_view);
        }
      }
    }
  }

  if (target_view) {
    macdrv_metal_view view = pfn_macdrv_view_create_metal_view(target_view, (macdrv_metal_device)params->device);
    params->ret_view = (obj_handle_t)view;
    if (view) {
      params->ret_layer = (obj_handle_t)pfn_macdrv_view_get_metal_layer(view);
    }
    if (dl) {
      fprintf(dl, "[winemetal] CreateMetalViewFromHWND view=%p layer=%p\n", view, (void *)params->ret_layer);
    } else {
      if (!dl)
        dl = winemetal_critical_log();
      if (dl) {
        fprintf(
            dl, "[winemetal] CreateMetalViewFromHWND create_metal_view failed for target_view=%p cocoa_window=%p\n",
            target_view, cocoa_window_handle
        );
      }
    }
  } else {
    if (!dl)
      dl = winemetal_critical_log();
    if (dl) {
      fprintf(
          dl, "[winemetal] CreateMetalViewFromHWND client_view/contentView unavailable cocoa_window=%p\n",
          cocoa_window_handle
      );
    }
  }

  if (win_data)
    pfn_release_win_data(win_data);
  if (dl)
    fclose(dl);
  return STATUS_SUCCESS;
}

static NTSTATUS
_ReleaseMetalView(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;

  void (*pfn_macdrv_view_release_metal_view)(macdrv_metal_view v) = NULL;

  struct macdrv_functions_t *macdrv_functions;
  if ((macdrv_functions = dlsym(RTLD_DEFAULT, "macdrv_functions"))) {
    pfn_macdrv_view_release_metal_view = macdrv_functions->macdrv_view_release_metal_view;
  } else {
    pfn_macdrv_view_release_metal_view = dlsym(RTLD_DEFAULT, "macdrv_view_release_metal_view");
  }

  if (pfn_macdrv_view_release_metal_view)
    pfn_macdrv_view_release_metal_view((macdrv_metal_view)params->handle);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50Initialize(void *args) {
  struct sm50_initialize_params *params = args;

  params->ret =
      SM50Initialize(params->bytecode, params->bytecode_size, params->shader, params->reflection, params->error);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50Destroy(void *args) {
  struct sm50_destroy_params *params = args;

  SM50Destroy(params->shader);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50Compile(void *args) {
  struct sm50_compile_params *params = args;

  params->ret = SM50Compile(params->shader, params->args, params->func_name, params->bitcode, params->error);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50GetCompiledBitcode(void *args) {
  struct sm50_get_compiled_bitcode_params *params = args;

  SM50GetCompiledBitcode(params->bitcode, params->data_out);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50DestroyBitcode(void *args) {
  struct sm50_destroy_bitcode_params *params = args;

  SM50DestroyBitcode(params->bitcode);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50GetErrorMessage(void *args) {
  struct sm50_get_error_message_params *params = args;

  params->ret_size = SM50GetErrorMessage(params->error, params->buffer, params->buffer_size);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50FreeError(void *args) {
  struct sm50_free_error_params *params = args;

  SM50FreeError(params->error);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50CompileTessellationPipelineHull(void *args) {
  struct sm50_compile_tessellation_pipeline_hull_params *params = args;

  params->ret = SM50CompileTessellationPipelineHull(
      params->vertex, params->hull, params->hull_args, params->func_name, params->bitcode, params->error
  );

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50CompileTessellationPipelineDomain(void *args) {
  struct sm50_compile_tessellation_pipeline_domain_params *params = args;

  params->ret = SM50CompileTessellationPipelineDomain(
      params->hull, params->domain, params->domain_args, params->func_name, params->bitcode, params->error
  );

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50CompileGeometryPipelineVertex(void *args) {
  struct sm50_compile_geometry_pipeline_vertex_params *params = args;

  params->ret = SM50CompileGeometryPipelineVertex(
      params->vertex, params->geometry, params->vertex_args, params->func_name, params->bitcode, params->error
  );

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50CompileGeometryPipelineGeometry(void *args) {
  struct sm50_compile_geometry_pipeline_geometry_params *params = args;

  params->ret = SM50CompileGeometryPipelineGeometry(
      params->vertex, params->geometry, params->geometry_args, params->func_name, params->bitcode, params->error
  );

  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandEncoder_setLabel(void *args) {
  struct unixcall_generic_obj_obj_noret *params = args;
  [(id<MTLCommandEncoder>)params->handle setLabel:(NSString *)params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_setShouldMaximizeConcurrentCompilation(void *args) {
  struct unixcall_generic_obj_uint64_noret *params = args;
  [(id<MTLDevice>)params->handle setShouldMaximizeConcurrentCompilation:(BOOL)params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50GetArgumentsInfo(void *args) {
  struct sm50_get_arguments_info_params *params = args;
  SM50GetArgumentsInfo(params->shader, params->constant_buffers, params->arguments);
  return STATUS_SUCCESS;
}

static inline void *
UInt32ToPtr(uint32_t v) {
  return (void *)(uint64_t)v;
}

#ifndef DXMT_NATIVE

static NTSTATUS
thunk32_SM50Initialize(void *args) {
  struct sm50_initialize_params32 *params = args;

  params->ret = SM50Initialize(
      UInt32ToPtr(params->bytecode), params->bytecode_size, UInt32ToPtr(params->shader),
      UInt32ToPtr(params->reflection), UInt32ToPtr(params->error)
  );

  return STATUS_SUCCESS;
}

struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  uint32_t num_output_slots;
  uint32_t num_elements;
  uint32_t strides[4];
  uint32_t elements;
};

struct SM50_SHADER_COMMON_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  enum SM50_SHADER_METAL_VERSION metal_version;
  enum SM50_SHADER_FLAG flag;
};

struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
};

struct SM50_SHADER_IA_INPUT_LAYOUT_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  enum SM50_INDEX_BUFFER_FORMAT index_buffer_format;
  uint32_t slot_mask;
  uint32_t num_elements;
  uint32_t elements;
};

struct SM50_SHADER_PSO_PIXEL_SHADER_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  uint32_t sample_mask;
  bool dual_source_blending;
  bool disable_depth_output;
  uint32_t unorm_output_reg_mask;
};

struct SM50_SHADER_GS_PASS_THROUGH_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  union {
    struct MTL_GEOMETRY_SHADER_PASS_THROUGH Data;
    uint32_t DataEncoded;
  };
  bool RasterizationDisabled;
};

struct SM50_SHADER_PSO_GEOMETRY_SHADER_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  bool strip_topology;
};

struct SM50_SHADER_PSO_TESSELLATOR_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  uint32_t max_potential_tess_factor;
};

void
sm50_compilation_argument32_convert(
    struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *first_arg, struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 *args32
) {
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *last_arg = first_arg;

  first_arg->type = SM50_SHADER_ARGUMENT_TYPE_MAX;
  first_arg->next = NULL;

  while (args32) {
    switch (args32->type) {
    case SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT: {
      struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA32 *src = (void *)args32;
      struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *data =
          malloc(sizeof(struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->num_output_slots = src->num_output_slots;
      data->num_elements = src->num_elements;
      data->strides[0] = src->strides[0];
      data->strides[1] = src->strides[1];
      data->strides[2] = src->strides[2];
      data->strides[3] = src->strides[3];
      data->elements = UInt32ToPtr(src->elements);
      break;
    }
    case SM50_SHADER_COMMON: {
      struct SM50_SHADER_COMMON_DATA32 *src = (void *)args32;
      struct SM50_SHADER_COMMON_DATA *data = malloc(sizeof(struct SM50_SHADER_COMMON_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->metal_version = src->metal_version;
      data->flags = src->flag;
      break;
    }
    case SM50_SHADER_PSO_PIXEL_SHADER: {
      struct SM50_SHADER_PSO_PIXEL_SHADER_DATA32 *src = (void *)args32;
      struct SM50_SHADER_PSO_PIXEL_SHADER_DATA *data = malloc(sizeof(struct SM50_SHADER_PSO_PIXEL_SHADER_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->unorm_output_reg_mask = src->unorm_output_reg_mask;
      data->disable_depth_output = src->disable_depth_output;
      data->sample_mask = src->sample_mask;
      data->dual_source_blending = src->dual_source_blending;
      break;
    }
    case SM50_SHADER_IA_INPUT_LAYOUT: {
      struct SM50_SHADER_IA_INPUT_LAYOUT_DATA32 *src = (void *)args32;
      struct SM50_SHADER_IA_INPUT_LAYOUT_DATA *data = malloc(sizeof(struct SM50_SHADER_IA_INPUT_LAYOUT_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->slot_mask = src->slot_mask;
      data->index_buffer_format = src->index_buffer_format;
      data->num_elements = src->num_elements;
      data->elements = UInt32ToPtr(src->elements);
      break;
    }
    case SM50_SHADER_GS_PASS_THROUGH: {
      struct SM50_SHADER_GS_PASS_THROUGH_DATA32 *src = (void *)args32;
      struct SM50_SHADER_GS_PASS_THROUGH_DATA *data = malloc(sizeof(struct SM50_SHADER_GS_PASS_THROUGH_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->Data = src->Data;
      data->RasterizationDisabled = src->RasterizationDisabled;
      break;
    }
    case SM50_SHADER_PSO_GEOMETRY_SHADER: {
      struct SM50_SHADER_PSO_GEOMETRY_SHADER_DATA32 *src = (void *)args32;
      struct SM50_SHADER_PSO_GEOMETRY_SHADER_DATA *data = malloc(sizeof(struct SM50_SHADER_PSO_GEOMETRY_SHADER_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->strip_topology = src->strip_topology;
      break;
    }
    case SM50_SHADER_PSO_TESSELLATOR: {
      struct SM50_SHADER_PSO_TESSELLATOR_DATA32 *src = (void *)args32;
      struct SM50_SHADER_PSO_TESSELLATOR_DATA *data = malloc(sizeof(struct SM50_SHADER_PSO_TESSELLATOR_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->max_potential_tess_factor = src->max_potential_tess_factor;
      break;
    }
    case SM50_SHADER_ARGUMENT_TYPE_MAX:
      break;
    }
    args32 = UInt32ToPtr(args32->next);
  }
}

void
sm50_compilation_argument32_free(struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *first_arg) {
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *arg = first_arg->next;

  while (arg) {
    struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *next = arg->next;
    free(arg);
    arg = next;
  }
}

static NTSTATUS
thunk32_SM50Compile(void *args) {
  struct sm50_compile_params32 *params = args;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA first_arg;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 *args32 = UInt32ToPtr(params->args);
  sm50_compilation_argument32_convert(&first_arg, args32);

  params->ret = SM50Compile(
      params->shader, &first_arg, UInt32ToPtr(params->func_name), UInt32ToPtr(params->bitcode),
      UInt32ToPtr(params->error)
  );

  sm50_compilation_argument32_free(&first_arg);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50GetCompiledBitcode(void *args) {
  struct sm50_get_compiled_bitcode_params32 *params = args;

  SM50GetCompiledBitcode(params->bitcode, UInt32ToPtr(params->data_out));

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50GetErrorMessage(void *args) {
  struct sm50_get_error_message_params32 *params = args;

  params->ret_size = SM50GetErrorMessage(params->error, UInt32ToPtr(params->buffer), params->buffer_size);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50CompileTessellationPipelineHull(void *args) {
  struct sm50_compile_tessellation_pipeline_hull_params32 *params = args;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA first_arg;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 *args32 = UInt32ToPtr(params->hull_args);
  sm50_compilation_argument32_convert(&first_arg, args32);

  params->ret = SM50CompileTessellationPipelineHull(
      params->vertex, params->hull, &first_arg, UInt32ToPtr(params->func_name), UInt32ToPtr(params->bitcode),
      UInt32ToPtr(params->error)
  );

  sm50_compilation_argument32_free(&first_arg);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50CompileTessellationPipelineDomain(void *args) {
  struct sm50_compile_tessellation_pipeline_domain_params32 *params = args;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA first_arg;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 *args32 = UInt32ToPtr(params->domain_args);
  sm50_compilation_argument32_convert(&first_arg, args32);

  params->ret = SM50CompileTessellationPipelineDomain(
      params->hull, params->domain, &first_arg, UInt32ToPtr(params->func_name), UInt32ToPtr(params->bitcode),
      UInt32ToPtr(params->error)
  );

  sm50_compilation_argument32_free(&first_arg);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50CompileGeometryPipelineVertex(void *args) {
  struct sm50_compile_geometry_pipeline_vertex_params32 *params = args;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA first_arg;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 *args32 = UInt32ToPtr(params->vertex_args);
  sm50_compilation_argument32_convert(&first_arg, args32);

  params->ret = SM50CompileGeometryPipelineVertex(
      params->vertex, params->geometry, &first_arg, UInt32ToPtr(params->func_name), UInt32ToPtr(params->bitcode),
      UInt32ToPtr(params->error)
  );

  sm50_compilation_argument32_free(&first_arg);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50CompileGeometryPipelineGeometry(void *args) {
  struct sm50_compile_geometry_pipeline_geometry_params32 *params = args;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA first_arg;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 *args32 = UInt32ToPtr(params->geometry_args);
  sm50_compilation_argument32_convert(&first_arg, args32);

  params->ret = SM50CompileGeometryPipelineGeometry(
      params->vertex, params->geometry, &first_arg, UInt32ToPtr(params->func_name), UInt32ToPtr(params->bitcode),
      UInt32ToPtr(params->error)
  );

  sm50_compilation_argument32_free(&first_arg);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50GetArgumentsInfo(void *args) {
  struct sm50_get_arguments_info_params32 *params = args;

  SM50GetArgumentsInfo(params->shader, UInt32ToPtr(params->constant_buffers), UInt32ToPtr(params->arguments));

  return STATUS_SUCCESS;
}
#endif /* DXMT_NATIVE */

static NTSTATUS
_MTLCommandBuffer_error(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLCommandBuffer>)params->handle error];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_logs(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLCommandBuffer>)params->handle logs];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLLogContainer_enumerate(void *obj) {
  struct unixcall_enumerate *params = obj;
  uint64_t count = 0;
  uint64_t read = 0;
  id *buffer = params->buffer.ptr;
  for (id _ in (id<MTLLogContainer>)params->enumerable) {
    if (count >= params->start) {
      if (count < params->start + params->buffer_size) {
        buffer[count - params->start] = _;
        read++;
      } else {
        break;
      }
    }
    count++;
  }
  params->ret_read = read;
  return STATUS_SUCCESS;
}

CFStringRef
GetColorSpaceName(enum WMTColorSpace colorspace) {
  switch (colorspace) {
  case WMTColorSpaceSRGB:
    return kCGColorSpaceSRGB;
  case WMTColorSpaceSRGBLinear:
  case WMTColorSpaceHDR_scRGB:
    return kCGColorSpaceExtendedLinearSRGB;
  case WMTColorSpaceBT2020:
    return kCGColorSpaceITUR_2020_sRGBGamma;
  case WMTColorSpaceHDR_PQ:
    return kCGColorSpaceITUR_2100_PQ;
  default:
    return nil;
  }
}

static NTSTATUS
_CGColorSpace_checkColorSpaceSupported(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = false;
  CFStringRef name = GetColorSpaceName((enum WMTColorSpace)params->handle);
  if (!name)
    return STATUS_SUCCESS;
  CGColorSpaceRef ref = CGColorSpaceCreateWithName(name);
  if (!ref)
    return STATUS_SUCCESS;
  CGColorSpaceRelease(ref);
  params->ret = true;
  return STATUS_SUCCESS;
}

static NTSTATUS
_MetalLayer_setColorSpace(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  CAMetalLayer *layer = (CAMetalLayer *)params->handle;
  enum WMTColorSpace colorspace = params->arg;
  CFStringRef name = GetColorSpaceName(colorspace);
  params->ret = false;
  if (!name)
    return STATUS_SUCCESS;
  CGColorSpaceRef ref = CGColorSpaceCreateWithName(name);
  if (!ref)
    return STATUS_SUCCESS;
  execute_on_main(^{
    layer.colorspace = ref;
    layer.wantsExtendedDynamicRangeContent = WMT_COLORSPACE_IS_HDR(colorspace);
    CGColorSpaceRelease(ref);
  });
  params->ret = true;
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTGetPrimaryDisplayId(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  params->ret = CGMainDisplayID();
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTGetSecondaryDisplayId(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  params->ret = kCGNullDirectDisplay;

  uint32_t count = 0;
  CGGetOnlineDisplayList(0, NULL, &count);

  if (count == 0)
    return STATUS_SUCCESS;

  CGDirectDisplayID main_display = CGMainDisplayID();
  CGDirectDisplayID displays[count];
  CGGetOnlineDisplayList(count, displays, &count);

  for (uint32_t i = 0; i < count; i++) {
    CGDirectDisplayID id = displays[i];
    if (id == main_display)
      continue;
    if (CGDisplayMirrorsDisplay(id) != kCGNullDirectDisplay)
      continue;
    params->ret = id;
    break;
  }

  return STATUS_SUCCESS;
}

typedef struct icc_XYZ_t {
  uint32_t sig;      // 0x205a5958
  uint32_t reserved; // 0
  int32_t x;
  int32_t y;
  int32_t z;
} icc_XYZ_t;

bool
GetChromaticity_xy(ColorSyncProfileRef profile, CFStringRef tag, float *out_x, float *out_y) {
  CFDataRef tag_data = ColorSyncProfileCopyTag(profile, tag);
  if (!tag_data)
    return false;
  if (CFDataGetLength(tag_data) != sizeof(icc_XYZ_t))
    return false;
  icc_XYZ_t *data = (icc_XYZ_t *)CFDataGetBytePtr(tag_data);
  if (data->sig != 0x205a5958)
    return false;
  double X = (int32_t)__builtin_bswap32(data->x) / 65536.0;
  double Y = (int32_t)__builtin_bswap32(data->y) / 65536.0;
  double Z = (int32_t)__builtin_bswap32(data->z) / 65536.0;
  *out_x = X / (X + Y + Z);
  *out_y = Y / (X + Y + Z);
  return true;
}

bool
GetDisplayColorGamut(ColorSyncProfileRef profile, struct WMTDisplayDescription *desc_out) {
  return GetChromaticity_xy(
             profile, kColorSyncSigMediaWhitePointTag, &desc_out->white_points[0], &desc_out->white_points[1]
         ) &&
         GetChromaticity_xy(
             profile, kColorSyncSigRedColorantTag, &desc_out->red_primaries[0], &desc_out->red_primaries[1]
         ) &&
         GetChromaticity_xy(
             profile, kColorSyncSigGreenColorantTag, &desc_out->green_primaries[0], &desc_out->green_primaries[1]
         ) &&
         GetChromaticity_xy(
             profile, kColorSyncSigBlueColorantTag, &desc_out->blue_primaries[0], &desc_out->blue_primaries[1]
         );
}

NSScreen *
GetNSScreenForDisplayID(CGDirectDisplayID display_id) {
  for (NSScreen *screen in [NSScreen screens]) {
    CGDirectDisplayID id = [[[screen deviceDescription] objectForKey:@"NSScreenNumber"] unsignedIntValue];
    if (id == display_id) {
      return screen;
    }
  }
  return nil;
}

static NTSTATUS
_WMTGetDisplayDescription(void *obj) {
  struct unixcall_generic_obj_ptr_noret *params = obj;
  CGDirectDisplayID display_id = params->handle;
  struct WMTDisplayDescription *desc_out = params->arg.ptr;
  ColorSyncProfileRef profile = ColorSyncProfileCreateWithDisplayID(display_id);
  if (!profile || !GetDisplayColorGamut(profile, desc_out))
    GetDisplayColorGamut(ColorSyncProfileCreateWithName(kColorSyncGenericRGBProfile), desc_out);
  NSScreen *screen = GetNSScreenForDisplayID(display_id);
  if (screen) {
    desc_out->maximum_edr_color_component_value = [screen maximumExtendedDynamicRangeColorComponentValue];
    desc_out->maximum_reference_edr_color_component_value =
        [screen maximumReferenceExtendedDynamicRangeColorComponentValue];
    desc_out->maximum_potential_edr_color_component_value =
        [screen maximumPotentialExtendedDynamicRangeColorComponentValue];
  } else {
    desc_out->maximum_edr_color_component_value = 1.0;
    desc_out->maximum_reference_edr_color_component_value = 0.0;
    desc_out->maximum_potential_edr_color_component_value = 1.0;
  }
  return STATUS_SUCCESS;
}

struct DisplaySetting {
  uint64_t version;
  enum WMTColorSpace colorspace;
  struct WMTHDRMetadata hdr_metadata;
};

struct DisplaySetting g_display_settings[2] = {{0, 0, {}}, {0, 0, {}}};

static NTSTATUS
_MetalLayer_getEDRValue(void *obj) {
  struct unixcall_generic_obj_ptr_noret *params = obj;
  CAMetalLayer *layer = (CAMetalLayer *)params->handle;
  struct WMTEDRValue *value = params->arg.ptr;
  value->maximum_edr_color_component_value = 1.0;
  value->maximum_potential_edr_color_component_value = 1.0;

  if (![layer.delegate isKindOfClass:NSView.class])
    return STATUS_SUCCESS;

  NSView *view = (NSView *)layer.delegate;
  if (!view.window)
    return STATUS_SUCCESS;

  if (!view.window.screen)
    return STATUS_SUCCESS;

  NSScreen *screen = view.window.screen;

  value->maximum_edr_color_component_value =
      layer.wantsExtendedDynamicRangeContent ? screen.maximumExtendedDynamicRangeColorComponentValue : 1.0;
  value->maximum_potential_edr_color_component_value = screen.maximumPotentialExtendedDynamicRangeColorComponentValue;

  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLLibrary_newFunctionWithConstants(void *obj) {
  struct unixcall_mtllibrary_newfunction_with_constants *params = obj;
  id<MTLLibrary> library = (id<MTLLibrary>)params->library;
  NSString *name = [[NSString alloc] initWithCString:(char *)params->name.ptr encoding:NSUTF8StringEncoding];
  struct WMTFunctionConstant *constants = (struct WMTFunctionConstant *)params->constants.ptr;
  NSError *err = NULL;
  MTLFunctionConstantValues *values = [[MTLFunctionConstantValues alloc] init];
  for (uint64_t i = 0; i < params->num_constants; i++)
    [values setConstantValue:constants[i].data.ptr type:(MTLDataType)constants[i].type atIndex:constants[i].index];

  params->ret = (obj_handle_t)[library newFunctionWithName:name constantValues:values error:&err];
  params->ret_error = (obj_handle_t)err;
  [name release];
  [values release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLLibrary_newFunctionWithDescriptor(void *obj) {
  struct unixcall_mtllibrary_newfunction_with_descriptor *params = obj;
  id<MTLLibrary> library = (id<MTLLibrary>)params->library;
  NSString *name = [[NSString alloc] initWithCString:(char *)params->name.ptr encoding:NSUTF8StringEncoding];
  NSString *specialized_name = nil;
  if (params->specialized_name.ptr) {
    specialized_name =
        [[NSString alloc] initWithCString:(char *)params->specialized_name.ptr encoding:NSUTF8StringEncoding];
  }

  MTLFunctionDescriptor *descriptor = [MTLFunctionDescriptor functionDescriptor];
  descriptor.name = name;
  descriptor.specializedName = specialized_name;
  descriptor.options = (MTLFunctionOptions)params->options;

  struct WMTFunctionConstant *constants = (struct WMTFunctionConstant *)params->constants.ptr;
  if (constants && params->num_constants) {
    MTLFunctionConstantValues *values = [[MTLFunctionConstantValues alloc] init];
    for (uint64_t i = 0; i < params->num_constants; i++)
      [values setConstantValue:constants[i].data.ptr type:(MTLDataType)constants[i].type atIndex:constants[i].index];
    descriptor.constantValues = values;
    [values release];
  }

  NSError *err = NULL;
  params->ret = (obj_handle_t)[library newFunctionWithDescriptor:descriptor error:&err];
  params->ret_error = (obj_handle_t)err;
  [name release];
  [specialized_name release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTQueryDisplaySetting(void *obj) {
  struct unixcall_query_display_setting *params = obj;
  CGDirectDisplayID display_id = params->display_id;
  struct WMTHDRMetadata *value = params->hdr_metadata.ptr;
  params->ret = false;
  struct DisplaySetting *setting = &g_display_settings[display_id == CGMainDisplayID()];
  if (setting->version) {
    *value = setting->hdr_metadata;
    params->colorspace = setting->colorspace;
    params->ret = true;
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTUpdateDisplaySetting(void *obj) {
  struct unixcall_update_display_setting *params = obj;
  CGDirectDisplayID display_id = params->display_id;
  const struct WMTHDRMetadata *value = params->hdr_metadata.ptr;
  struct DisplaySetting *setting = &g_display_settings[display_id == CGMainDisplayID()];
  if (value) {
    setting->hdr_metadata = *value;
    setting->colorspace = params->colorspace;
    setting->version++;
  } else {
    setting->version = 0;
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTQueryDisplaySettingForLayer(void *obj) {
  struct unixcall_query_display_setting_for_layer *params = obj;
  CAMetalLayer *layer = (CAMetalLayer *)params->layer;
  struct WMTHDRMetadata *hdr_metadata_out = params->hdr_metadata.ptr;

  params->version = 0;
  if (![layer.delegate isKindOfClass:NSView.class])
    return STATUS_SUCCESS;

  NSView *view = (NSView *)layer.delegate;
  if (!view.window)
    return STATUS_SUCCESS;

  if (!view.window.screen)
    return STATUS_SUCCESS;

  NSScreen *screen = view.window.screen;
  CGDirectDisplayID id = [[[screen deviceDescription] objectForKey:@"NSScreenNumber"] unsignedIntValue];

  struct DisplaySetting *setting = &g_display_settings[id == CGMainDisplayID()];
  *hdr_metadata_out = setting->hdr_metadata;
  params->version = setting->version;
  params->colorspace = setting->colorspace;
  params->edr_value.maximum_edr_color_component_value =
      layer.wantsExtendedDynamicRangeContent ? screen.maximumExtendedDynamicRangeColorComponentValue : 1.0;
  params->edr_value.maximum_potential_edr_color_component_value =
      screen.maximumPotentialExtendedDynamicRangeColorComponentValue;

  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_encodeWaitForEvent(void *obj) {
  struct unixcall_generic_obj_obj_uint64_noret *params = obj;
  [(id<MTLCommandBuffer>)params->handle encodeWaitForEvent:(id<MTLSharedEvent>)params->arg0 value:params->arg1];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLSharedEvent_signalValue(void *obj) {
  struct unixcall_generic_obj_uint64_noret *params = obj;
  [(id<MTLSharedEvent>)params->handle setSignaledValue:params->arg];
  return STATUS_SUCCESS;
}

#ifndef DXMT_NATIVE

typedef struct {
  _Atomic(CFRunLoopRef) runloop_ref;
  MTLSharedEventListener *shared_listener;
} *shared_event_listener_t;

extern NTSTATUS NtSetEvent(void *handle, void *prev_state);

static NTSTATUS
_MTLSharedEvent_setWin32EventAtValue(void *obj) {
  struct unixcall_mtlsharedevent_setevent *params = obj;
  void *nt_event_handle = (shared_event_listener_t)params->event_handle;
  shared_event_listener_t q = (shared_event_listener_t)params->shared_event_listener;
  [(id<MTLSharedEvent>)params->shared_event
      notifyListener:q->shared_listener
             atValue:params->value
               block:^(id<MTLSharedEvent> _e, uint64_t _v) {
                 // NOTE: must ensure no more notification comes after listener been destroyed.
                 while (!atomic_load_explicit(&q->runloop_ref, memory_order_acquire)) {
#if defined(__x86_64__)
                   _mm_pause();
#elif defined(__aarch64__)
          __asm__ __volatile__("yield");
#endif
                 }
                 CFRunLoopPerformBlock(q->runloop_ref, kCFRunLoopCommonModes, ^{
                   NtSetEvent(nt_event_handle, NULL);
                 });
                 CFRunLoopWakeUp(q->runloop_ref);
               }];
  return STATUS_SUCCESS;
}

static NTSTATUS
_SharedEventListener_start(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;
  shared_event_listener_t q = (shared_event_listener_t)params->handle;
  CFRunLoopRef uninited = NULL;
  if (q && atomic_compare_exchange_strong(&q->runloop_ref, &uninited, CFRunLoopGetCurrent())) {
    /* Add a dummy source so the runloop stays running */
    CFRunLoopSourceContext source_context = {0};
    CFRunLoopSourceRef source = CFRunLoopSourceCreate(NULL, 0, &source_context);
    CFRunLoopAddSource(q->runloop_ref, source, kCFRunLoopCommonModes);
    CFRunLoopRun();
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_SharedEventListener_create(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  shared_event_listener_t q = malloc(sizeof(*q));
  if (q) {
    q->runloop_ref = NULL;
    q->shared_listener = [[MTLSharedEventListener alloc] init];
  }
  params->ret = (obj_handle_t)q;
  return STATUS_SUCCESS;
}

static NTSTATUS
_SharedEventListener_destroy(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;
  shared_event_listener_t q = (shared_event_listener_t)params->handle;
  if (q && q->runloop_ref) {
    CFRunLoopStop(q->runloop_ref);
    q->runloop_ref = NULL;
    [q->shared_listener release];
    q->shared_listener = nil;
    free(q);
  }
  return STATUS_SUCCESS;
}

#else
static NTSTATUS
_MTLSharedEvent_setWin32EventAtValue(void *obj) {
  // nop
  return STATUS_SUCCESS;
}

static NTSTATUS
_SharedEventListener_start(void *obj) {
  return STATUS_SUCCESS;
}

static NTSTATUS
_SharedEventListener_create(void *obj) {
  return STATUS_SUCCESS;
}

static NTSTATUS
_SharedEventListener_destroy(void *obj) {
  return STATUS_SUCCESS;
}

#endif

static NTSTATUS
_MTLDevice_newFence(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLDevice>)params->handle newFence];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newEvent(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLDevice>)params->handle newEvent];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLBuffer_updateContents(void *obj) {
  struct unixcall_mtlbuffer_updatecontents *params = obj;
  memcpy((void *)((char *)[(id<MTLBuffer>)params->buffer contents] + params->offset), params->data.ptr, params->length);
  if ([(id<MTLBuffer>)params->buffer storageMode] == MTLStorageModeManaged)
    [(id<MTLBuffer>)params->buffer didModifyRange:NSMakeRange(params->offset, params->length)];
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTGetOSVersion(void *obj) {
  struct unixcall_get_os_version *params = obj;
  NSOperatingSystemVersion version = [NSProcessInfo processInfo].operatingSystemVersion;
  params->ret_major = version.majorVersion;
  params->ret_minor = version.minorVersion;
  params->ret_patch = version.patchVersion;
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newBinaryArchive(void *obj) {
  struct unixcall_mtldevice_newbinaryarchive *params = obj;
  NSString *path_str = NULL;
  NSURL *url = NULL;
  MTLBinaryArchiveDescriptor *desc = [[MTLBinaryArchiveDescriptor alloc] init];
  if (params->url.ptr != NULL) {
    path_str = [[NSString alloc] initWithCString:params->url.ptr encoding:NSUTF8StringEncoding];
    url = [[NSURL alloc] initFileURLWithPath:path_str];
    desc.url = url;
  }
  NSError *err = NULL;
  params->ret_archive = (obj_handle_t)[(id<MTLDevice>)params->device newBinaryArchiveWithDescriptor:desc error:&err];
  params->ret_error = (obj_handle_t)err;
  [desc release];
  if (url)
    [url release];
  if (path_str)
    [path_str release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLBinaryArchive_serialize(void *obj) {
  struct unixcall_mtlbinaryarchive_serialize *params = obj;
  NSString *path_str = [[NSString alloc] initWithCString:params->url.ptr encoding:NSUTF8StringEncoding];
  NSURL *url = [[NSURL alloc] initFileURLWithPath:path_str];
  NSError *err = NULL;
  [(id<MTLBinaryArchive>)params->archive serializeToURL:url error:&err];
  params->ret_error = (obj_handle_t)err;
  [url release];
  [path_str release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_DispatchData_alloc_init(void *obj) {
  struct unixcall_generic_obj_uint64_obj_ret *params = obj;
  params->ret = (obj_handle_t)dispatch_data_create((void *)params->handle, params->arg, NULL, NULL);
  return STATUS_SUCCESS;
}

@interface MTLSharedTextureHandle ()

- (MTLSharedTextureHandle *)initWithMachPort:(mach_port_t)port;
- (mach_port_t)createMachPort;

@end

static NTSTATUS
_MTLDevice_newSharedTexture(void *obj) {
  struct unixcall_mtldevice_newtexture *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  struct WMTTextureInfo *info = params->info.ptr;

  if (info->mach_port) {
    MTLSharedTextureHandle *handle = [[MTLSharedTextureHandle alloc] initWithMachPort:info->mach_port];
    id<MTLTexture> ret = [device newSharedTextureWithHandle:handle];
    extract_texture_descriptor(ret, info);
    params->ret = (obj_handle_t)ret;
    info->gpu_resource_id = [ret gpuResourceID]._impl;
    [handle release];
  } else {
    MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
    fill_texture_descriptor(desc, info);
    id<MTLTexture> ret = [device newSharedTextureWithDescriptor:desc];
    MTLSharedTextureHandle *handle = [ret newSharedTextureHandle];
    params->ret = (obj_handle_t)ret;
    info->gpu_resource_id = [ret gpuResourceID]._impl;
    info->mach_port = [handle createMachPort]; // implicitly add ref to underlying IOSurface
    [handle release];
    [desc release];
  }

  return STATUS_SUCCESS;
}

/* Private API to register a mach port with the bootstrap server */
extern kern_return_t bootstrap_register2(mach_port_t bp, name_t service_name, mach_port_t sp, int flags);

static NTSTATUS
_WMTBootstrapRegister(void *obj) {
  struct unixcall_bootstrap *params = obj;
  mach_port_t rp = params->mach_port;
  mach_port_t bp;

  if (task_get_bootstrap_port(mach_task_self(), &bp) != KERN_SUCCESS)
    return STATUS_UNSUCCESSFUL;
  NTSTATUS ret = bootstrap_register2(bp, params->name, rp, 0) != KERN_SUCCESS ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
  mach_port_deallocate(mach_task_self(), bp);
  return ret;
}

static NTSTATUS
_WMTBootstrapLookUp(void *obj) {
  struct unixcall_bootstrap *params = obj;
  mach_port_t rp = 0;
  mach_port_t bp;

  if (task_get_bootstrap_port(mach_task_self(), &bp) != KERN_SUCCESS)
    return STATUS_UNSUCCESSFUL;
  NTSTATUS ret = bootstrap_look_up(bp, params->name, &rp) != KERN_SUCCESS ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
  mach_port_deallocate(mach_task_self(), bp);
  params->mach_port = rp;
  return ret;
}

@protocol MTLDeviceSPI <MTLDevice>

- (id<MTLSharedEvent>)newSharedEventWithMachPort:(mach_port_t)machPort;

@end

@interface MTLSharedEventHandle ()

- (mach_port_t)eventPort;

@end

static NTSTATUS
_MTLSharedEvent_createMachPort(void *obj) {
  struct unixcall_mtlsharedevent_createmachport *params = obj;
  id<MTLSharedEvent> event = (id<MTLSharedEvent>)params->event;
  MTLSharedEventHandle *handle = [event newSharedEventHandle];
  mach_port_t port = [handle eventPort];

  // The eventPort method returns a send right that's owned by the handle.
  // We need to add our own send right since we're keeping the port but releasing the handle.
  // This increments the send right count so the port remains valid.
  mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_SEND, 1);

  params->ret_mach_port = port;
  [handle release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newSharedEventWithMachPort(void *obj) {
  struct unixcall_mtldevice_newsharedeventwithmachport *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  id<MTLDeviceSPI> deviceSPI = (id<MTLDeviceSPI>)device;
  params->ret_event = (obj_handle_t)[deviceSPI newSharedEventWithMachPort:params->mach_port];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_registryID(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle registryID];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLSharedEvent_waitUntilSignaledValue(void *obj) {
  struct unixcall_mtlsharedevent_waituntilsignaledvalue *params = obj;
  bool timeout = [(id<MTLSharedEvent>)params->event waitUntilSignaledValue:params->value timeoutMS:params->timeout_ms];
  params->ret_timeout = timeout;
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCounterSampleBuffer_newTimestampBuffer(void *obj) {
  struct unixcall_mtlcountersamplebuffer_newtimestampbuffer *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;

  MTLCounterSampleBufferDescriptor *desc = [[MTLCounterSampleBufferDescriptor alloc] init];
  NSArray<id<MTLCounterSet>> *counter_sets = [device counterSets];
  id<MTLCounterSet> timestamp_counter_set = nil;
  for (id<MTLCounterSet> counterSet in counter_sets) {
    if ([counterSet.name isEqualToString:MTLCommonCounterSetTimestamp]) {
      timestamp_counter_set = counterSet;
      break;
    }
  }
  desc.counterSet = timestamp_counter_set;
  desc.sampleCount = params->sample_count;
  desc.storageMode = params->shared ? MTLStorageModeShared : MTLStorageModePrivate;

  NSError *error = nil;
  params->ret = (obj_handle_t)[device newCounterSampleBufferWithDescriptor:desc error:&error];

  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCounterSampleBuffer_resolveCounterRange(void *obj) {
  struct unixcall_mtlcountersamplebuffer_resolvecounterrange *params = obj;
  id<MTLCounterSampleBuffer> sample_buffer = (id<MTLCounterSampleBuffer>)params->sample_buffer;

  NSData *data = [sample_buffer resolveCounterRange:NSMakeRange(params->start, params->len)];
  if (data && params->data_out.ptr) {
    [data getBytes:params->data_out.ptr length:params->data_length];
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_blitCommandEncoderWithSampleBuffers(void *obj) {
  struct unixcall_mtlcommandbuffer_blitcommandencoderwithsamplebuffers *params = obj;
  id<MTLCommandBuffer> cmdbuf = (id<MTLCommandBuffer>)params->cmdbuf;
  struct WMTSampleBufferAttachmentInfo *attachments = params->attachments.ptr;

  MTLBlitPassDescriptor *blit_desc = [[MTLBlitPassDescriptor alloc] init];
  for (uint64_t i = 0; i < params->num_attachments; i++) {
    MTLBlitPassSampleBufferAttachmentDescriptor *desc = blit_desc.sampleBufferAttachments[i];
    desc.sampleBuffer = (id<MTLCounterSampleBuffer>)attachments[i].sample_buffer;
    desc.startOfEncoderSampleIndex = attachments[i].start_of_encoder_sample_index;
    desc.endOfEncoderSampleIndex = attachments[i].end_of_encoder_sample_index;
  }

  params->ret = (obj_handle_t)[cmdbuf blitCommandEncoderWithDescriptor:blit_desc];

  [blit_desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_property(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  id<MTLCommandBuffer> cmdbuf = (id<MTLCommandBuffer>)params->handle;
  double ns = 1000000000;
  switch (params->arg) {
  case WMTCommandBufferPropertyKernelStartTime:
    params->ret = (uint64_t)([cmdbuf kernelStartTime] * ns);
    break;
  case WMTCommandBufferPropertyKernelEndTime:
    params->ret = (uint64_t)([cmdbuf kernelEndTime] * ns);
    break;
  case WMTCommandBufferPropertyGPUStartTime:
    params->ret = (uint64_t)([cmdbuf GPUStartTime] * ns);
    break;
  case WMTCommandBufferPropertyGPUEndTime:
    params->ret = (uint64_t)([cmdbuf GPUEndTime] * ns);
    break;
  default:
    params->ret = 0;
    break;
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newTileRenderPipelineState(void *obj) {
  struct unixcall_mtldevice_newrenderpso *params = obj;
  const struct WMTTileRenderPipelineInfo *info = params->info.ptr;
  MTLTileRenderPipelineDescriptor *descriptor = [[MTLTileRenderPipelineDescriptor alloc] init];

  for (unsigned i = 0; i < 8; i++) {
    descriptor.colorAttachments[i].pixelFormat = to_metal_pixel_format(info->color_formats[i]);
  }

  for (unsigned i = 0; i < 31; i++) {
    if (info->immutable_tile_buffers & (1 << i))
      descriptor.tileBuffers[i].mutability = MTLMutabilityImmutable;
  }

  descriptor.rasterSampleCount = info->raster_sample_count;
  descriptor.threadgroupSizeMatchesTileSize = info->tgsize_matches_tile_size;

  descriptor.tileFunction = (id<MTLFunction>)info->tile_function;

  MTLPipelineOption options = MTLPipelineOptionNone;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
  if (@available(macOS 15, *)) {
    if (info->num_binary_archives_for_lookup && info->binary_archives_for_lookup.ptr)
      descriptor.binaryArchives = winemetal_array_from_handles(
          (const obj_handle_t *)info->binary_archives_for_lookup.ptr, info->num_binary_archives_for_lookup,
          "tile.binaryArchives"
      );
    options = info->fail_on_binary_archive_miss ? MTLPipelineOptionFailOnBinaryArchiveMiss : MTLPipelineOptionNone;
  }
#endif
  NSError *err = NULL;
  params->ret_pso = (obj_handle_t)[(id<MTLDevice>)params->device newRenderPipelineStateWithTileDescriptor:descriptor
                                                                                                  options:options
                                                                                               reflection:nil
                                                                                                    error:&err];
  params->ret_error = (obj_handle_t)err;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
  if (@available(macOS 15, *)) {
    if (params->ret_pso && !err && info->binary_archive_for_serialization) {
      [(id<MTLBinaryArchive>)info->binary_archive_for_serialization
          addTileRenderPipelineFunctionsWithDescriptor:descriptor
                                                 error:&err];
    }
  }
#endif
  [descriptor release];
  return STATUS_SUCCESS;
}

/*
 * Definition from cache.c
 */

NTSTATUS _CacheReader_alloc_init(void *obj);
NTSTATUS _CacheReader_get(void *obj);
NTSTATUS _CacheWriter_alloc_init(void *obj);
NTSTATUS _CacheWriter_set(void *obj);
NTSTATUS _WMTSetMetalShaderCachePath(void *obj);

const void *__wine_unix_call_funcs[] = {
    &_NSObject_retain,
    &_NSObject_release,
    &_NSArray_object,
    &_NSArray_count,
    &_MTLCopyAllDevices,
    &_MTLDevice_recommendedMaxWorkingSetSize,
    &_MTLDevice_currentAllocatedSize,
    &_MTLDevice_name,
    &_NSString_getCString,
    &_MTLDevice_newCommandQueue,
    &_NSAutoreleasePool_alloc_init,
    &_MTLCommandQueue_commandBuffer,
    &_MTLCommandBuffer_commit,
    &_MTLCommandBuffer_waitUntilCompleted,
    &_MTLCommandBuffer_status,
    &_MTLDevice_newSharedEvent,
    &_MTLSharedEvent_signaledValue,
    &_MTLCommandBuffer_encodeSignalEvent,
    &_MTLDevice_newBuffer,
    &_MTLDevice_newSamplerState,
    &_MTLDevice_newDepthStencilState,
    &_MTLDevice_newTexture,
    &_MTLBuffer_newTexture,
    &_MTLTexture_newTextureView,
    &_MTLDevice_minimumLinearTextureAlignmentForPixelFormat,
    &_MTLDevice_newLibrary,
    &_MTLLibrary_newFunction,
    &_NSString_lengthOfBytesUsingEncoding,
    &_NSObject_description,
    &_MTLDevice_newComputePipelineState,
    &_MTLCommandBuffer_blitCommandEncoder,
    &_MTLCommandBuffer_computeCommandEncoder,
    &_MTLCommandBuffer_renderCommandEncoder,
    &_MTLCommandEncoder_endEncoding,
    &_MTLDevice_newRenderPipelineState,
    &_MTLDevice_newMeshRenderPipelineState,
    &_MTLBlitCommandEncoder_encodeCommands,
    &_MTLComputeCommandEncoder_encodeCommands,
    &_MTLRenderCommandEncoder_encodeCommands,
    &_MTLTexture_pixelFormat,
    &_MTLTexture_width,
    &_MTLTexture_height,
    &_MTLTexture_depth,
    &_MTLTexture_arrayLength,
    &_MTLTexture_mipmapLevelCount,
    &_MTLTexture_replaceRegion,
    &_MTLBuffer_didModifyRange,
    &_MTLCommandBuffer_presentDrawable,
    &_MTLCommandBuffer_presentDrawableAfterMinimumDuration,
    &_MTLDevice_supportsFamily,
    &_MTLDevice_supportsBCTextureCompression,
    &_MTLDevice_supportsTextureSampleCount,
    &_MTLDevice_hasUnifiedMemory,
    &_MTLCaptureManager_sharedCaptureManager,
    &_MTLCaptureManager_startCapture,
    &_MTLCaptureManager_stopCapture,
    &_MTLDevice_newTemporalScaler,
    &_MTLDevice_newSpatialScaler,
    &_MTLCommandBuffer_encodeTemporalScale,
    &_MTLCommandBuffer_encodeSpatialScale,
    &_NSString_string,
    &_NSString_alloc_init,
    &_DeveloperHUDProperties_instance,
    &_DeveloperHUDProperties_addLabel,
    &_DeveloperHUDProperties_updateLabel,
    &_DeveloperHUDProperties_remove,
    &_MetalDrawable_texture,
    &_MetalLayer_nextDrawable,
    &_MTLDevice_supportsFXSpatialScaler,
    &_MTLDevice_supportsFXTemporalScaler,
    &_MetalLayer_setProps,
    &_MetalLayer_getProps,
    &_CreateMetalViewFromHWND,
    &_ReleaseMetalView,
    &thunk_SM50Initialize,
    &thunk_SM50Destroy,
    &thunk_SM50Compile,
    &thunk_SM50GetCompiledBitcode,
    &thunk_SM50DestroyBitcode,
    &thunk_SM50GetErrorMessage,
    &thunk_SM50FreeError,
    &thunk_SM50CompileGeometryPipelineVertex,
    &thunk_SM50CompileGeometryPipelineGeometry,
    NULL,
    &thunk_SM50CompileTessellationPipelineHull,
    &thunk_SM50CompileTessellationPipelineDomain,
    &_MTLCommandEncoder_setLabel,
    &_MTLDevice_setShouldMaximizeConcurrentCompilation,
    &thunk_SM50GetArgumentsInfo,
    &_MTLCommandBuffer_error,
    &_MTLCommandBuffer_logs,
    &_MTLLogContainer_enumerate,
    &_CGColorSpace_checkColorSpaceSupported,
    &_MetalLayer_setColorSpace,
    &_WMTGetPrimaryDisplayId,
    &_WMTGetSecondaryDisplayId,
    &_WMTGetDisplayDescription,
    &_MetalLayer_getEDRValue,
    &_MTLLibrary_newFunctionWithConstants,
    &_WMTQueryDisplaySetting,
    &_WMTUpdateDisplaySetting,
    &_WMTQueryDisplaySettingForLayer,
    &_MTLCommandBuffer_encodeWaitForEvent,
    &_MTLSharedEvent_signalValue,
    &_MTLSharedEvent_setWin32EventAtValue,
    &_MTLDevice_newFence,
    &_MTLDevice_newEvent,
    &_MTLBuffer_updateContents,
    &_SharedEventListener_create,
    &_SharedEventListener_start,
    &_SharedEventListener_destroy,
    &_WMTGetOSVersion,
    &_MTLDevice_newBinaryArchive,
    &_MTLBinaryArchive_serialize,
    &_DispatchData_alloc_init,
    &_CacheReader_alloc_init,
    &_CacheReader_get,
    &_CacheWriter_alloc_init,
    &_CacheWriter_set,
    &_WMTSetMetalShaderCachePath,
    &_MTLDevice_newSharedTexture,
    &_WMTBootstrapRegister,
    &_WMTBootstrapLookUp,
    &_MTLSharedEvent_createMachPort,
    &_MTLDevice_newSharedEventWithMachPort,
    &_MTLDevice_registryID,
    &_MTLSharedEvent_waitUntilSignaledValue,
    &_MTLCounterSampleBuffer_newTimestampBuffer,
    &_MTLCounterSampleBuffer_resolveCounterRange,
    &_MTLCommandBuffer_blitCommandEncoderWithSampleBuffers,
    &_MTLCommandBuffer_property,
    &_MTLDevice_newTileRenderPipelineState,
    &_MTLDevice_newLibraryWithSource,
    &_MTLLibrary_newFunctionWithDescriptor,
    &_MTLFunction_copyVertexAttributes,
    &_WMTM12CoreRecordCounters,
    &_WMTM12CoreHashShaderBytecode,
    &_WMTM12CoreFormatShaderCachePaths,
    &_WMTM12CoreProbeShaderCache,
    &_WMTM12CoreParseShaderReflection,
    &_WMTM12CoreMakePipelineCacheKey,
    &_WMTM12CoreCreateShaderFunction,
    &_WMTM12CoreLowerDXILToMSL,
    &_WMTM12CoreReflectSM50Shader,
    &_WMTM12CoreLookupPipelineCache,
    &_WMTM12CoreStorePipelineCache,
    &_WMTM12CoreMakePipelineCacheKeyFromFields,
    &_WMTM12CoreCreatePipelineState,
    &_WMTM12CoreSummarizeRootSignature,
    &_WMTM12CoreBuildRootBindingPlan,
    &_WMTM12CoreLookupRootBinding,
    &_WMTM12CoreSummarizePrewarmPack,
    &_WMTM12CoreBuildDrawPlan,
    &_WMTM12CoreBuildPresentPlan,
    &_WMTM12CoreBuildReplayPlan,
    &_WMTM12CoreValidateCommandStream,
    &_WMTM12CorePlanRenderPass,
    &_WMTM12CorePlanPresentExecute,
    &_WMTM12CoreExecutePresentBlit,
    &_WMTM12CorePlanReplayExecute,
    &_WMTM12CoreValidateCommandPacketStream,
    &_WMTM12CoreMakeCacheCompatibilityKey,
    &_WMTM12CoreRegisterHandle,
    &_WMTM12CoreValidateHandle,
    &_WMTM12CoreClassifyPacketSupport,
    &_WMTM12CoreExecuteReplayPacketStream,
    &_WMTM12CorePlanEncoderOwnership,
    &_WMTM12CorePlanNativePresentOwnership,
    &_WMTM12CorePlanCacheWarmStart,
};

#ifndef DXMT_NATIVE
const void *__wine_unix_call_wow64_funcs[] = {
    &_NSObject_retain,
    &_NSObject_release,
    &_NSArray_object,
    &_NSArray_count,
    &_MTLCopyAllDevices,
    &_MTLDevice_recommendedMaxWorkingSetSize,
    &_MTLDevice_currentAllocatedSize,
    &_MTLDevice_name,
    &_NSString_getCString,
    &_MTLDevice_newCommandQueue,
    &_NSAutoreleasePool_alloc_init,
    &_MTLCommandQueue_commandBuffer,
    &_MTLCommandBuffer_commit,
    &_MTLCommandBuffer_waitUntilCompleted,
    &_MTLCommandBuffer_status,
    &_MTLDevice_newSharedEvent,
    &_MTLSharedEvent_signaledValue,
    &_MTLCommandBuffer_encodeSignalEvent,
    &_MTLDevice_newBuffer,
    &_MTLDevice_newSamplerState,
    &_MTLDevice_newDepthStencilState,
    &_MTLDevice_newTexture,
    &_MTLBuffer_newTexture,
    &_MTLTexture_newTextureView,
    &_MTLDevice_minimumLinearTextureAlignmentForPixelFormat,
    &_MTLDevice_newLibrary,
    &_MTLLibrary_newFunction,
    &_NSString_lengthOfBytesUsingEncoding,
    &_NSObject_description,
    &_MTLDevice_newComputePipelineState,
    &_MTLCommandBuffer_blitCommandEncoder,
    &_MTLCommandBuffer_computeCommandEncoder,
    &_MTLCommandBuffer_renderCommandEncoder,
    &_MTLCommandEncoder_endEncoding,
    &_MTLDevice_newRenderPipelineState,
    &_MTLDevice_newMeshRenderPipelineState,
    &_MTLBlitCommandEncoder_encodeCommands,
    &_MTLComputeCommandEncoder_encodeCommands,
    &_MTLRenderCommandEncoder_encodeCommands,
    &_MTLTexture_pixelFormat,
    &_MTLTexture_width,
    &_MTLTexture_height,
    &_MTLTexture_depth,
    &_MTLTexture_arrayLength,
    &_MTLTexture_mipmapLevelCount,
    &_MTLTexture_replaceRegion,
    &_MTLBuffer_didModifyRange,
    &_MTLCommandBuffer_presentDrawable,
    &_MTLCommandBuffer_presentDrawableAfterMinimumDuration,
    &_MTLDevice_supportsFamily,
    &_MTLDevice_supportsBCTextureCompression,
    &_MTLDevice_supportsTextureSampleCount,
    &_MTLDevice_hasUnifiedMemory,
    &_MTLCaptureManager_sharedCaptureManager,
    &_MTLCaptureManager_startCapture,
    &_MTLCaptureManager_stopCapture,
    &_MTLDevice_newTemporalScaler,
    &_MTLDevice_newSpatialScaler,
    &_MTLCommandBuffer_encodeTemporalScale,
    &_MTLCommandBuffer_encodeSpatialScale,
    &_NSString_string,
    &_NSString_alloc_init,
    &_DeveloperHUDProperties_instance,
    &_DeveloperHUDProperties_addLabel,
    &_DeveloperHUDProperties_updateLabel,
    &_DeveloperHUDProperties_remove,
    &_MetalDrawable_texture,
    &_MetalLayer_nextDrawable,
    &_MTLDevice_supportsFXSpatialScaler,
    &_MTLDevice_supportsFXTemporalScaler,
    &_MetalLayer_setProps,
    &_MetalLayer_getProps,
    &_CreateMetalViewFromHWND,
    &_ReleaseMetalView,
    &thunk32_SM50Initialize,
    &thunk_SM50Destroy,
    &thunk32_SM50Compile,
    &thunk32_SM50GetCompiledBitcode,
    &thunk_SM50DestroyBitcode,
    &thunk32_SM50GetErrorMessage,
    &thunk_SM50FreeError,
    &thunk32_SM50CompileGeometryPipelineVertex,
    &thunk32_SM50CompileGeometryPipelineGeometry,
    NULL,
    &thunk32_SM50CompileTessellationPipelineHull,
    &thunk32_SM50CompileTessellationPipelineDomain,
    &_MTLCommandEncoder_setLabel,
    &_MTLDevice_setShouldMaximizeConcurrentCompilation,
    &thunk32_SM50GetArgumentsInfo,
    &_MTLCommandBuffer_error,
    &_MTLCommandBuffer_logs,
    &_MTLLogContainer_enumerate,
    &_CGColorSpace_checkColorSpaceSupported,
    &_MetalLayer_setColorSpace,
    &_WMTGetPrimaryDisplayId,
    &_WMTGetSecondaryDisplayId,
    &_WMTGetDisplayDescription,
    &_MetalLayer_getEDRValue,
    &_MTLLibrary_newFunctionWithConstants,
    &_WMTQueryDisplaySetting,
    &_WMTUpdateDisplaySetting,
    &_WMTQueryDisplaySettingForLayer,
    &_MTLCommandBuffer_encodeWaitForEvent,
    &_MTLSharedEvent_signalValue,
    &_MTLSharedEvent_setWin32EventAtValue,
    &_MTLDevice_newFence,
    &_MTLDevice_newEvent,
    &_MTLBuffer_updateContents,
    &_SharedEventListener_create,
    &_SharedEventListener_start,
    &_SharedEventListener_destroy,
    &_WMTGetOSVersion,
    &_MTLDevice_newBinaryArchive,
    &_MTLBinaryArchive_serialize,
    &_DispatchData_alloc_init,
    &_CacheReader_alloc_init,
    &_CacheReader_get,
    &_CacheWriter_alloc_init,
    &_CacheWriter_set,
    &_WMTSetMetalShaderCachePath,
    &_MTLDevice_newSharedTexture,
    &_WMTBootstrapRegister,
    &_WMTBootstrapLookUp,
    &_MTLSharedEvent_createMachPort,
    &_MTLDevice_newSharedEventWithMachPort,
    &_MTLDevice_registryID,
    &_MTLSharedEvent_waitUntilSignaledValue,
    &_MTLCounterSampleBuffer_newTimestampBuffer,
    &_MTLCounterSampleBuffer_resolveCounterRange,
    &_MTLCommandBuffer_blitCommandEncoderWithSampleBuffers,
    &_MTLCommandBuffer_property,
    &_MTLDevice_newTileRenderPipelineState,
    &_MTLDevice_newLibraryWithSource,
    &_MTLLibrary_newFunctionWithDescriptor,
    &_MTLFunction_copyVertexAttributes,
    &_WMTM12CoreRecordCounters,
    &_WMTM12CoreHashShaderBytecode,
    &_WMTM12CoreFormatShaderCachePaths,
    &_WMTM12CoreProbeShaderCache,
    &_WMTM12CoreParseShaderReflection,
    &_WMTM12CoreMakePipelineCacheKey,
    &_WMTM12CoreCreateShaderFunction,
    &_WMTM12CoreLowerDXILToMSL,
    &_WMTM12CoreReflectSM50Shader,
    &_WMTM12CoreLookupPipelineCache,
    &_WMTM12CoreStorePipelineCache,
    &_WMTM12CoreMakePipelineCacheKeyFromFields,
    &_WMTM12CoreCreatePipelineState,
    &_WMTM12CoreSummarizeRootSignature,
    &_WMTM12CoreBuildRootBindingPlan,
    &_WMTM12CoreLookupRootBinding,
    &_WMTM12CoreSummarizePrewarmPack,
    &_WMTM12CoreBuildDrawPlan,
    &_WMTM12CoreBuildPresentPlan,
    &_WMTM12CoreBuildReplayPlan,
    &_WMTM12CoreValidateCommandStream,
    &_WMTM12CorePlanRenderPass,
    &_WMTM12CorePlanPresentExecute,
    &_WMTM12CoreExecutePresentBlit,
    &_WMTM12CorePlanReplayExecute,
    &_WMTM12CoreValidateCommandPacketStream,
    &_WMTM12CoreMakeCacheCompatibilityKey,
    &_WMTM12CoreRegisterHandle,
    &_WMTM12CoreValidateHandle,
    &_WMTM12CoreClassifyPacketSupport,
    &_WMTM12CoreExecuteReplayPacketStream,
    &_WMTM12CorePlanEncoderOwnership,
    &_WMTM12CorePlanNativePresentOwnership,
    &_WMTM12CorePlanCacheWarmStart,
};
#endif
