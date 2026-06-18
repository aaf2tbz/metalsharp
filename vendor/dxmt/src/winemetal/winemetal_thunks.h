
#ifndef __WINEMETAL_THUNKS_H
#define __WINEMETAL_THUNKS_H

#include "winemetal.h"
#include "../m12core/m12core.h"

#pragma pack(push, 8)

struct unixcall_generic_obj_ret {
  obj_handle_t ret;
};

struct unixcall_generic_obj_noret {
  obj_handle_t handle;
};

struct unixcall_generic_obj_obj_ret {
  obj_handle_t handle;
  obj_handle_t ret;
};

struct unixcall_generic_obj_uint64_obj_ret {
  obj_handle_t handle;
  uint64_t arg;
  obj_handle_t ret;
};

struct unixcall_mtlfunction_vertex_attributes {
  obj_handle_t function;
  uint64_t attributes;
  uint32_t max_attributes;
  uint32_t ret_count;
};

struct unixcall_generic_obj_uint64_ret {
  obj_handle_t handle;
  uint64_t ret;
};

struct unixcall_generic_obj_uint64_noret {
  obj_handle_t handle;
  uint64_t arg;
};

struct unixcall_generic_obj_ptr_noret {
  obj_handle_t handle;
  struct WMTMemoryPointer arg;
};

struct unixcall_generic_obj_constptr_noret {
  obj_handle_t handle;
  struct WMTConstMemoryPointer arg;
};

struct unixcall_generic_obj_obj_uint64_noret {
  obj_handle_t handle;
  obj_handle_t arg0;
  uint64_t arg1;
};

struct unixcall_generic_obj_uint64_uint64_ret {
  obj_handle_t handle;
  uint64_t arg;
  uint64_t ret;
};

struct unixcall_nsstring_getcstring {
  obj_handle_t str;
  uint64_t buffer_ptr;
  uint64_t max_length;
  enum WMTStringEncoding encoding;
  uint32_t ret;
};

struct unixcall_mtldevice_newbuffer {
  obj_handle_t device;
  struct WMTMemoryPointer info;
  obj_handle_t ret;
};

struct unixcall_mtldevice_newsamplerstate {
  obj_handle_t device;
  struct WMTMemoryPointer info;
  obj_handle_t ret;
};

struct unixcall_mtldevice_newdepthstencilstate {
  obj_handle_t device;
  struct WMTConstMemoryPointer info;
  obj_handle_t ret;
};

struct unixcall_mtldevice_newtexture {
  obj_handle_t device;
  struct WMTMemoryPointer info;
  obj_handle_t ret;
};

struct unixcall_mtlbuffer_newtexture {
  obj_handle_t buffer;
  struct WMTMemoryPointer info;
  uint64_t offset;
  uint64_t bytes_per_row;
  obj_handle_t ret;
};

struct unixcall_mtltexture_newtextureview {
  obj_handle_t texture;
  enum WMTPixelFormat format;
  enum WMTTextureType texture_type;
  uint16_t level_start;
  uint16_t level_count;
  uint16_t slice_start;
  uint16_t slice_count;
  struct WMTTextureSwizzleChannels swizzle;
  obj_handle_t ret;
  uint64_t gpu_resource_id;
};

struct unixcall_mtldevice_newlibrary {
  obj_handle_t device;
  obj_handle_t data;
  obj_handle_t ret_error;
  obj_handle_t ret_library;
};

struct unixcall_mtldevice_newcomputepso {
  obj_handle_t device;
  struct WMTConstMemoryPointer info;
  obj_handle_t ret_error;
  obj_handle_t ret_pso;
};

struct unixcall_mtldevice_newrenderpso {
  obj_handle_t device;
  struct WMTConstMemoryPointer info;
  obj_handle_t ret_error;
  obj_handle_t ret_pso;
};

struct unixcall_mtldevice_newmeshrenderpso {
  obj_handle_t device;
  struct WMTConstMemoryPointer info;
  obj_handle_t ret_error;
  obj_handle_t ret_pso;
};

struct unixcall_generic_obj_cmd_noret {
  obj_handle_t encoder;
  struct WMTConstMemoryPointer cmd_head;
};

