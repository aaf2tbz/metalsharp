#include "m12core.h"
#include "../winemetal/winemetal.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Phase 3 native shader-function ownership.
 *
 * This file is Objective-C compiled as part of libm12core.  It is the first
 * high-risk shader slice where the native core, not PE-side D3D12, owns Metal
 * library creation, function lookup fallback order, and an in-process function
 * cache.  The ABI remains POD-only: PE shims pass the native MTLDevice handle,
 * shader bytes/source bytes, and a requested entry name; libm12core returns an
 * opaque retained MTLFunction handle plus an optional retained NSError handle.
 * DXIL->MSL lowering and full D3D12 reflection compatibility still stay on the
 * old path until their own scoped migration slices.
 */

static pthread_mutex_t g_shader_function_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static NSMutableDictionary<NSString *, id<MTLFunction>> *g_shader_function_cache;
static pthread_mutex_t g_pipeline_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static NSMutableDictionary<NSString *, id> *g_pipeline_cache;

static void m12core_copy_cstr(char *dst, size_t dst_size, const char *src) {
  if (!dst || !dst_size)
    return;
  if (!src)
    src = "";
  snprintf(dst, dst_size, "%s", src);
  dst[dst_size - 1] = 0;
}

static const char *m12core_default_entry_for_stage(uint32_t stage) {
  switch (stage) {
  case M12CORE_SHADER_STAGE_COMPUTE:
    return "cs_main";
  case M12CORE_SHADER_STAGE_VERTEX:
    return "vs_main";
  case M12CORE_SHADER_STAGE_PIXEL:
    return "ps_main";
  default:
    return "main";
  }
}

static NSString *m12core_pipeline_cache_key(const M12CorePipelineCacheQuery *query) {
  return [NSString stringWithFormat:@"%u:%016llx",
                                    query ? query->kind : 0,
                                    (unsigned long long)(query ? query->key : 0)];
}

static NSString *m12core_shader_function_cache_key(const M12CoreShaderFunctionDesc *desc) {
  const char *entry = desc->entry_point[0] ? desc->entry_point : m12core_default_entry_for_stage(desc->stage);
  return [NSString stringWithFormat:@"%llu:%u:%u:%016llx:%s",
                                    (unsigned long long)desc->device_handle,
                                    desc->stage,
                                    desc->input_kind,
                                    (unsigned long long)desc->shader_hash,
                                    entry];
}

static id<MTLFunction> m12core_lookup_function_with_fallbacks(id<MTLLibrary> library,
                                                               uint32_t stage,
                                                               const char *requested_entry,
                                                               char *selected_entry,
                                                               size_t selected_entry_size) {
  const char *fallbacks[5] = {
      requested_entry && requested_entry[0] ? requested_entry : m12core_default_entry_for_stage(stage),
      "main",
      "cs_main",
      "vs_main",
      "ps_main",
  };

  for (size_t i = 0; i < 5; i++) {
    if (!fallbacks[i] || !fallbacks[i][0])
      continue;
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) {
      if (fallbacks[j] && strcmp(fallbacks[i], fallbacks[j]) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate)
      continue;

    NSString *name = [[NSString alloc] initWithUTF8String:fallbacks[i]];
    if (!name)
      continue;
    id<MTLFunction> function = [library newFunctionWithName:name];
    [name release];
    if (function) {
      m12core_copy_cstr(selected_entry, selected_entry_size, fallbacks[i]);
      return function;
    }
  }
  return nil;
}

int m12core_lookup_pipeline_cache(const M12CorePipelineCacheQuery *query,
                                  M12CorePipelineCacheResult *out_result) {
  if (!out_result)
    return 1;

  memset(out_result, 0, sizeof(*out_result));
  out_result->abi_version = M12CORE_ABI_VERSION;
  if (!query || query->abi_version != M12CORE_ABI_VERSION)
    return 1;

  out_result->kind = query->kind;
  NSString *key = m12core_pipeline_cache_key(query);
  pthread_mutex_lock(&g_pipeline_cache_mutex);
  id cached = g_pipeline_cache ? [g_pipeline_cache objectForKey:key] : nil;
  if (cached) {
    [cached retain];
    out_result->hit = 1;
    out_result->pipeline_handle = (uint64_t)(uintptr_t)cached;
  }
  pthread_mutex_unlock(&g_pipeline_cache_mutex);
  return 0;
}

