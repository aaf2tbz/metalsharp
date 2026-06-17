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
  M12CORE_FEATURE_COUNTERS = 1u << 1,
  M12CORE_FEATURE_SHADER_INTROSPECTION = 1u << 2,
  M12CORE_FEATURE_SHADER_FUNCTIONS = 1u << 3,
  M12CORE_FEATURE_DXIL_TO_MSL = 1u << 4,
};

typedef struct M12CoreVersion {
  uint32_t abi_version;
  uint32_t feature_flags;
  uint32_t build_id_low;
  uint32_t build_id_high;
} M12CoreVersion;

/* Counter IDs are stable ABI values.  Keep these append-only: old PE shims may
 * send counters to a newer core, and newer shims must tolerate older cores that
 * only know the first subset.  These IDs intentionally mirror the current
 * PSO_PRESSURE vocabulary so Phase 2 can move diagnostics ownership without
 * changing perf-analysis log semantics.
 */
typedef enum M12CoreCounterId {
  M12CORE_COUNTER_LOADER_LOAD_SUCCESS = 0,
  M12CORE_COUNTER_GRAPHICS_PSO_REQUESTS = 1,
  M12CORE_COUNTER_COMPUTE_PSO_REQUESTS = 2,
  M12CORE_COUNTER_GRAPHICS_PSO_REPEATED = 3,
  M12CORE_COUNTER_COMPUTE_PSO_REPEATED = 4,
  M12CORE_COUNTER_SHADER_MEMORY_CACHE_HITS = 5,
  M12CORE_COUNTER_SHADER_MEMORY_CACHE_MISSES = 6,
  M12CORE_COUNTER_SHADER_METALLIB_CACHE_HITS = 7,
  M12CORE_COUNTER_SHADER_METALLIB_CACHE_MISSES = 8,
  M12CORE_COUNTER_METAL_RENDER_PIPELINE_CREATES = 9,
  M12CORE_COUNTER_METAL_COMPUTE_PIPELINE_CREATES = 10,
  M12CORE_COUNTER_RENDER_PIPELINE_CACHE_HITS = 11,
  M12CORE_COUNTER_RENDER_PIPELINE_CACHE_MISSES = 12,
  M12CORE_COUNTER_COMPUTE_PIPELINE_CACHE_HITS = 13,
  M12CORE_COUNTER_COMPUTE_PIPELINE_CACHE_MISSES = 14,
  M12CORE_COUNTER_COMPILE_WAIT_COUNT = 15,
  M12CORE_COUNTER_COMPILE_WAIT_NS = 16,
  M12CORE_COUNTER_COUNT = 17,
} M12CoreCounterId;

typedef struct M12CoreCounterSnapshot {
  uint32_t abi_version;
  uint32_t counter_count;
  uint64_t values[M12CORE_COUNTER_COUNT];
} M12CoreCounterSnapshot;

/* Shader stages use stable ABI values rather than D3D12 enums so future
 * non-D3D12 callers can share the same cache namespace.  Keep append-only.
 */
typedef enum M12CoreShaderStage {
  M12CORE_SHADER_STAGE_UNKNOWN = 0,
  M12CORE_SHADER_STAGE_VERTEX = 1,
  M12CORE_SHADER_STAGE_PIXEL = 2,
  M12CORE_SHADER_STAGE_GEOMETRY = 3,
  M12CORE_SHADER_STAGE_HULL = 4,
  M12CORE_SHADER_STAGE_DOMAIN = 5,
  M12CORE_SHADER_STAGE_COMPUTE = 6,
} M12CoreShaderStage;

typedef struct M12CoreShaderBytecodeInfo {
  uint32_t abi_version;
  uint32_t stage;
  uint64_t bytecode_hash;
  uint64_t bytecode_size;
  uint32_t contains_dxil;
  uint32_t reserved;
} M12CoreShaderBytecodeInfo;

#define M12CORE_SHADER_CACHE_PATH_CAPACITY 1024u

