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
#define M12CORE_BUILD_ID_LOW 0x4d313243u /* "M12C" marker. */
#define M12CORE_BUILD_ID_HIGH                                                  \
  0x00000015u /* Core convergence C2 shadow packet/cache recording. */

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
  M12CORE_FEATURE_SM50_REFLECTION = 1u << 5,
  M12CORE_FEATURE_PIPELINE_CACHE = 1u << 6,
  M12CORE_FEATURE_PIPELINE_CREATION = 1u << 7,
  M12CORE_FEATURE_ROOT_SIGNATURE_KEYS = 1u << 8,
  M12CORE_FEATURE_ROOT_BINDING_PLAN = 1u << 9,
  M12CORE_FEATURE_ROOT_ARGUMENT_LAYOUT = 1u << 10,
  M12CORE_FEATURE_PREWARM_PACKS = 1u << 11,
  M12CORE_FEATURE_DRAW_PLANNING = 1u << 12,
  M12CORE_FEATURE_PRESENT_PLANNING = 1u << 13,
  M12CORE_FEATURE_REPLAY_PLANNING = 1u << 14,
  M12CORE_FEATURE_COMMAND_STREAM_DESCRIPTORS = 1u << 15,
  M12CORE_FEATURE_RENDER_PASS_HAZARD_PLANNING = 1u << 16,
  M12CORE_FEATURE_PRESENT_EXECUTE_PLANNING = 1u << 17,
  M12CORE_FEATURE_REPLAY_EXECUTE_PLANNING = 1u << 18,
  M12CORE_FEATURE_COMMAND_PACKET_STREAM = 1u << 19,
  M12CORE_FEATURE_CACHE_COMPATIBILITY_KEYS = 1u << 20,
  M12CORE_FEATURE_COMMAND_PACKET_SHADOW_RECORDING = 1u << 21,
  M12CORE_FEATURE_CACHE_INDEX_SHADOW = 1u << 22,
  M12CORE_FEATURE_ALL =
      M12CORE_FEATURE_INERT_LOADER | M12CORE_FEATURE_COUNTERS |
      M12CORE_FEATURE_SHADER_INTROSPECTION | M12CORE_FEATURE_SHADER_FUNCTIONS |
      M12CORE_FEATURE_DXIL_TO_MSL | M12CORE_FEATURE_SM50_REFLECTION |
      M12CORE_FEATURE_PIPELINE_CACHE | M12CORE_FEATURE_PIPELINE_CREATION |
      M12CORE_FEATURE_ROOT_SIGNATURE_KEYS | M12CORE_FEATURE_ROOT_BINDING_PLAN |
      M12CORE_FEATURE_ROOT_ARGUMENT_LAYOUT | M12CORE_FEATURE_PREWARM_PACKS |
      M12CORE_FEATURE_DRAW_PLANNING | M12CORE_FEATURE_PRESENT_PLANNING |
      M12CORE_FEATURE_REPLAY_PLANNING |
      M12CORE_FEATURE_COMMAND_STREAM_DESCRIPTORS |
      M12CORE_FEATURE_RENDER_PASS_HAZARD_PLANNING |
      M12CORE_FEATURE_PRESENT_EXECUTE_PLANNING |
      M12CORE_FEATURE_REPLAY_EXECUTE_PLANNING |
      M12CORE_FEATURE_COMMAND_PACKET_STREAM |
      M12CORE_FEATURE_CACHE_COMPATIBILITY_KEYS |
      M12CORE_FEATURE_COMMAND_PACKET_SHADOW_RECORDING |
      M12CORE_FEATURE_CACHE_INDEX_SHADOW,
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

typedef struct M12CorePipelineKeyFields {
  uint32_t abi_version;
  uint32_t kind;
  uint64_t base_hash;
  uint64_t device_id;
  uint64_t flags;
  const uint64_t *fields;
  uint32_t field_count;
  uint32_t reserved;
} M12CorePipelineKeyFields;

typedef struct M12CorePipelineCacheKey {
  uint32_t abi_version;
  uint32_t kind;
  uint64_t key;
} M12CorePipelineCacheKey;

typedef struct M12CorePipelineCacheQuery {
  uint32_t abi_version;
  uint32_t kind;
  uint64_t key;
} M12CorePipelineCacheQuery;

typedef struct M12CorePipelineCacheResult {
  uint32_t abi_version;
  uint32_t hit;
  uint32_t kind;
  uint32_t reserved;
  uint64_t pipeline_handle;
} M12CorePipelineCacheResult;

typedef enum M12CorePipelineCreateStatus {
  M12CORE_PIPELINE_CREATE_STATUS_OK = 0,
  M12CORE_PIPELINE_CREATE_STATUS_INVALID = 1,
  M12CORE_PIPELINE_CREATE_STATUS_UNSUPPORTED_KIND = 2,
  M12CORE_PIPELINE_CREATE_STATUS_CREATE_FAILED = 3,
} M12CorePipelineCreateStatus;

typedef struct M12CorePipelineCreateDesc {
  uint32_t abi_version;
  uint32_t kind;
  uint64_t device_handle;
  uint64_t cache_key;
  const void *pipeline_info;
  uint64_t pipeline_info_size;
} M12CorePipelineCreateDesc;

typedef struct M12CorePipelineCreateResult {
  uint32_t abi_version;
  uint32_t status;
  uint32_t kind;
  uint32_t cache_hit;
  uint64_t pipeline_handle;
  uint64_t error_handle;
} M12CorePipelineCreateResult;

typedef enum M12CoreRootSignatureStatus {
  M12CORE_ROOT_SIGNATURE_STATUS_OK = 0,
  M12CORE_ROOT_SIGNATURE_STATUS_INVALID = 1,
} M12CoreRootSignatureStatus;

typedef struct M12CoreRootSignatureDesc {
  uint32_t abi_version;
  uint32_t parameter_count;
  uint32_t static_sampler_count;
  uint32_t flags;
  uint64_t blob_hash;
  const uint64_t *fields;
  uint32_t field_count;
  uint32_t reserved;
} M12CoreRootSignatureDesc;

