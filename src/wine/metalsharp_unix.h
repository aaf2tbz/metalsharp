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
    MS_FUNC_CREATE_BUFFER,
    MS_FUNC_CREATE_TEXTURE_2D,
    MS_FUNC_CREATE_VERTEX_SHADER,
    MS_FUNC_CREATE_PIXEL_SHADER,
    MS_FUNC_CREATE_RENDER_TARGET_VIEW,
    MS_FUNC_CREATE_DEPTH_STENCIL_VIEW,
    MS_FUNC_CREATE_INPUT_LAYOUT,
    MS_FUNC_CREATE_BLEND_STATE,
    MS_FUNC_CREATE_RASTERIZER_STATE,
    MS_FUNC_CREATE_DEPTH_STENCIL_STATE,
    MS_FUNC_CREATE_SAMPLER_STATE,
    MS_FUNC_DRAW_INDEXED,
    MS_FUNC_DRAW,
    MS_FUNC_MAP,
    MS_FUNC_UNMAP,
    MS_FUNC_PRESENT,
    MS_FUNC_RESIZE_BUFFERS,
    MS_FUNC_GET_BUFFER,
    MS_FUNC_COPY_RESOURCE,
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
    MS_FUNC_COPY_SUBRESOURCE_REGION,
};

struct ms_create_device_params {
    int driver_type;
    int flags;
    int feature_level;
    int sdk_version;
};

struct ms_create_swap_chain_params {
    uint64_t hwnd;
    uint32_t width;
    uint32_t height;
    int windowed;
    int buffer_count;
    int format;
};

struct ms_create_buffer_params {
    uint32_t byte_width;
    int usage;
    int bind_flags;
    int cpu_access_flags;
    int misc_flags;
    uint32_t structure_byte_stride;
    size_t initial_data_size;
    const void* initial_data;
    uint64_t out_handle;
};

struct ms_create_texture_2d_params {
    uint32_t width;
    uint32_t height;
    uint32_t mip_levels;
    uint32_t array_size;
    uint32_t format;
    uint32_t sample_count;
    uint32_t sample_quality;
    uint32_t usage;
    uint32_t bind_flags;
    uint32_t cpu_access_flags;
    uint32_t misc_flags;
    uint64_t out_handle;
};

struct ms_create_shader_params {
    const void* bytecode;
    uint32_t bytecode_size;
    uint64_t out_handle;
};

struct ms_create_rt_view_params {
    uint64_t texture_handle;
    uint32_t format;
    uint32_t mip_slice;
    uint64_t out_handle;
};

struct ms_set_render_targets_params {
    uint32_t num_views;
    uint64_t rtv_handles[8];
    uint64_t dsv_handle;
};

struct ms_clear_params {
    uint64_t view_handle;
    float r, g, b, a;
};

struct ms_viewport {
    float top_left_x;
    float top_left_y;
    float width;
    float height;
    float min_depth;
    float max_depth;
};

struct ms_set_vertex_buffers_params {
    uint32_t start_slot;
    uint32_t num_buffers;
    uint64_t buffer_handles[16];
    uint32_t strides[16];
    uint32_t offsets[16];
};

struct ms_set_index_buffer_params {
    uint64_t buffer_handle;
    uint32_t format;
    uint32_t offset;
};

struct ms_set_constant_buffers_params {
    uint32_t start_slot;
    uint32_t num_buffers;
    uint64_t buffer_handles[14];
};

struct ms_draw_indexed_params {
    uint32_t index_count;
    uint32_t start_index;
    int32_t base_vertex;
};

struct ms_draw_params {
    uint32_t vertex_count;
    uint32_t start_vertex;
};

struct ms_update_subresource_params {
    uint64_t resource_handle;
    uint32_t subresource;
    const void* data;
    uint32_t row_pitch;
    uint32_t depth_pitch;
    size_t data_size;
    uint32_t offset;
};

struct ms_map_params {
    uint64_t resource_handle;
    uint32_t subresource;
    void* out_data;
    uint32_t out_row_pitch;
};

struct ms_resize_params {
    uint32_t width;
    uint32_t height;
    uint32_t buffer_count;
};

struct ms_get_buffer_params {
    uint32_t buffer_index;
    uint64_t out_handle;
};

#ifdef __cplusplus
}
#endif

#endif