struct unixcall_mtltexture_replaceregion {
  obj_handle_t texture;
  struct WMTOrigin origin;
  struct WMTSize size;
  uint64_t level;
  uint64_t slice;
  struct WMTMemoryPointer data;
  uint64_t bytes_per_row;
  uint64_t bytes_per_image;
};

struct unixcall_generic_obj_obj_noret {
  obj_handle_t handle;
  obj_handle_t arg;
};

struct unixcall_generic_obj_obj_obj_noret {
  obj_handle_t handle;
  obj_handle_t arg0;
  obj_handle_t arg1;
};

struct unixcall_generic_obj_obj_obj_uint64_ret {
  obj_handle_t handle;
  obj_handle_t arg0;
  obj_handle_t arg1;
  uint64_t ret;
};

struct unixcall_generic_obj_obj_double_noret {
  obj_handle_t handle;
  obj_handle_t arg0;
  double arg1;
};

struct unixcall_mtlcapturemanager_startcapture {
  obj_handle_t capture_manager;
  struct WMTMemoryPointer info;
  bool ret;
};

struct unixcall_mtldevice_newfxtemporalscaler {
  obj_handle_t device;
  struct WMTConstMemoryPointer info;
  obj_handle_t ret;
};

struct unixcall_mtldevice_newfxspatialscaler {
  obj_handle_t device;
  struct WMTConstMemoryPointer info;
  obj_handle_t ret;
};

struct unixcall_mtlcommandbuffer_temporal_scale {
  obj_handle_t cmdbuf;
  obj_handle_t scaler;
  obj_handle_t color;
  obj_handle_t output;
  obj_handle_t depth;
  obj_handle_t motion;
  obj_handle_t exposure;
  obj_handle_t fence;
  struct WMTConstMemoryPointer props;
};

struct unixcall_mtlcommandbuffer_spatial_scale {
  obj_handle_t cmdbuf;
  obj_handle_t scaler;
  obj_handle_t color;
  obj_handle_t output;
  obj_handle_t fence;
};

struct unixcall_nsstring_string {
  struct WMTConstMemoryPointer buffer_ptr;
  enum WMTStringEncoding encoding;
  obj_handle_t ret;
};

struct unixcall_create_metal_view_from_hwnd {
  uint64_t hwnd;
  obj_handle_t device;
  obj_handle_t ret_view;
  obj_handle_t ret_layer;
};

struct unixcall_enumerate {
  obj_handle_t enumerable;
  uint64_t start;
  uint64_t buffer_size;
  struct WMTMemoryPointer buffer;
  uint64_t ret_read;
};

struct unixcall_mtllibrary_newfunction_with_constants {
  obj_handle_t library;
  struct WMTConstMemoryPointer name;
  struct WMTConstMemoryPointer constants;
  uint64_t num_constants;
  obj_handle_t ret;
  obj_handle_t ret_error;
};

struct unixcall_mtllibrary_newfunction_with_descriptor {
  obj_handle_t library;
  struct WMTConstMemoryPointer name;
  struct WMTConstMemoryPointer specialized_name;
  struct WMTConstMemoryPointer constants;
  uint64_t num_constants;
  uint32_t options;
  obj_handle_t ret;
  obj_handle_t ret_error;
};

struct unixcall_query_display_setting {
  uint64_t display_id;
  enum WMTColorSpace colorspace;
  struct WMTMemoryPointer hdr_metadata;
  bool ret;
};

struct unixcall_update_display_setting {
  uint64_t display_id;
  enum WMTColorSpace colorspace;
  struct WMTConstMemoryPointer hdr_metadata;
};

struct unixcall_query_display_setting_for_layer {
  obj_handle_t layer;
  uint64_t version;
  enum WMTColorSpace colorspace;
  struct WMTMemoryPointer hdr_metadata;
  struct WMTEDRValue edr_value;
};

struct unixcall_mtlbuffer_updatecontents {
  obj_handle_t buffer;
  uint64_t offset;
  struct WMTConstMemoryPointer data;
  uint64_t length;
};

struct unixcall_mtlsharedevent_setevent {
  obj_handle_t shared_event;
  obj_handle_t event_handle;
  obj_handle_t shared_event_listener;
  uint64_t value;
};

struct unixcall_mtlsharedevent_createmachport {
  obj_handle_t event;
  uint32_t ret_mach_port;
};