typedef struct M12CoreRootSignatureSummary {
  uint32_t abi_version;
  uint32_t status;
  uint32_t parameter_count;
  uint32_t descriptor_table_count;
  uint32_t descriptor_range_count;
  uint32_t root_descriptor_count;
  uint32_t root_constant_count;
  uint32_t static_sampler_count;
  uint64_t root_signature_key;
} M12CoreRootSignatureSummary;

typedef struct M12CoreRootBindingParameter {
  uint32_t type;
  uint32_t shader_visibility;
  uint32_t register_space;
  uint32_t register_index;
  uint32_t num_descriptors;
  uint32_t num_32bit_values;
  uint32_t descriptor_flags;
  uint32_t range_start;
  uint32_t range_count;
  uint32_t reserved[3];
} M12CoreRootBindingParameter;

typedef struct M12CoreRootBindingRange {
  uint32_t range_type;
  uint32_t num_descriptors;
  uint32_t base_register;
  uint32_t register_space;
  uint32_t offset_in_table;
  uint32_t flags;
} M12CoreRootBindingRange;

typedef struct M12CoreStaticSamplerBinding {
  uint32_t shader_register;
  uint32_t register_space;
  uint32_t shader_visibility;
  uint32_t reserved;
} M12CoreStaticSamplerBinding;

typedef struct M12CoreRootBindingPlanDesc {
  uint32_t abi_version;
  uint32_t flags;
  uint64_t root_signature_key;
  const M12CoreRootBindingParameter *parameters;
  uint32_t parameter_count;
  uint32_t reserved0;
  const M12CoreRootBindingRange *ranges;
  uint32_t range_count;
  uint32_t reserved1;
  const M12CoreStaticSamplerBinding *static_samplers;
  uint32_t static_sampler_count;
  uint32_t reserved2;
} M12CoreRootBindingPlanDesc;

typedef struct M12CoreRootBindingPlanSummary {
  uint32_t abi_version;
  uint32_t status;
  uint32_t parameter_count;
  uint32_t descriptor_table_count;
  uint32_t descriptor_range_count;
  uint32_t root_descriptor_count;
  uint32_t root_constant_count;
  uint32_t static_sampler_count;
  uint32_t resource_range_count;
  uint32_t sampler_range_count;
  uint32_t unbounded_range_count;
  uint32_t visibility_specific_count;
  uint32_t register_space_count;
  uint32_t max_descriptor_table_span;
  uint32_t argument_resource_slot_count;
  uint32_t argument_sampler_slot_count;
  uint32_t argument_root_descriptor_slot_count;
  uint32_t argument_root_constant_dword_count;
  uint32_t argument_visibility_mask;
  uint32_t argument_layout_reserved;
  uint64_t binding_plan_key;
} M12CoreRootBindingPlanSummary;

typedef enum M12CoreRootBindingLookupKind {
  M12CORE_ROOT_BINDING_LOOKUP_DESCRIPTOR_RANGE = 1,
  M12CORE_ROOT_BINDING_LOOKUP_STATIC_SAMPLER = 2,
  M12CORE_ROOT_BINDING_LOOKUP_ROOT_DESCRIPTOR = 3,
  M12CORE_ROOT_BINDING_LOOKUP_ROOT_CONSTANTS = 4,
} M12CoreRootBindingLookupKind;

typedef struct M12CoreRootBindingLookupDesc {
  uint32_t abi_version;
  uint32_t lookup_kind;
  uint32_t range_type;
  uint32_t shader_register;
  uint32_t register_space;
  uint32_t shader_visibility;
  const M12CoreRootBindingParameter *parameters;
  uint32_t parameter_count;
  uint32_t reserved0;
  const M12CoreRootBindingRange *ranges;
  uint32_t range_count;
  uint32_t reserved1;
  const M12CoreStaticSamplerBinding *static_samplers;
  uint32_t static_sampler_count;
  uint32_t reserved2;
} M12CoreRootBindingLookupDesc;

typedef struct M12CoreRootBindingLookupResult {
  uint32_t abi_version;
  uint32_t status;
  uint32_t found;
  uint32_t root_parameter_index;
  uint32_t range_index;
  uint32_t descriptor_offset;
  uint32_t visibility_fallback;
  uint32_t reserved;
} M12CoreRootBindingLookupResult;

typedef enum M12CorePrewarmPackStatus {
  M12CORE_PREWARM_PACK_STATUS_OK = 0,
  M12CORE_PREWARM_PACK_STATUS_INVALID = 1,
} M12CorePrewarmPackStatus;

typedef enum M12CorePrewarmPackFlags {
  M12CORE_PREWARM_PACK_OFFLINE_PROFILE_GATED = 1u << 0,
  M12CORE_PREWARM_PACK_HAS_PREWARM_ORDER = 1u << 1,
} M12CorePrewarmPackFlags;

/* Compact prewarm packs use dense stage-mask bits so masks remain stable even
 * if the public M12CoreShaderStage enum grows append-only values later.
 */
typedef enum M12CorePrewarmStageMask {
  M12CORE_PREWARM_STAGE_MASK_VERTEX = 1u << 0,
  M12CORE_PREWARM_STAGE_MASK_PIXEL = 1u << 1,
  M12CORE_PREWARM_STAGE_MASK_GEOMETRY = 1u << 2,
  M12CORE_PREWARM_STAGE_MASK_HULL = 1u << 3,
  M12CORE_PREWARM_STAGE_MASK_DOMAIN = 1u << 4,
  M12CORE_PREWARM_STAGE_MASK_COMPUTE = 1u << 5,
  M12CORE_PREWARM_STAGE_MASK_UNKNOWN = 1u << 31,
} M12CorePrewarmStageMask;

typedef struct M12CorePrewarmStageRecord {
  uint32_t stage;
  uint32_t reserved;
  uint64_t shader_key;
  uint64_t shader_bytecode_hash;
  uint64_t root_structural_hash;
} M12CorePrewarmStageRecord;

