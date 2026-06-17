#include "m12core.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
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