typedef struct M12CoreShaderCachePaths {
  uint32_t abi_version;
  uint32_t path_capacity;
  uint64_t shader_hash;
  char cache_path[M12CORE_SHADER_CACHE_PATH_CAPACITY];
  char dxbc_path[M12CORE_SHADER_CACHE_PATH_CAPACITY];
  char metallib_path[M12CORE_SHADER_CACHE_PATH_CAPACITY];
  char reflection_path[M12CORE_SHADER_CACHE_PATH_CAPACITY];
  char module_summary_path[M12CORE_SHADER_CACHE_PATH_CAPACITY];
  char dxil_report_path[M12CORE_SHADER_CACHE_PATH_CAPACITY];
  char metallib_error_path[M12CORE_SHADER_CACHE_PATH_CAPACITY];
} M12CoreShaderCachePaths;

typedef struct M12CoreShaderCacheLookup {
  uint32_t abi_version;
  uint32_t metallib_available;
  uint32_t metallib_exists;
  uint32_t force_source_compile;
  M12CoreShaderCachePaths paths;
} M12CoreShaderCacheLookup;

#define M12CORE_SHADER_ENTRY_POINT_CAPACITY 256u

typedef struct M12CoreShaderReflectionSummary {
  uint32_t abi_version;
  uint32_t has_entry_point;
  uint32_t has_threadgroup_size;
  uint32_t reserved;
  char entry_point[M12CORE_SHADER_ENTRY_POINT_CAPACITY];
  uint32_t threadgroup_size[3];
} M12CoreShaderReflectionSummary;

typedef struct M12CoreVertexInputElement {
  uint32_t shader_register;
  uint32_t table_index;
  uint32_t input_slot;
  uint32_t aligned_byte_offset;
  uint32_t dxgi_format;
  uint32_t metal_format;
  uint32_t per_instance;
  uint32_t instance_step_rate;
  uint32_t table_indexing_mode;
  uint32_t system_value;
  uint32_t reserved[2];
} M12CoreVertexInputElement;

typedef enum M12CoreDXILToMSLStatus {
  M12CORE_DXIL_TO_MSL_STATUS_OK = 0,
  M12CORE_DXIL_TO_MSL_STATUS_INVALID = 1,
  M12CORE_DXIL_TO_MSL_STATUS_CONTAINER_PARSE_FAILED = 2,
  M12CORE_DXIL_TO_MSL_STATUS_BITCODE_PARSE_FAILED = 3,
  M12CORE_DXIL_TO_MSL_STATUS_LOWERING_FAILED = 4,
  M12CORE_DXIL_TO_MSL_STATUS_OUTPUT_TOO_SMALL = 5,
} M12CoreDXILToMSLStatus;

typedef struct M12CoreDXILToMSLDesc {
  uint32_t abi_version;
  uint32_t stage;
  const void *dxil_container;
  uint64_t dxil_container_size;
  const M12CoreVertexInputElement *vertex_inputs;
  uint32_t vertex_input_count;
  uint32_t reserved;
} M12CoreDXILToMSLDesc;

typedef struct M12CoreDXILToMSLResult {
  uint32_t abi_version;
  uint32_t status;
  uint64_t required_source_size;
  char entry_point[M12CORE_SHADER_ENTRY_POINT_CAPACITY];
  uint32_t threadgroup_size[3];
  uint32_t unsupported_intrinsics;
  uint32_t unsupported_opcodes;
  uint32_t used_typed_lowering;
  uint32_t reserved;
} M12CoreDXILToMSLResult;

typedef enum M12CoreShaderFunctionInputKind {
  M12CORE_SHADER_FUNCTION_INPUT_UNKNOWN = 0,
  M12CORE_SHADER_FUNCTION_INPUT_METALLIB = 1,
  M12CORE_SHADER_FUNCTION_INPUT_MSL_SOURCE = 2,
} M12CoreShaderFunctionInputKind;

typedef enum M12CoreShaderFunctionStatus {
  M12CORE_SHADER_FUNCTION_STATUS_OK = 0,
  M12CORE_SHADER_FUNCTION_STATUS_INVALID = 1,
  M12CORE_SHADER_FUNCTION_STATUS_LIBRARY_FAILED = 2,
  M12CORE_SHADER_FUNCTION_STATUS_FUNCTION_FAILED = 3,
} M12CoreShaderFunctionStatus;