typedef struct M12CorePrewarmPipelineRecord {
  uint64_t pipeline_key;
  uint64_t root_signature_key;
  uint64_t root_structural_hash;
  uint32_t stage_mask;
  uint32_t prewarm_order;
  uint32_t stage_start;
  uint32_t stage_count;
  uint32_t argument_resource_slot_count;
  uint32_t argument_sampler_slot_count;
  uint32_t argument_root_descriptor_slot_count;
  uint32_t argument_root_constant_dword_count;
  uint32_t expected_layout_flags;
  uint32_t reserved;
} M12CorePrewarmPipelineRecord;

typedef struct M12CorePrewarmPackDesc {
  uint32_t abi_version;
  uint32_t flags;
  uint64_t appid;
  uint64_t profile_key;
  uint64_t source_pack_key;
  const M12CorePrewarmPipelineRecord *pipelines;
  uint32_t pipeline_count;
  uint32_t reserved0;
  const M12CorePrewarmStageRecord *stages;
  uint32_t stage_count;
  uint32_t reserved1;
} M12CorePrewarmPackDesc;

typedef struct M12CorePrewarmPackSummary {
  uint32_t abi_version;
  uint32_t status;
  uint32_t pipeline_count;
  uint32_t stage_link_count;
  uint32_t render_pipeline_count;
  uint32_t compute_pipeline_count;
  uint32_t unique_root_count;
  uint32_t unique_shader_count;
  uint32_t ordered_pipeline_count;
  uint32_t max_stage_links_per_pipeline;
  uint32_t expected_resource_slots;
  uint32_t expected_sampler_slots;
  uint32_t expected_root_descriptor_slots;
  uint32_t expected_root_constant_dwords;
  uint64_t prewarm_pack_key;
} M12CorePrewarmPackSummary;

typedef enum M12CoreDrawPlanStatus {
  M12CORE_DRAW_PLAN_STATUS_OK = 0,
  M12CORE_DRAW_PLAN_STATUS_INVALID = 1,
} M12CoreDrawPlanStatus;

typedef enum M12CoreDrawPlanFlags {
  M12CORE_DRAW_PLAN_INDEXED = 1u << 0,
  M12CORE_DRAW_PLAN_INDIRECT = 1u << 1,
  M12CORE_DRAW_PLAN_HAS_GRAPHICS_PSO = 1u << 2,
  M12CORE_DRAW_PLAN_HAS_ROOT_SIGNATURE = 1u << 3,
  M12CORE_DRAW_PLAN_HAS_RENDER_TARGET = 1u << 4,
  M12CORE_DRAW_PLAN_HAS_DEPTH_STENCIL = 1u << 5,
} M12CoreDrawPlanFlags;

typedef struct M12CoreDrawPlanDesc {
  uint32_t abi_version;
  uint32_t flags;
  uint64_t pso_key;
  uint64_t root_signature_key;
  uint64_t binding_plan_key;
  uint32_t root_parameter_count;
  uint32_t descriptor_table_count;
  uint32_t root_descriptor_count;
  uint32_t root_constant_count;
  uint32_t argument_resource_slot_count;
  uint32_t argument_sampler_slot_count;
  uint32_t argument_root_descriptor_slot_count;
  uint32_t argument_root_constant_dword_count;
  uint32_t render_target_count;
  uint32_t resource_barrier_count;
  uint32_t descriptor_heap_count;
  uint32_t reserved;
} M12CoreDrawPlanDesc;

typedef struct M12CoreDrawPlanSummary {
  uint32_t abi_version;
  uint32_t status;
  uint32_t flags;
  uint32_t validation_flags;
  uint32_t resource_usage_count;
  uint32_t binding_validation_count;
  uint32_t redundant_binding_candidate_count;
  uint32_t render_pass_attachment_count;
  uint32_t descriptor_pressure_score;
  uint32_t reserved;
  uint64_t draw_plan_key;
} M12CoreDrawPlanSummary;

typedef enum M12CorePresentPlanStatus {
  M12CORE_PRESENT_PLAN_STATUS_OK = 0,
  M12CORE_PRESENT_PLAN_STATUS_INVALID = 1,
} M12CorePresentPlanStatus;

typedef enum M12CorePresentPlanFlags {
  M12CORE_PRESENT_PLAN_HAS_BACKBUFFER = 1u << 0,
  M12CORE_PRESENT_PLAN_HAS_SOURCE_TEXTURE = 1u << 1,
  M12CORE_PRESENT_PLAN_HAS_DRAWABLE = 1u << 2,
  M12CORE_PRESENT_PLAN_USES_PRESENTER = 1u << 3,
  M12CORE_PRESENT_PLAN_USES_RAW_BLIT = 1u << 4,
  M12CORE_PRESENT_PLAN_WAITED_FOR_RENDER = 1u << 5,
  M12CORE_PRESENT_PLAN_LIVE_PRESENT = 1u << 6,
  M12CORE_PRESENT_PLAN_READBACK_REQUESTED = 1u << 7,
} M12CorePresentPlanFlags;

typedef struct M12CorePresentPlanDesc {
  uint32_t abi_version;
  uint32_t flags;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t buffer_index;
  uint32_t buffer_count;
  uint32_t sync_interval;
  uint32_t present_flags;
  uint32_t work_classification;
  uint32_t command_buffer_status;
  uint32_t reserved0;
  uint64_t present_count;
  uint64_t source_texture_key;
  uint64_t drawable_texture_key;
  uint64_t queue_serial;
  uint64_t command_count;
  uint64_t draw_count;
  uint64_t dispatch_count;
  uint64_t clear_count;
  uint64_t wait_seq;
} M12CorePresentPlanDesc;

typedef struct M12CorePresentPlanSummary {
  uint32_t abi_version;
  uint32_t status;
  uint32_t flags;
  uint32_t validation_flags;
  uint32_t present_path;
  uint32_t work_classification;
  uint32_t hazard_score;
  uint32_t reserved;
  uint64_t present_plan_key;
  uint64_t scheduled_work_count;
} M12CorePresentPlanSummary;