struct unixcall_mtldevice_newsharedeventwithmachport {
  obj_handle_t device;
  uint32_t mach_port;
  obj_handle_t ret_event;
};

struct unixcall_get_os_version {
  uint64_t ret_major;
  uint64_t ret_minor;
  uint64_t ret_patch;
};

struct unixcall_mtldevice_newbinaryarchive {
  obj_handle_t device;
  struct WMTConstMemoryPointer url;
  obj_handle_t ret_archive;
  obj_handle_t ret_error;
};

struct unixcall_mtlbinaryarchive_serialize {
  obj_handle_t archive;
  struct WMTConstMemoryPointer url;
  obj_handle_t ret_error;
};

struct unixcall_cache_alloc_init {
  struct WMTConstMemoryPointer path;
  uint64_t version;
  obj_handle_t ret_cache;
};

struct unixcall_cache_get {
  obj_handle_t cache;
  struct WMTConstMemoryPointer key;
  uint64_t key_length;
  obj_handle_t ret_data;
};

struct unixcall_cache_set {
  obj_handle_t cache;
  struct WMTConstMemoryPointer key;
  uint64_t key_length;
  obj_handle_t value_data;
};

struct unixcall_setmetalcachepath {
  struct WMTConstMemoryPointer path;
  uint64_t ret_success;
};

struct unixcall_bootstrap {
  char name[128];
  uint32_t mach_port;
  uint32_t reserved;
};

struct unixcall_mtlsharedevent_waituntilsignaledvalue {
  obj_handle_t event;
  uint64_t value;
  uint64_t timeout_ms;
  bool ret_timeout;
};

struct unixcall_mtlcountersamplebuffer_newtimestampbuffer {
  obj_handle_t device;
  uint32_t sample_count;
  uint32_t shared;
  obj_handle_t ret;
};

struct unixcall_mtlcountersamplebuffer_resolvecounterrange {
  obj_handle_t sample_buffer;
  uint32_t start;
  uint32_t len;
  struct WMTMemoryPointer data_out;
  uint64_t data_length;
};

struct unixcall_mtlcommandbuffer_blitcommandencoderwithsamplebuffers {
  obj_handle_t cmdbuf;
  struct WMTMemoryPointer attachments;
  uint64_t num_attachments;
  obj_handle_t ret;
};

struct unixcall_mtldevice_newlibrary_source {
  obj_handle_t device;
  struct WMTConstMemoryPointer source;
  uint64_t source_length;
  obj_handle_t ret_error;
  obj_handle_t ret_library;
};

struct unixcall_m12core_record_counters {
  uint32_t counter_count;
  uint32_t reserved;
  uint64_t deltas[M12CORE_COUNTER_COUNT];
};

struct unixcall_m12core_hash_shader_bytecode {
  struct WMTConstMemoryPointer bytecode;
  uint64_t bytecode_size;
  uint32_t stage;
  uint32_t ret_success;
  M12CoreShaderBytecodeInfo ret_info;
};

struct unixcall_m12core_format_shader_cache_paths {
  struct WMTConstMemoryPointer cache_root;
  uint64_t shader_hash;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreShaderCachePaths ret_paths;
};

struct unixcall_m12core_probe_shader_cache {
  struct WMTConstMemoryPointer cache_root;
  uint64_t shader_hash;
  uint32_t force_source_compile;
  uint32_t ret_success;
  M12CoreShaderCacheLookup ret_lookup;
};

struct unixcall_m12core_parse_shader_reflection {
  struct WMTConstMemoryPointer reflection_text;
  uint64_t reflection_text_size;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreShaderReflectionSummary ret_summary;
};

struct unixcall_m12core_make_pipeline_cache_key {
  M12CorePipelineCacheKeyInput input;
  uint32_t ret_success;
  uint32_t reserved;
  M12CorePipelineCacheKey ret_key;
};

struct unixcall_m12core_lower_dxil_to_msl {
  uint32_t abi_version;
  uint32_t stage;
  struct WMTConstMemoryPointer dxil_container;
  uint64_t dxil_container_size;
  struct WMTConstMemoryPointer vertex_inputs;
  uint32_t vertex_input_count;
  uint32_t reserved0;
  struct WMTMemoryPointer out_source;
  uint64_t out_source_capacity;
  uint32_t ret_success;
  uint32_t reserved1;
  M12CoreDXILToMSLResult ret_result;
};