int m12core_store_pipeline_cache(const M12CorePipelineCacheQuery *query,
                                 uint64_t pipeline_handle) {
  if (!query || query->abi_version != M12CORE_ABI_VERSION || !pipeline_handle)
    return 1;

  /* Phase 4 pipeline-cache seam.  libm12core owns retained in-process Metal
   * pipeline cache storage.  The newer create_pipeline_state ABI below can
   * populate this cache itself; this older store call remains as the PE-side
   * direct-creation fallback seam.
   */
  NSString *key = m12core_pipeline_cache_key(query);
  id pipeline = (id)(uintptr_t)pipeline_handle;
  pthread_mutex_lock(&g_pipeline_cache_mutex);
  if (!g_pipeline_cache)
    g_pipeline_cache = [[NSMutableDictionary alloc] init];
  [g_pipeline_cache setObject:pipeline forKey:key];
  pthread_mutex_unlock(&g_pipeline_cache_mutex);
  return 0;
}

static MTLPixelFormat m12core_to_metal_pixel_format(enum WMTPixelFormat format) {
  return (MTLPixelFormat)ORIGINAL_FORMAT(format);
}

static NSArray *m12core_array_from_handles(const obj_handle_t *handles, uint32_t count) {
  if (!handles || !count)
    return nil;
  NSMutableArray *array = [NSMutableArray arrayWithCapacity:count];
  for (uint32_t i = 0; i < count; i++) {
    id object = (id)(uintptr_t)handles[i];
    if (object)
      [array addObject:object];
  }
  return array.count ? array : nil;
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
#endif

static id<MTLComputePipelineState> m12core_create_compute_pipeline(
    id<MTLDevice> device,
    const struct WMTComputePipelineInfo *info,
    NSError **error_out) {
  MTLComputePipelineDescriptor *descriptor = [[MTLComputePipelineDescriptor alloc] init];
  descriptor.computeFunction = (id<MTLFunction>)(uintptr_t)info->compute_function;
  descriptor.threadGroupSizeIsMultipleOfThreadExecutionWidth = info->tgsize_is_multiple_of_sgwidth;
  for (unsigned i = 0; i < 31; i++) {
    if (info->immutable_buffers & (1u << i))
      descriptor.buffers[i].mutability = MTLMutabilityImmutable;
  }
  if (info->num_binary_archives_for_lookup && info->binary_archives_for_lookup.ptr)
    descriptor.binaryArchives = m12core_array_from_handles(
        (const obj_handle_t *)info->binary_archives_for_lookup.ptr,
        info->num_binary_archives_for_lookup);
  MTLPipelineOption options =
      info->fail_on_binary_archive_miss ? MTLPipelineOptionFailOnBinaryArchiveMiss : MTLPipelineOptionNone;
  id<MTLComputePipelineState> pipeline = nil;
  NSError *error = nil;
  @try {
    pipeline = [device newComputePipelineStateWithDescriptor:descriptor
                                                     options:options
                                                  reflection:nil
                                                       error:&error];
  } @catch (NSException *exception) {
    pipeline = nil;
  }
  if (pipeline && !error && info->binary_archive_for_serialization) {
    [(id<MTLBinaryArchive>)(uintptr_t)info->binary_archive_for_serialization addComputePipelineFunctionsWithDescriptor:descriptor
                                                                                                                 error:&error];
  }
  if (error_out)
    *error_out = error;
  [descriptor release];
  return pipeline;
}

static id<MTLRenderPipelineState> m12core_create_render_pipeline(
    id<MTLDevice> device,
    const struct WMTRenderPipelineInfo *info,
    NSError **error_out) {
  MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];

  for (unsigned i = 0; i < 8; i++) {
    descriptor.colorAttachments[i].pixelFormat = m12core_to_metal_pixel_format(info->colors[i].pixel_format);
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
    if (info->immutable_fragment_buffers & (1u << i))
      descriptor.fragmentBuffers[i].mutability = MTLMutabilityImmutable;
    if (info->immutable_vertex_buffers & (1u << i))
      descriptor.vertexBuffers[i].mutability = MTLMutabilityImmutable;
  }

#ifndef DXMT_NO_PRIVATE_API
  [descriptor setLogicOperationEnabled:info->logic_operation_enabled];
  [descriptor setLogicOperation:(MTLLogicOperation)info->logic_operation];
#endif
  descriptor.depthAttachmentPixelFormat = m12core_to_metal_pixel_format(info->depth_pixel_format);
  descriptor.stencilAttachmentPixelFormat = m12core_to_metal_pixel_format(info->stencil_pixel_format);
  descriptor.alphaToCoverageEnabled = info->alpha_to_coverage_enabled;
  descriptor.rasterizationEnabled = info->rasterization_enabled;
  descriptor.rasterSampleCount = info->raster_sample_count;
  descriptor.inputPrimitiveTopology = (MTLPrimitiveTopologyClass)info->input_primitive_topology;
  descriptor.tessellationPartitionMode = (MTLTessellationPartitionMode)info->tessellation_partition_mode;
  descriptor.tessellationFactorStepFunction = (MTLTessellationFactorStepFunction)info->tessellation_factor_step;
  descriptor.tessellationOutputWindingOrder = (MTLWinding)info->tessellation_output_winding_order;
  descriptor.maxTessellationFactor = info->max_tessellation_factor;
  descriptor.vertexFunction = (id<MTLFunction>)(uintptr_t)info->vertex_function;
  descriptor.fragmentFunction = (id<MTLFunction>)(uintptr_t)info->fragment_function;

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 130000
  if (@available(macOS 13, *)) {
    if (info->num_vertex_linked_functions && info->vertex_linked_functions.ptr) {
      MTLLinkedFunctions *linked = [[MTLLinkedFunctions alloc] init];
      linked.functions = m12core_array_from_handles(
          (const obj_handle_t *)info->vertex_linked_functions.ptr,
          info->num_vertex_linked_functions);
      descriptor.vertexLinkedFunctions = linked;
      [linked release];
    }
    if (info->num_fragment_linked_functions && info->fragment_linked_functions.ptr) {
      MTLLinkedFunctions *linked = [[MTLLinkedFunctions alloc] init];
      linked.functions = m12core_array_from_handles(
          (const obj_handle_t *)info->fragment_linked_functions.ptr,
          info->num_fragment_linked_functions);
      descriptor.fragmentLinkedFunctions = linked;
      [linked release];
    }
  }
#endif

  if (info->vertex_descriptor &&
      (info->vertex_descriptor->attribute_count > 0 || info->vertex_descriptor->layout_count > 0)) {
    MTLVertexDescriptor *vd = [[MTLVertexDescriptor alloc] init];
    for (uint32_t i = 0; i < info->vertex_descriptor->layout_count && i < WMT_MAX_VERTEX_BUFFER_LAYOUTS; i++) {
      struct WMTVertexBufferLayoutDesc layout = info->vertex_descriptor->layouts[i];
      if (layout.stride > 0) {
        vd.layouts[i].stride = layout.stride;
        vd.layouts[i].stepFunction = (MTLVertexStepFunction)layout.step_function;
        vd.layouts[i].stepRate = layout.step_rate;
      }
    }
    for (uint32_t i = 0; i < info->vertex_descriptor->attribute_count && i < WMT_MAX_VERTEX_ATTRIBUTES; i++) {
      struct WMTVertexAttributeDesc attr = info->vertex_descriptor->attributes[i];
      if (attr.format != WMTAttributeFormatInvalid) {
        vd.attributes[i].format = (MTLVertexFormat)attr.format;
        vd.attributes[i].offset = attr.offset;
        vd.attributes[i].bufferIndex = attr.buffer_index;
      }
    }
    descriptor.vertexDescriptor = vd;
    [vd release];
  }

  if (info->num_binary_archives_for_lookup && info->binary_archives_for_lookup.ptr)
    descriptor.binaryArchives = m12core_array_from_handles(
        (const obj_handle_t *)info->binary_archives_for_lookup.ptr,
        info->num_binary_archives_for_lookup);

  MTLPipelineOption options =
      info->fail_on_binary_archive_miss ? MTLPipelineOptionFailOnBinaryArchiveMiss : MTLPipelineOptionNone;
  NSError *error = nil;
  id<MTLRenderPipelineState> pipeline = nil;
  @try {
    pipeline = [device newRenderPipelineStateWithDescriptor:descriptor
                                                    options:options
                                                 reflection:nil
                                                      error:&error];
  } @catch (NSException *exception) {
    pipeline = nil;
  }
  if (pipeline && !error && info->binary_archive_for_serialization) {
    [(id<MTLBinaryArchive>)(uintptr_t)info->binary_archive_for_serialization addRenderPipelineFunctionsWithDescriptor:descriptor
                                                                                                                error:&error];
  }
  if (error_out)
    *error_out = error;
  [descriptor release];
  return pipeline;
}