typedef enum M12CorePresentExecuteStatus {
  M12CORE_PRESENT_EXECUTE_STATUS_OK = 0,
  M12CORE_PRESENT_EXECUTE_STATUS_INVALID = 1,
} M12CorePresentExecuteStatus;

typedef enum M12CorePresentExecuteFlags {
  M12CORE_PRESENT_EXECUTE_HAS_SOURCE_TEXTURE = 1u << 0,
  M12CORE_PRESENT_EXECUTE_HAS_DRAWABLE = 1u << 1,
  M12CORE_PRESENT_EXECUTE_USES_RAW_BLIT = 1u << 2,
  M12CORE_PRESENT_EXECUTE_USES_PRESENTER = 1u << 3,
  M12CORE_PRESENT_EXECUTE_LIVE_PRESENT = 1u << 4,
  M12CORE_PRESENT_EXECUTE_READBACK_REQUESTED = 1u << 5,
  M12CORE_PRESENT_EXECUTE_GATE_ENABLED = 1u << 6,
  M12CORE_PRESENT_EXECUTE_WAITED_FOR_RENDER = 1u << 7,
  M12CORE_PRESENT_EXECUTE_FORMAT_SUPPORTED = 1u << 8,
} M12CorePresentExecuteFlags;

typedef enum M12CorePresentExecuteFallbackReason {
  M12CORE_PRESENT_EXECUTE_FALLBACK_NONE = 0,
  M12CORE_PRESENT_EXECUTE_FALLBACK_GATE_DISABLED = 1,
  M12CORE_PRESENT_EXECUTE_FALLBACK_MISSING_SOURCE = 2,
  M12CORE_PRESENT_EXECUTE_FALLBACK_MISSING_DRAWABLE = 3,
  M12CORE_PRESENT_EXECUTE_FALLBACK_NON_RAW_PATH = 4,
  M12CORE_PRESENT_EXECUTE_FALLBACK_DIAGNOSTIC_ACTIVE = 5,
  M12CORE_PRESENT_EXECUTE_FALLBACK_ZERO_EXTENT = 6,
  M12CORE_PRESENT_EXECUTE_FALLBACK_UNSUPPORTED_FORMAT = 7,
} M12CorePresentExecuteFallbackReason;

typedef enum M12CorePresentExecuteSummaryFlags {
  M12CORE_PRESENT_EXECUTE_SUMMARY_SUPPORTED = 1u << 0,
  M12CORE_PRESENT_EXECUTE_SUMMARY_GATE_ENABLED = 1u << 1,
  M12CORE_PRESENT_EXECUTE_SUMMARY_EXECUTED = 1u << 2,
} M12CorePresentExecuteSummaryFlags;

typedef struct M12CorePresentExecuteDesc {
  uint32_t abi_version;
  uint32_t flags;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t buffer_index;
  uint32_t buffer_count;
  uint32_t sync_interval;
  uint32_t present_flags;
  uint32_t work_classification;
  uint32_t command_buffer_status;
  uint32_t reserved0;
  uint64_t present_count;
  uint64_t source_texture_key;
  uint64_t drawable_texture_key;
  uint64_t queue_serial;
  uint64_t command_count;
  uint64_t draw_count;
  uint64_t dispatch_count;
  uint64_t clear_count;
  uint64_t wait_seq;
} M12CorePresentExecuteDesc;

typedef struct M12CorePresentExecuteSummary {
  uint32_t abi_version;
  uint32_t status;
  uint32_t flags;
  uint32_t fallback_reason;
  uint32_t validation_flags;
  uint32_t planned_operation_count;
  uint32_t hazard_score;
  uint32_t reserved;
  uint64_t present_execute_key;
  uint64_t scheduled_work_count;
} M12CorePresentExecuteSummary;

typedef enum M12CoreReplayPlanStatus {
  M12CORE_REPLAY_PLAN_STATUS_OK = 0,
  M12CORE_REPLAY_PLAN_STATUS_INVALID = 1,
} M12CoreReplayPlanStatus;

typedef enum M12CoreReplayPlanFlags {
  M12CORE_REPLAY_PLAN_HAS_COMMAND_STREAM = 1u << 0,
  M12CORE_REPLAY_PLAN_HAS_GRAPHICS_WORK = 1u << 1,
  M12CORE_REPLAY_PLAN_HAS_COMPUTE_WORK = 1u << 2,
  M12CORE_REPLAY_PLAN_HAS_CLEAR_WORK = 1u << 3,
  M12CORE_REPLAY_PLAN_HAS_SWAPCHAIN_WORK = 1u << 4,
  M12CORE_REPLAY_PLAN_HAS_SWAPCHAIN_TARGET = 1u << 5,
  M12CORE_REPLAY_PLAN_SYNC_EXECUTE = 1u << 6,
  M12CORE_REPLAY_PLAN_COMMAND_BUFFER_COMPLETED = 1u << 7,
} M12CoreReplayPlanFlags;

typedef struct M12CoreReplayPlanDesc {
  uint32_t abi_version;
  uint32_t flags;
  uint32_t queue_type;
  uint32_t command_list_index;
  uint32_t command_buffer_status;
  uint32_t reserved0;
  uint64_t command_list_id;
  uint64_t queue_serial;
  uint64_t command_count;
  uint64_t draw_count;
  uint64_t indexed_draw_count;
  uint64_t indirect_count;
  uint64_t dispatch_count;
  uint64_t clear_rtv_count;
  uint64_t clear_dsv_count;
  uint64_t clear_uav_count;
  int64_t replay_ms;
  int64_t wait_ms;
} M12CoreReplayPlanDesc;