struct unixcall_m12core_lookup_pipeline_cache {
  M12CorePipelineCacheQuery query;
  uint32_t ret_success;
  uint32_t reserved;
  M12CorePipelineCacheResult ret_result;
};

struct unixcall_m12core_store_pipeline_cache {
  M12CorePipelineCacheQuery query;
  obj_handle_t pipeline_handle;
  uint32_t ret_success;
  uint32_t reserved;
};

struct unixcall_m12core_reflect_sm50_shader {
  struct WMTConstMemoryPointer bytecode;
  uint64_t bytecode_size;
  uint32_t options;
  uint32_t constant_buffer_capacity;
  uint32_t argument_capacity;
  uint32_t ret_success;
  struct WMTMemoryPointer out_reflection;
  struct WMTMemoryPointer out_constant_buffers;
  struct WMTMemoryPointer out_arguments;
  M12CoreSM50ReflectionResult ret_result;
};

struct unixcall_m12core_create_shader_function {
  obj_handle_t device;
  uint32_t stage;
  uint32_t input_kind;
  uint64_t shader_hash;
  struct WMTConstMemoryPointer input_data;
  uint64_t input_size;
  char entry_point[M12CORE_SHADER_ENTRY_POINT_CAPACITY];
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreShaderFunctionResult ret_result;
};

struct unixcall_m12core_make_pipeline_cache_key_from_fields {
  uint32_t abi_version;
  uint32_t kind;
  uint64_t base_hash;
  uint64_t device_id;
  uint64_t flags;
  struct WMTConstMemoryPointer fields;
  uint32_t field_count;
  uint32_t ret_success;
  M12CorePipelineCacheKey ret_key;
};

struct unixcall_m12core_create_pipeline_state {
  obj_handle_t device;
  uint32_t kind;
  uint32_t reserved0;
  uint64_t cache_key;
  struct WMTConstMemoryPointer pipeline_info;
  uint64_t pipeline_info_size;
  uint32_t ret_success;
  uint32_t reserved1;
  M12CorePipelineCreateResult ret_result;
};

struct unixcall_m12core_summarize_root_signature {
  uint32_t abi_version;
  uint32_t parameter_count;
  uint32_t static_sampler_count;
  uint32_t flags;
  uint64_t blob_hash;
  struct WMTConstMemoryPointer fields;
  uint32_t field_count;
  uint32_t ret_success;
  M12CoreRootSignatureSummary ret_summary;
};

struct unixcall_m12core_build_root_binding_plan {
  uint32_t abi_version;
  uint32_t flags;
  uint64_t root_signature_key;
  struct WMTConstMemoryPointer parameters;
  uint32_t parameter_count;
  uint32_t reserved0;
  struct WMTConstMemoryPointer ranges;
  uint32_t range_count;
  uint32_t reserved1;
  struct WMTConstMemoryPointer static_samplers;
  uint32_t static_sampler_count;
  uint32_t ret_success;
  M12CoreRootBindingPlanSummary ret_summary;
};

struct unixcall_m12core_lookup_root_binding {
  uint32_t abi_version;
  uint32_t lookup_kind;
  uint32_t range_type;
  uint32_t shader_register;
  uint32_t register_space;
  uint32_t shader_visibility;
  struct WMTConstMemoryPointer parameters;
  uint32_t parameter_count;
  uint32_t reserved0;
  struct WMTConstMemoryPointer ranges;
  uint32_t range_count;
  uint32_t reserved1;
  struct WMTConstMemoryPointer static_samplers;
  uint32_t static_sampler_count;
  uint32_t ret_success;
  M12CoreRootBindingLookupResult ret_result;
};

struct unixcall_m12core_summarize_prewarm_pack {
  uint32_t abi_version;
  uint32_t flags;
  uint64_t appid;
  uint64_t profile_key;
  uint64_t source_pack_key;
  struct WMTConstMemoryPointer pipelines;
  uint32_t pipeline_count;
  uint32_t reserved0;
  struct WMTConstMemoryPointer stages;
  uint32_t stage_count;
  uint32_t ret_success;
  M12CorePrewarmPackSummary ret_summary;
};

