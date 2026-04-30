#ifndef METALSHARP_UNIX_H
#define METALSHARP_UNIX_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ms_func_id {
    MS_FUNC_INIT = 0,
    MS_FUNC_CREATE_DEVICE,
    MS_FUNC_CREATE_SWAP_CHAIN,
    MS_FUNC_CREATE_TEXTURE_2D,
    MS_FUNC_CREATE_BUFFER,
    MS_FUNC_CREATE_VERTEX_SHADER,
    MS_FUNC_CREATE_PIXEL_SHADER,
    MS_FUNC_CREATE_RENDER_TARGET_VIEW,
    MS_FUNC_CREATE_DEPTH_STENCIL_VIEW,
    MS_FUNC_CREATE_SAMPLER_STATE,
    MS_FUNC_CREATE_INPUT_LAYOUT,
    MS_FUNC_CREATE_BLEND_STATE,
    MS_FUNC_CREATE_RASTERIZER_STATE,
    MS_FUNC_CREATE_DEPTH_STENCIL_STATE,
    MS_FUNC_DRAW_INDEXED,
    MS_FUNC_DRAW,
    MS_FUNC_MAP,
    MS_FUNC_UNMAP,
    MS_FUNC_PRESENT,
    MS_FUNC_RESIZE_BUFFERS,
    MS_FUNC_GET_BUFFER,
    MS_FUNC_COPY_RESOURCE,
    MS_FUNC_COPY_SUBRESOURCE_REGION,
    MS_FUNC_SET_VIEWPORTS,
    MS_FUNC_SET_SCISSOR_RECTS,
    MS_FUNC_CLEAR_RENDER_TARGET_VIEW,
    MS_FUNC_CLEAR_DEPTH_STENCIL_VIEW,
    MS_FUNC_OM_SET_RENDER_TARGETS,
    MS_FUNC_IA_SET_VERTEX_BUFFERS,
    MS_FUNC_IA_SET_INDEX_BUFFER,
    MS_FUNC_IA_SET_PRIMITIVE_TOPOLOGY,
    MS_FUNC_VS_SET_SHADER,
    MS_FUNC_PS_SET_SHADER,
    MS_FUNC_VS_SET_CONSTANT_BUFFERS,
    MS_FUNC_PS_SET_CONSTANT_BUFFERS,
    MS_FUNC_VS_SET_SHADER_RESOURCES,
    MS_FUNC_PS_SET_SHADER_RESOURCES,
    MS_FUNC_VS_SET_SAMPLERS,
    MS_FUNC_PS_SET_SAMPLERS,
    MS_FUNC_RSS_SET_STATE,
    MS_FUNC_OM_SET_BLEND_STATE,
    MS_FUNC_OM_SET_DEPTH_STENCIL_STATE,
    MS_FUNC_CREATE_SHADER_RESOURCE_VIEW,
    MS_FUNC_UPDATE_SUBRESOURCE,
    MS_FUNC_GENERATE_MIPS,
    MS_FUNC_CREATE_COMPUTE_SHADER,
    MS_FUNC_CS_SET_SHADER,
    MS_FUNC_CS_SET_CONSTANT_BUFFERS,
    MS_FUNC_CS_SET_SHADER_RESOURCES,
    MS_FUNC_CS_SET_SAMPLERS,
    MS_FUNC_CS_SET_UNORDERED_ACCESS_VIEWS,
    MS_FUNC_DISPATCH,
    MS_FUNC_CREATE_UNORDERED_ACCESS_VIEW,
    MS_FUNC_DESTROY_DEVICE,
    MS_FUNC_DESTROY_TEXTURE,
    MS_FUNC_DESTROY_BUFFER,
    MS_FUNC_DESTROY_SHADER,
    MS_FUNC_DESTROY_VIEW,
    MS_FUNC_DESTROY_STATE,
    MS_FUNC_DESTROY_INPUT_LAYOUT,
    MS_FUNC_GET_DEVICE_CONTEXT,
    MS_FUNC_EXECUTE_COMMAND_LIST,
    MS_FUNC_FINISH_COMMAND_LIST,
    MS_FUNC_FLUSH,
    MS_FUNC_SHUTDOWN,
};

struct ms_create_device_params {
    int driver_type;
    int flags;
    int feature_level;
    int sdk_version;
};

struct ms_create_swap_chain_params {
    void* hwnd;
    int width;
    int height;
    int refresh_rate_numerator;
    int refresh_rate_denominator;
    int windowed;
    int sample_count;
    int sample_quality;
    int buffer_count;
    int format;
    int scanline_ordering;
    int scaling;
};

struct ms_create_texture_2d_params {
    int width;
    int height;
    int mip_levels;
    int array_size;
    int format;
    int sample_count;
    int sample_quality;
    int usage;
    int bind_flags;
    int cpu_access_flags;
    int misc_flags;
};

struct ms_create_buffer_params {
    int byte_width;
    int usage;
    int bind_flags;
    int cpu_access_flags;
    int misc_flags;
    int structure_byte_stride;
    size_t initial_data_size;
    const void* initial_data;
};

struct ms_viewport {
    float top_left_x;
    float top_left_y;
    float width;
    float height;
    float min_depth;
    float max_depth;
};

struct ms_rect {
    int left;
    int top;
    int right;
    int bottom;
};

struct ms_subresource_data {
    const void* p_sys_mem;
    int sys_mem_pitch;
    int sys_mem_slice_pitch;
};

struct ms_box {
    int left;
    int top;
    int front;
    int right;
    int bottom;
    int back;
};

struct ms_mapped_subresource {
    void* p_data;
    int row_pitch;
    int depth_pitch;
};

typedef int (*ms_unix_call_func)(ms_func_id func, void* params);

#ifdef __cplusplus
}
#endif

#endif