int m12core_create_pipeline_state(const M12CorePipelineCreateDesc *desc,
                                  M12CorePipelineCreateResult *out_result) {
  if (!out_result)
    return 1;

  memset(out_result, 0, sizeof(*out_result));
  out_result->abi_version = M12CORE_ABI_VERSION;
  if (!desc || desc->abi_version != M12CORE_ABI_VERSION || !desc->device_handle ||
      !desc->pipeline_info || !desc->pipeline_info_size) {
    out_result->status = M12CORE_PIPELINE_CREATE_STATUS_INVALID;
    return 0;
  }
  out_result->kind = desc->kind;

  /* Phase 4 native PSO creation seam.  D3D12 still builds the compatibility
   * WMT*PipelineInfo POD block, but libm12core now owns cache lookup, actual
   * Metal new*PipelineState creation, and retained cache insertion.  The PE
   * caller keeps a direct WMT fallback if this ABI is absent or returns no PSO.
   */
  M12CorePipelineCacheQuery query;
  memset(&query, 0, sizeof(query));
  query.abi_version = M12CORE_ABI_VERSION;
  query.kind = desc->kind;
  query.key = desc->cache_key;
  NSString *key = m12core_pipeline_cache_key(&query);
  pthread_mutex_lock(&g_pipeline_cache_mutex);
  id cached = g_pipeline_cache ? [g_pipeline_cache objectForKey:key] : nil;
  if (cached) {
    [cached retain];
    pthread_mutex_unlock(&g_pipeline_cache_mutex);
    out_result->status = M12CORE_PIPELINE_CREATE_STATUS_OK;
    out_result->cache_hit = 1;
    out_result->pipeline_handle = (uint64_t)(uintptr_t)cached;
    return 0;
  }
  pthread_mutex_unlock(&g_pipeline_cache_mutex);

  id<MTLDevice> device = (id<MTLDevice>)(uintptr_t)desc->device_handle;
  NSError *error = nil;
  id pipeline = nil;
  if (desc->kind == M12CORE_PIPELINE_KIND_COMPUTE) {
    if (desc->pipeline_info_size < sizeof(struct WMTComputePipelineInfo)) {
      out_result->status = M12CORE_PIPELINE_CREATE_STATUS_INVALID;
      return 0;
    }
    pipeline = m12core_create_compute_pipeline(device,
        (const struct WMTComputePipelineInfo *)desc->pipeline_info,
        &error);
  } else if (desc->kind == M12CORE_PIPELINE_KIND_RENDER) {
    if (desc->pipeline_info_size < sizeof(struct WMTRenderPipelineInfo)) {
      out_result->status = M12CORE_PIPELINE_CREATE_STATUS_INVALID;
      return 0;
    }
    pipeline = m12core_create_render_pipeline(device,
        (const struct WMTRenderPipelineInfo *)desc->pipeline_info,
        &error);
  } else {
    out_result->status = M12CORE_PIPELINE_CREATE_STATUS_UNSUPPORTED_KIND;
    return 0;
  }

  if (!pipeline) {
    out_result->status = M12CORE_PIPELINE_CREATE_STATUS_CREATE_FAILED;
    if (error) {
      [error retain];
      out_result->error_handle = (uint64_t)(uintptr_t)error;
    }
    return 0;
  }

  pthread_mutex_lock(&g_pipeline_cache_mutex);
  if (!g_pipeline_cache)
    g_pipeline_cache = [[NSMutableDictionary alloc] init];
  [g_pipeline_cache setObject:pipeline forKey:key];
  pthread_mutex_unlock(&g_pipeline_cache_mutex);

  out_result->status = M12CORE_PIPELINE_CREATE_STATUS_OK;
  out_result->pipeline_handle = (uint64_t)(uintptr_t)pipeline;
  return 0;
}