typedef struct M12CoreReplayPlanSummary {
  uint32_t abi_version;
  uint32_t status;
  uint32_t flags;
  uint32_t validation_flags;
  uint32_t work_classification;
  uint32_t execution_path;
  uint32_t hazard_score;
  uint32_t reserved;
  uint64_t replay_plan_key;
  uint64_t scheduled_work_count;
} M12CoreReplayPlanSummary;

typedef enum M12CoreReplayExecuteStatus {
  M12CORE_REPLAY_EXECUTE_STATUS_OK = 0,
  M12CORE_REPLAY_EXECUTE_STATUS_INVALID = 1,
} M12CoreReplayExecuteStatus;

typedef enum M12CoreReplayExecuteFlags {
  M12CORE_REPLAY_EXECUTE_GATE_ENABLED = 1u << 0,
  M12CORE_REPLAY_EXECUTE_HAS_COMMAND_STREAM = 1u << 1,
  M12CORE_REPLAY_EXECUTE_HAS_GRAPHICS_WORK = 1u << 2,
  M12CORE_REPLAY_EXECUTE_HAS_COMPUTE_WORK = 1u << 3,
  M12CORE_REPLAY_EXECUTE_HAS_CLEAR_WORK = 1u << 4,
  M12CORE_REPLAY_EXECUTE_HAS_BARRIERS = 1u << 5,
  M12CORE_REPLAY_EXECUTE_HAS_ROOT_BINDING = 1u << 6,
  M12CORE_REPLAY_EXECUTE_HAS_DESCRIPTOR_HEAPS = 1u << 7,
  M12CORE_REPLAY_EXECUTE_HAS_INDIRECT = 1u << 8,
  M12CORE_REPLAY_EXECUTE_HAS_CORRUPTION = 1u << 9,
  M12CORE_REPLAY_EXECUTE_HAS_SWAPCHAIN_TARGET = 1u << 10,
} M12CoreReplayExecuteFlags;

typedef enum M12CoreReplayExecuteFallbackReason {
  M12CORE_REPLAY_EXECUTE_FALLBACK_NONE = 0,
  M12CORE_REPLAY_EXECUTE_FALLBACK_GATE_DISABLED = 1,
  M12CORE_REPLAY_EXECUTE_FALLBACK_EMPTY_STREAM = 2,
  M12CORE_REPLAY_EXECUTE_FALLBACK_UNSUPPORTED_INDIRECT = 3,
  M12CORE_REPLAY_EXECUTE_FALLBACK_CORRUPT_STREAM = 4,
  M12CORE_REPLAY_EXECUTE_FALLBACK_UNSUPPORTED_SHAPE = 5,
} M12CoreReplayExecuteFallbackReason;

typedef enum M12CoreReplayExecuteSummaryFlags {
  M12CORE_REPLAY_EXECUTE_SUMMARY_ELIGIBLE = 1u << 0,
  M12CORE_REPLAY_EXECUTE_SUMMARY_GATE_ENABLED = 1u << 1,
} M12CoreReplayExecuteSummaryFlags;

typedef struct M12CoreReplayExecuteDesc {
  uint32_t abi_version;
  uint32_t flags;
  uint32_t queue_type;
  uint32_t command_list_index;
  uint32_t command_count;
  uint32_t draw_count;
  uint32_t indexed_draw_count;
  uint32_t dispatch_count;
  uint32_t clear_count;
  uint32_t indirect_count;
  uint32_t barrier_count;
  uint32_t root_binding_count;
  uint32_t descriptor_heap_count;
  uint32_t render_target_count;
  uint32_t command_buffer_status;
  uint32_t reserved0;
  uint64_t command_list_id;
  uint64_t queue_serial;
} M12CoreReplayExecuteDesc;

typedef struct M12CoreReplayExecuteSummary {
  uint32_t abi_version;
  uint32_t status;
  uint32_t flags;
  uint32_t fallback_reason;
  uint32_t validation_flags;
  uint32_t supported_command_mask;
  uint32_t unsupported_command_mask;
  uint32_t planned_packet_count;
  uint32_t replay_classification;
  uint32_t hazard_score;
  uint32_t reserved[2];
  uint64_t replay_execute_key;
  uint64_t scheduled_work_count;
} M12CoreReplayExecuteSummary;

typedef enum M12CoreCommandStreamStatus {
  M12CORE_COMMAND_STREAM_STATUS_OK = 0,
  M12CORE_COMMAND_STREAM_STATUS_INVALID = 1,
} M12CoreCommandStreamStatus;

typedef enum M12CoreCommandStreamFlags {
  M12CORE_COMMAND_STREAM_HAS_COMMANDS = 1u << 0,
  M12CORE_COMMAND_STREAM_HAS_GRAPHICS_WORK = 1u << 1,
  M12CORE_COMMAND_STREAM_HAS_COMPUTE_WORK = 1u << 2,
  M12CORE_COMMAND_STREAM_HAS_CLEAR_WORK = 1u << 3,
  M12CORE_COMMAND_STREAM_HAS_GRAPHICS_SETUP = 1u << 4,
  M12CORE_COMMAND_STREAM_HAS_COMPUTE_SETUP = 1u << 5,
  M12CORE_COMMAND_STREAM_HAS_RENDER_TARGETS = 1u << 6,
  M12CORE_COMMAND_STREAM_HAS_DSV = 1u << 7,
  M12CORE_COMMAND_STREAM_HAS_DESCRIPTOR_HEAPS = 1u << 8,
  M12CORE_COMMAND_STREAM_HAS_SWAPCHAIN_TOUCH = 1u << 9,
  M12CORE_COMMAND_STREAM_HAS_SWAPCHAIN_TARGET = 1u << 10,
  M12CORE_COMMAND_STREAM_CORRUPT = 1u << 11,
} M12CoreCommandStreamFlags;