struct unixcall_m12core_build_draw_plan {
  M12CoreDrawPlanDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreDrawPlanSummary ret_summary;
};

struct unixcall_m12core_build_present_plan {
  M12CorePresentPlanDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CorePresentPlanSummary ret_summary;
};

struct unixcall_m12core_build_replay_plan {
  M12CoreReplayPlanDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreReplayPlanSummary ret_summary;
};

struct unixcall_m12core_validate_command_stream {
  M12CoreCommandStreamDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreCommandStreamSummary ret_summary;
};

struct unixcall_m12core_plan_replay_execute {
  M12CoreReplayExecuteDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreReplayExecuteSummary ret_summary;
};

struct unixcall_m12core_plan_render_pass {
  M12CoreRenderPassPlanDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreRenderPassPlanSummary ret_summary;
};

struct unixcall_m12core_plan_present_execute {
  M12CorePresentExecuteDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CorePresentExecuteSummary ret_summary;
};

struct unixcall_m12core_validate_command_packet_stream {
  uint32_t abi_version;
  uint32_t packet_count;
  uint32_t queue_type;
  uint32_t command_list_index;
  uint64_t command_list_id;
  uint64_t queue_serial;
  struct WMTConstMemoryPointer packets;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreCommandPacketStreamSummary ret_summary;
};

struct unixcall_m12core_make_cache_compatibility_key {
  M12CoreCacheCompatibilityDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreCacheCompatibilityKey ret_key;
};

struct unixcall_m12core_register_handle {
  M12CoreHandleRegistryDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreHandleRegistryResult ret_result;
};

struct unixcall_m12core_validate_handle {
  M12CoreHandleValidationDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreHandleValidationResult ret_result;
};

struct unixcall_m12core_classify_packet_support {
  uint32_t abi_version;
  uint32_t packet_count;
  uint32_t queue_type;
  uint32_t flags;
  uint64_t stream_key;
  uint64_t packet_sequence_xor;
  struct WMTConstMemoryPointer packets;
  uint32_t ret_success;
  uint32_t reserved;
  M12CorePacketSupportSummary ret_summary;
};

struct unixcall_m12core_execute_replay_packet_stream {
  uint32_t abi_version;
  uint32_t flags;
  uint32_t packet_count;
  uint32_t queue_type;
  uint32_t command_list_index;
  uint32_t reserved0;
  uint64_t command_list_id;
  uint64_t queue_serial;
  uint64_t stream_key;
  uint64_t packet_sequence_xor;
  struct WMTConstMemoryPointer packets;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreReplayPacketExecuteSummary ret_summary;
};

struct unixcall_m12core_plan_encoder_ownership {
  uint32_t abi_version;
  uint32_t flags;
  uint32_t packet_count;
  uint32_t queue_type;
  uint32_t command_list_index;
  uint32_t render_target_count;
  uint32_t descriptor_heap_count;
  uint32_t reserved0;
  uint64_t command_list_id;
  uint64_t queue_serial;
  uint64_t stream_key;
  uint64_t packet_sequence_xor;
  uint64_t replay_execute_key;
  uint64_t resource_layout_key;
  struct WMTConstMemoryPointer packets;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreEncoderOwnershipSummary ret_summary;
};

struct unixcall_m12core_plan_native_present_ownership {
  M12CoreNativePresentOwnershipDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreNativePresentOwnershipSummary ret_summary;
};

struct unixcall_m12core_plan_cache_warm_start {
  M12CoreCacheWarmStartDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreCacheWarmStartSummary ret_summary;
};

struct unixcall_m12core_plan_replay_coverage {
  M12CoreReplayCoverageDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreReplayCoverageSummary ret_summary;
};

struct unixcall_m12core_plan_thin_pe_checkpoint {
  M12CoreThinPECheckpointDesc desc;
  uint32_t ret_success;
  uint32_t reserved;
  M12CoreThinPECheckpointSummary ret_summary;
};

struct unixcall_m12core_execute_present_blit {
  M12CorePresentExecuteDesc desc;
  obj_handle_t command_buffer;
  obj_handle_t source_texture;
  obj_handle_t destination_texture;
  obj_handle_t drawable;
  uint32_t ret_success;
  uint32_t reserved;
  M12CorePresentExecuteSummary ret_summary;
};

#pragma pack(pop)

#endif