int m12core_create_shader_function(const M12CoreShaderFunctionDesc *desc,
                                   M12CoreShaderFunctionResult *out_result) {
  if (!out_result)
    return 1;

  memset(out_result, 0, sizeof(*out_result));
  out_result->abi_version = M12CORE_ABI_VERSION;

  if (!desc || desc->abi_version != M12CORE_ABI_VERSION || !desc->device_handle ||
      !desc->input_data || !desc->input_size) {
    out_result->status = M12CORE_SHADER_FUNCTION_STATUS_INVALID;
    return 0;
  }

  NSString *cache_key = m12core_shader_function_cache_key(desc);
  pthread_mutex_lock(&g_shader_function_cache_mutex);
  if (!g_shader_function_cache)
    g_shader_function_cache = [[NSMutableDictionary alloc] init];
  id<MTLFunction> cached = [g_shader_function_cache objectForKey:cache_key];
  if (cached) {
    [cached retain];
    pthread_mutex_unlock(&g_shader_function_cache_mutex);
    out_result->status = M12CORE_SHADER_FUNCTION_STATUS_OK;
    out_result->cache_hit = 1;
    out_result->function_handle = (uint64_t)(uintptr_t)cached;
    m12core_copy_cstr(out_result->selected_entry, sizeof(out_result->selected_entry),
                      desc->entry_point[0] ? desc->entry_point : m12core_default_entry_for_stage(desc->stage));
    return 0;
  }
  pthread_mutex_unlock(&g_shader_function_cache_mutex);

  id<MTLDevice> device = (id<MTLDevice>)(uintptr_t)desc->device_handle;
  NSError *error = nil;
  id<MTLLibrary> library = nil;

  if (desc->input_kind == M12CORE_SHADER_FUNCTION_INPUT_METALLIB) {
    dispatch_data_t data = dispatch_data_create(desc->input_data, (size_t)desc->input_size,
                                                NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    if (data) {
      library = [device newLibraryWithData:data error:&error];
      dispatch_release(data);
    }
  } else if (desc->input_kind == M12CORE_SHADER_FUNCTION_INPUT_MSL_SOURCE) {
    NSString *source = [[NSString alloc] initWithBytes:desc->input_data
                                               length:(NSUInteger)desc->input_size
                                             encoding:NSUTF8StringEncoding];
    if (source) {
      MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
      [options setLanguageVersion:MTLLanguageVersion3_1];
      library = [device newLibraryWithSource:source options:options error:&error];
      [options release];
      [source release];
    }
  } else {
    out_result->status = M12CORE_SHADER_FUNCTION_STATUS_INVALID;
    return 0;
  }

  if (!library) {
    out_result->status = M12CORE_SHADER_FUNCTION_STATUS_LIBRARY_FAILED;
    if (error) {
      [error retain];
      out_result->error_handle = (uint64_t)(uintptr_t)error;
    }
    return 0;
  }

  id<MTLFunction> function = m12core_lookup_function_with_fallbacks(
      library, desc->stage, desc->entry_point, out_result->selected_entry,
      sizeof(out_result->selected_entry));
  [library release];

  if (!function) {
    out_result->status = M12CORE_SHADER_FUNCTION_STATUS_FUNCTION_FAILED;
    return 0;
  }

  pthread_mutex_lock(&g_shader_function_cache_mutex);
  [g_shader_function_cache setObject:function forKey:cache_key];
  pthread_mutex_unlock(&g_shader_function_cache_mutex);

  out_result->status = M12CORE_SHADER_FUNCTION_STATUS_OK;
  out_result->function_handle = (uint64_t)(uintptr_t)function;
  return 0;
}