typedef struct M12CoreCommandStreamDesc {
  uint32_t abi_version;
  uint32_t flags;
  uint32_t queue_type;
  uint32_t command_list_index;
  uint32_t command_buffer_status;
  uint32_t command_count;
  uint32_t draw_count;
  uint32_t indexed_draw_count;
  uint32_t indirect_count;
  uint32_t dispatch_count;
  uint32_t clear_rtv_count;
  uint32_t clear_dsv_count;
  uint32_t clear_uav_count;
  uint32_t set_pso_count;
  uint32_t set_graphics_root_sig_count;
  uint32_t set_graphics_root_binding_count;
  uint32_t set_compute_root_sig_count;
  uint32_t set_compute_root_binding_count;
  uint32_t om_set_render_targets_count;
  uint32_t ia_binding_count;
  uint32_t viewport_scissor_count;
  uint32_t final_render_target_count;
  uint32_t final_has_dsv;
  uint32_t final_descriptor_heap_count;
  uint32_t swapchain_touched_count;
  uint32_t reserved0;
  uint64_t command_list_id;
  uint64_t queue_serial;
} M12CoreCommandStreamDesc;

typedef struct M12CoreCommandStreamSummary {
  uint32_t abi_version;
  uint32_t status;
  uint32_t input_flags;
  uint32_t expected_flags;
  uint32_t drift_flags;
  uint32_t work_classification;
  uint32_t execution_path;
  uint32_t descriptor_pressure_score;
  uint32_t attachment_count;
  uint32_t binding_change_count;
  uint32_t reserved[2];
  uint64_t command_stream_key;
  uint64_t scheduled_work_count;
} M12CoreCommandStreamSummary;

typedef enum M12CoreRenderPassPlanStatus {
  M12CORE_RENDER_PASS_PLAN_STATUS_OK = 0,
  M12CORE_RENDER_PASS_PLAN_STATUS_INVALID = 1,
} M12CoreRenderPassPlanStatus;

typedef enum M12CoreRenderPassPlanFlags {
  M12CORE_RENDER_PASS_PLAN_HAS_RENDER_TARGETS = 1u << 0,
  M12CORE_RENDER_PASS_PLAN_HAS_DSV = 1u << 1,
  M12CORE_RENDER_PASS_PLAN_HAS_SWAPCHAIN_TARGET = 1u << 2,
  M12CORE_RENDER_PASS_PLAN_HAS_SWAPCHAIN_TOUCH = 1u << 3,
  M12CORE_RENDER_PASS_PLAN_HAS_DRAW_WORK = 1u << 4,
  M12CORE_RENDER_PASS_PLAN_HAS_CLEAR_WORK = 1u << 5,
  M12CORE_RENDER_PASS_PLAN_HAS_COMPUTE_WORK = 1u << 6,
  M12CORE_RENDER_PASS_PLAN_HAS_BARRIERS = 1u << 7,
  M12CORE_RENDER_PASS_PLAN_HAS_DESCRIPTOR_HEAPS = 1u << 8,
} M12CoreRenderPassPlanFlags;

typedef struct M12CoreRenderPassPlanDesc {
  uint32_t abi_version;
  uint32_t flags;
  uint32_t queue_type;
  uint32_t command_list_index;
  uint32_t render_target_count;
  uint32_t dsv_format;
  uint32_t rtv0_format;
  uint32_t rtv_format_xor;
  uint32_t draw_count;
  uint32_t clear_count;
  uint32_t dispatch_count;
  uint32_t resource_barrier_count;
  uint32_t descriptor_heap_count;
  uint32_t swapchain_touched_count;
  uint32_t command_buffer_status;
  uint32_t reserved0;
  uint64_t command_list_id;
  uint64_t queue_serial;
} M12CoreRenderPassPlanDesc;

typedef struct M12CoreRenderPassPlanSummary {
  uint32_t abi_version;
  uint32_t status;
  uint32_t input_flags;
  uint32_t expected_flags;
  uint32_t drift_flags;
  uint32_t render_pass_classification;
  uint32_t hazard_score;
  uint32_t attachment_count;
  uint32_t resource_transition_count;
  uint32_t descriptor_pressure_score;
  uint32_t reserved[2];
  uint64_t render_pass_plan_key;
} M12CoreRenderPassPlanSummary;

typedef enum M12CoreSM50ReflectionStatus {
  M12CORE_SM50_REFLECTION_STATUS_OK = 0,
  M12CORE_SM50_REFLECTION_STATUS_INVALID = 1,
  M12CORE_SM50_REFLECTION_STATUS_INIT_FAILED = 2,
  M12CORE_SM50_REFLECTION_STATUS_OUTPUT_TOO_SMALL = 3,
} M12CoreSM50ReflectionStatus;

typedef struct M12CoreSM50ShaderArgument {
  uint32_t type;
  uint32_t binding_slot;
  uint32_t register_space;
  uint32_t flags;
  uint32_t structure_ptr_offset;
  uint32_t size_in_vec4;
} M12CoreSM50ShaderArgument;

typedef struct M12CoreSM50ShaderReflection {
  uint32_t abi_version;
  uint32_t constant_buffer_table_bind_index;
  uint32_t argument_buffer_bind_index;
  uint32_t num_constant_buffers;
  uint32_t num_arguments;
  uint32_t stage_payload[3];
  uint16_t constant_buffer_slot_mask;
  uint16_t sampler_slot_mask;
  uint64_t uav_slot_mask;
  uint64_t srv_slot_mask_hi;
  uint64_t srv_slot_mask_lo;
  uint32_t num_output_element;
  uint32_t threads_per_patch;
  uint32_t argument_table_qwords;
} M12CoreSM50ShaderReflection;

typedef struct M12CoreSM50ReflectionResult {
  uint32_t abi_version;
  uint32_t status;
  uint32_t required_constant_buffers;
  uint32_t required_arguments;
} M12CoreSM50ReflectionResult;

/* Core Convergence C1 packet ABI.  These packets are not an execution API yet:
 * they are the scalar command-stream description that later C slices will feed
 * from PE command recording into libm12core validation, cache lookup, and then
 * gated native execution.  Keep every field POD/scalar and append-only.
 */