typedef struct M12CoreShaderFunctionDesc {
  uint32_t abi_version;
  uint32_t stage;
  uint32_t input_kind;
  uint32_t reserved;
  uint64_t shader_hash;
  uint64_t device_handle;
  const void *input_data;
  uint64_t input_size;
  char entry_point[M12CORE_SHADER_ENTRY_POINT_CAPACITY];
} M12CoreShaderFunctionDesc;

typedef struct M12CoreShaderFunctionResult {
  uint32_t abi_version;
  uint32_t status;
  uint32_t cache_hit;
  uint32_t reserved;
  uint64_t function_handle;
  uint64_t error_handle;
  char selected_entry[M12CORE_SHADER_ENTRY_POINT_CAPACITY];
} M12CoreShaderFunctionResult;

typedef enum M12CorePipelineKind {
  M12CORE_PIPELINE_KIND_UNKNOWN = 0,
  M12CORE_PIPELINE_KIND_RENDER = 1,
  M12CORE_PIPELINE_KIND_COMPUTE = 2,
} M12CorePipelineKind;

typedef struct M12CorePipelineCacheKeyInput {
  uint32_t abi_version;
  uint32_t kind;
  uint64_t base_hash;
  uint64_t device_id;
  uint64_t flags;
} M12CorePipelineCacheKeyInput;

typedef struct M12CorePipelineCacheKey {
  uint32_t abi_version;
  uint32_t kind;
  uint64_t key;
} M12CorePipelineCacheKey;

/* Returns 0 on success. Non-zero values are reserved for future detailed
 * status codes once PE-side callers start depending on this ABI.
 */
int m12core_get_version(M12CoreVersion *out_version);

/* Human-readable build string for diagnostics only. The returned pointer is
 * process-lifetime static storage owned by libm12core.
 */
const char *m12core_build_string(void);

/* Phase-2 diagnostic counter API.  The native core owns the storage; PE-side
 * shims should eventually send increments through winemetal thunks instead of
 * owning independent process-local atomics.  These functions are intentionally
 * tiny and C-only so they can be used from direct loader probes, winemetal.so,
 * and future PE thunk calls.
 */
int m12core_record_counter(uint32_t counter_id, uint64_t delta);
int m12core_get_counters(M12CoreCounterSnapshot *out_snapshot);
void m12core_reset_counters(void);

/* Phase-3 shader introspection foundation.  These helpers deliberately do not
 * create Metal objects yet; they centralize the cheap, platform-neutral pieces
 * of shader cache keying first so later refactors can move compilation and
 * reflection without changing key semantics at the same time.
 */
int m12core_hash_shader_bytecode(const void *bytecode, uint64_t bytecode_size,
                                 uint32_t stage, M12CoreShaderBytecodeInfo *out_info);
int m12core_shader_contains_dxil(const void *bytecode, uint64_t bytecode_size,
                                 uint32_t *out_contains_dxil);
int m12core_format_shader_cache_paths(const char *cache_root, uint64_t shader_hash,
                                      M12CoreShaderCachePaths *out_paths);
int m12core_probe_shader_cache(const char *cache_root, uint64_t shader_hash,
                               uint32_t force_source_compile,
                               M12CoreShaderCacheLookup *out_lookup);
int m12core_parse_shader_reflection(const char *reflection_text, uint64_t reflection_text_size,
                                    M12CoreShaderReflectionSummary *out_summary);
int m12core_lower_dxil_to_msl(const M12CoreDXILToMSLDesc *desc,
                              char *out_source,
                              uint64_t out_source_capacity,
                              M12CoreDXILToMSLResult *out_result);
int m12core_create_shader_function(const M12CoreShaderFunctionDesc *desc,
                                   M12CoreShaderFunctionResult *out_result);
int m12core_make_pipeline_cache_key(const M12CorePipelineCacheKeyInput *input,
                                    M12CorePipelineCacheKey *out_key);

#ifdef __cplusplus
}
#endif