typedef enum M12CoreCommandPacketKind {
  M12CORE_COMMAND_PACKET_KIND_UNKNOWN = 0,
  M12CORE_COMMAND_PACKET_KIND_SET_PIPELINE = 1,
  M12CORE_COMMAND_PACKET_KIND_SET_ROOT_SIGNATURE = 2,
  M12CORE_COMMAND_PACKET_KIND_SET_DESCRIPTOR_HEAP = 3,
  M12CORE_COMMAND_PACKET_KIND_SET_ROOT_BINDING = 4,
  M12CORE_COMMAND_PACKET_KIND_SET_RENDER_TARGETS = 5,
  M12CORE_COMMAND_PACKET_KIND_RESOURCE_BARRIER = 6,
  M12CORE_COMMAND_PACKET_KIND_CLEAR_RTV = 7,
  M12CORE_COMMAND_PACKET_KIND_CLEAR_DSV = 8,
  M12CORE_COMMAND_PACKET_KIND_CLEAR_UAV = 9,
  M12CORE_COMMAND_PACKET_KIND_DRAW = 10,
  M12CORE_COMMAND_PACKET_KIND_DRAW_INDEXED = 11,
  M12CORE_COMMAND_PACKET_KIND_DISPATCH = 12,
  M12CORE_COMMAND_PACKET_KIND_COPY = 13,
  M12CORE_COMMAND_PACKET_KIND_PRESENT = 14,
} M12CoreCommandPacketKind;

typedef enum M12CoreCommandPacketFlags {
  M12CORE_COMMAND_PACKET_FLAG_GRAPHICS = 1u << 0,
  M12CORE_COMMAND_PACKET_FLAG_COMPUTE = 1u << 1,
  M12CORE_COMMAND_PACKET_FLAG_COPY = 1u << 2,
  M12CORE_COMMAND_PACKET_FLAG_PRESENT = 1u << 3,
  M12CORE_COMMAND_PACKET_FLAG_USES_RESOURCE = 1u << 4,
  M12CORE_COMMAND_PACKET_FLAG_USES_PIPELINE = 1u << 5,
  M12CORE_COMMAND_PACKET_FLAG_USES_ROOT_SIGNATURE = 1u << 6,
  M12CORE_COMMAND_PACKET_FLAG_USES_BINDINGS = 1u << 7,
  M12CORE_COMMAND_PACKET_FLAG_UNSUPPORTED = 1u << 8,
} M12CoreCommandPacketFlags;

typedef enum M12CoreCommandPacketStreamStatus {
  M12CORE_COMMAND_PACKET_STREAM_STATUS_OK = 0,
  M12CORE_COMMAND_PACKET_STREAM_STATUS_INVALID = 1,
  M12CORE_COMMAND_PACKET_STREAM_STATUS_UNSUPPORTED = 2,
} M12CoreCommandPacketStreamStatus;

typedef enum M12CoreCommandPacketStreamSummaryFlags {
  M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_GRAPHICS = 1u << 0,
  M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_COMPUTE = 1u << 1,
  M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_COPY = 1u << 2,
  M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_PRESENT = 1u << 3,
  M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_BARRIERS = 1u << 4,
  M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_UNSUPPORTED = 1u << 5,
  M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_INVALID = 1u << 6,
} M12CoreCommandPacketStreamSummaryFlags;

typedef struct M12CoreCommandPacketHeader {
  uint32_t abi_version;
  uint32_t kind;
  uint32_t flags;
  uint32_t payload_qwords;
  uint64_t sequence;
} M12CoreCommandPacketHeader;

typedef struct M12CoreCommandPacket {
  M12CoreCommandPacketHeader header;
  uint64_t object_id0;
  uint64_t object_id1;
  uint64_t object_id2;
  uint64_t object_id3;
  uint64_t value0;
  uint64_t value1;
  uint64_t value2;
  uint64_t value3;
} M12CoreCommandPacket;

typedef struct M12CoreCommandPacketStreamDesc {
  uint32_t abi_version;
  uint32_t packet_count;
  uint32_t queue_type;
  uint32_t command_list_index;
  uint64_t command_list_id;
  uint64_t queue_serial;
  const M12CoreCommandPacket *packets;
} M12CoreCommandPacketStreamDesc;

typedef struct M12CoreCommandPacketStreamSummary {
  uint32_t abi_version;
  uint32_t status;
  uint32_t summary_flags;
  uint32_t unsupported_packet_count;
  uint32_t invalid_packet_count;
  uint32_t graphics_packet_count;
  uint32_t compute_packet_count;
  uint32_t copy_packet_count;
  uint32_t present_packet_count;
  uint32_t barrier_packet_count;
  uint32_t draw_packet_count;
  uint32_t dispatch_packet_count;
  uint32_t clear_packet_count;
  uint32_t binding_packet_count;
  uint32_t reserved[2];
  uint64_t stream_key;
  uint64_t packet_sequence_xor;
} M12CoreCommandPacketStreamSummary;

/* Core Convergence C1 cache compatibility schema.  A compatible warm-cache hit
 * must match every required dimension; missing required dimensions make the key
 * invalid so stale artifacts cannot bypass layout or runtime validation.
 */
typedef enum M12CoreCacheArtifactKind {
  M12CORE_CACHE_ARTIFACT_UNKNOWN = 0,
  M12CORE_CACHE_ARTIFACT_SHADER_FUNCTION = 1,
  M12CORE_CACHE_ARTIFACT_RENDER_PSO = 2,
  M12CORE_CACHE_ARTIFACT_COMPUTE_PSO = 3,
  M12CORE_CACHE_ARTIFACT_ROOT_BINDING_PLAN = 4,
  M12CORE_CACHE_ARTIFACT_PREWARM_PACK = 5,
} M12CoreCacheArtifactKind;

typedef enum M12CoreCacheCompatibilityFlags {
  M12CORE_CACHE_COMPAT_HAS_APPID = 1u << 0,
  M12CORE_CACHE_COMPAT_HAS_DEVICE = 1u << 1,
  M12CORE_CACHE_COMPAT_HAS_RUNTIME = 1u << 2,
  M12CORE_CACHE_COMPAT_HAS_TRANSLATOR = 1u << 3,
  M12CORE_CACHE_COMPAT_HAS_ROOT_BINDING = 1u << 4,
  M12CORE_CACHE_COMPAT_HAS_PIPELINE = 1u << 5,
  M12CORE_CACHE_COMPAT_HAS_METAL = 1u << 6,
  M12CORE_CACHE_COMPAT_HAS_SCHEMA = 1u << 7,
} M12CoreCacheCompatibilityFlags;

typedef enum M12CoreCacheCompatibilityStatus {
  M12CORE_CACHE_COMPAT_STATUS_OK = 0,
  M12CORE_CACHE_COMPAT_STATUS_INVALID = 1,
  M12CORE_CACHE_COMPAT_STATUS_MISSING_DIMENSION = 2,
} M12CoreCacheCompatibilityStatus;

typedef struct M12CoreCacheCompatibilityDesc {
  uint32_t abi_version;
  uint32_t artifact_kind;
  uint32_t flags;
  uint32_t schema_version;
  uint64_t appid;
  uint64_t profile_key;
  uint64_t device_key;
  uint64_t os_metal_key;
  uint64_t core_build_key;
  uint64_t core_feature_flags;
  uint64_t translator_key;
  uint64_t root_binding_key;
  uint64_t pipeline_key;
  uint64_t artifact_key;
} M12CoreCacheCompatibilityDesc;

typedef struct M12CoreCacheCompatibilityKey {
  uint32_t abi_version;
  uint32_t status;
  uint32_t missing_flags;
  uint32_t artifact_kind;
  uint64_t compatibility_key;
  uint64_t invalidation_key;
} M12CoreCacheCompatibilityKey;

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
                                 uint32_t stage,
                                 M12CoreShaderBytecodeInfo *out_info);
int m12core_shader_contains_dxil(const void *bytecode, uint64_t bytecode_size,
                                 uint32_t *out_contains_dxil);
int m12core_format_shader_cache_paths(const char *cache_root,
                                      uint64_t shader_hash,
                                      M12CoreShaderCachePaths *out_paths);
int m12core_probe_shader_cache(const char *cache_root, uint64_t shader_hash,
                               uint32_t force_source_compile,
                               M12CoreShaderCacheLookup *out_lookup);
int m12core_parse_shader_reflection(
    const char *reflection_text, uint64_t reflection_text_size,
    M12CoreShaderReflectionSummary *out_summary);
int m12core_lower_dxil_to_msl(const M12CoreDXILToMSLDesc *desc,
                              char *out_source, uint64_t out_source_capacity,
                              M12CoreDXILToMSLResult *out_result);
int m12core_create_shader_function(const M12CoreShaderFunctionDesc *desc,
                                   M12CoreShaderFunctionResult *out_result);
int m12core_reflect_sm50_shader(const void *bytecode, uint64_t bytecode_size,
                                uint32_t options,
                                M12CoreSM50ShaderReflection *out_reflection,
                                M12CoreSM50ShaderArgument *out_constant_buffers,
                                uint32_t constant_buffer_capacity,
                                M12CoreSM50ShaderArgument *out_arguments,
                                uint32_t argument_capacity,
                                M12CoreSM50ReflectionResult *out_result);
int m12core_make_pipeline_cache_key(const M12CorePipelineCacheKeyInput *input,
                                    M12CorePipelineCacheKey *out_key);
int m12core_make_pipeline_cache_key_from_fields(
    const M12CorePipelineKeyFields *input, M12CorePipelineCacheKey *out_key);
int m12core_lookup_pipeline_cache(const M12CorePipelineCacheQuery *query,
                                  M12CorePipelineCacheResult *out_result);
int m12core_store_pipeline_cache(const M12CorePipelineCacheQuery *query,
                                 uint64_t pipeline_handle);
int m12core_create_pipeline_state(const M12CorePipelineCreateDesc *desc,
                                  M12CorePipelineCreateResult *out_result);
int m12core_summarize_root_signature(const M12CoreRootSignatureDesc *desc,
                                     M12CoreRootSignatureSummary *out_summary);
int m12core_build_root_binding_plan(const M12CoreRootBindingPlanDesc *desc,
                                    M12CoreRootBindingPlanSummary *out_summary);
int m12core_lookup_root_binding(const M12CoreRootBindingLookupDesc *desc,
                                M12CoreRootBindingLookupResult *out_result);
int m12core_summarize_prewarm_pack(const M12CorePrewarmPackDesc *desc,
                                   M12CorePrewarmPackSummary *out_summary);
int m12core_build_draw_plan(const M12CoreDrawPlanDesc *desc,
                            M12CoreDrawPlanSummary *out_summary);
int m12core_build_present_plan(const M12CorePresentPlanDesc *desc,
                               M12CorePresentPlanSummary *out_summary);
int m12core_plan_present_execute(const M12CorePresentExecuteDesc *desc,
                                 M12CorePresentExecuteSummary *out_summary);
int m12core_build_replay_plan(const M12CoreReplayPlanDesc *desc,
                              M12CoreReplayPlanSummary *out_summary);
int m12core_plan_replay_execute(const M12CoreReplayExecuteDesc *desc,
                                M12CoreReplayExecuteSummary *out_summary);
int m12core_validate_command_stream(const M12CoreCommandStreamDesc *desc,
                                    M12CoreCommandStreamSummary *out_summary);
int m12core_plan_render_pass(const M12CoreRenderPassPlanDesc *desc,
                             M12CoreRenderPassPlanSummary *out_summary);
int m12core_validate_command_packet_stream(
    const M12CoreCommandPacketStreamDesc *desc,
    M12CoreCommandPacketStreamSummary *out_summary);
int m12core_make_cache_compatibility_key(
    const M12CoreCacheCompatibilityDesc *desc,
    M12CoreCacheCompatibilityKey *out_key);

#ifdef __cplusplus
}
#endif
