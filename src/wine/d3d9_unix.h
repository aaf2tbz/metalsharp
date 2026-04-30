#ifndef METALSHARP_D3D9_UNIX_H
#define METALSHARP_D3D9_UNIX_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum d3d9_func_id {
    D3D9_FUNC_INIT = 0,
    D3D9_FUNC_CREATE_DEVICE,
    D3D9_FUNC_CREATE_VERTEX_BUFFER,
    D3D9_FUNC_CREATE_INDEX_BUFFER,
    D3D9_FUNC_CREATE_TEXTURE,
    D3D9_FUNC_CREATE_RENDER_TARGET,
    D3D9_FUNC_CREATE_DEPTH_STENCIL,
    D3D9_FUNC_CREATE_VERTEX_SHADER,
    D3D9_FUNC_CREATE_PIXEL_SHADER,
    D3D9_FUNC_CREATE_VERTEX_DECLARATION,
    D3D9_FUNC_SET_VERTEX_DECLARATION,
    D3D9_FUNC_SET_STREAM_SOURCE,
    D3D9_FUNC_SET_INDICES,
    D3D9_FUNC_SET_VERTEX_SHADER,
    D3D9_FUNC_SET_PIXEL_SHADER,
    D3D9_FUNC_SET_VS_CONSTANTS_F,
    D3D9_FUNC_SET_PS_CONSTANTS_F,
    D3D9_FUNC_SET_TEXTURE,
    D3D9_FUNC_SET_SAMPLER_STATE,
    D3D9_FUNC_SET_RENDER_STATE,
    D3D9_FUNC_DRAW_PRIMITIVE,
    D3D9_FUNC_DRAW_INDEXED_PRIMITIVE,
    D3D9_FUNC_CLEAR,
    D3D9_FUNC_PRESENT,
    D3D9_FUNC_SET_RENDER_TARGET,
    D3D9_FUNC_SET_DEPTH_STENCIL,
    D3D9_FUNC_SET_VIEWPORT,
    D3D9_FUNC_SET_SCISSOR_RECT,
    D3D9_FUNC_BEGIN_SCENE,
    D3D9_FUNC_END_SCENE,
    D3D9_FUNC_RESET,
    D3D9_FUNC_CREATE_SURFACE,
    D3D9_FUNC_MAP,
    D3D9_FUNC_UNMAP,
    D3D9_FUNC_COUNT
};

struct d3d9_create_device_params {
    uint64_t hwnd;
    uint32_t width;
    uint32_t height;
    uint32_t back_buffer_format;
    int windowed;
    uint32_t back_buffer_count;
    int enable_auto_depth_stencil;
    uint32_t auto_depth_stencil_format;
    uint32_t presentation_interval;
    uint32_t multisample_type;
    uint32_t multisample_quality;
};

struct d3d9_create_buffer_params {
    uint32_t length;
    uint32_t usage;
    uint32_t fvf;
    int pool;
    uint64_t out_handle;
};

struct d3d9_create_texture_params {
    uint32_t width;
    uint32_t height;
    uint32_t levels;
    uint32_t usage;
    uint32_t format;
    int pool;
    uint64_t out_handle;
};

struct d3d9_create_surface_params {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t multisample_type;
    uint32_t multisample_quality;
    int discard;
    uint64_t out_handle;
};

struct d3d9_create_shader_params {
    const void* bytecode;
    uint32_t bytecode_size;
    uint64_t out_handle;
};

struct d3d9_vertex_element {
    uint16_t stream;
    uint16_t offset;
    uint8_t type;
    uint8_t method;
    uint8_t usage;
    uint8_t usage_index;
};

struct d3d9_create_vertex_decl_params {
    const struct d3d9_vertex_element* elements;
    uint32_t num_elements;
    uint64_t out_handle;
};

struct d3d9_set_stream_source_params {
    uint32_t stream_number;
    uint64_t buffer_handle;
    uint32_t offset;
    uint32_t stride;
};

struct d3d9_set_constants_f_params {
    uint32_t start_register;
    uint32_t count;
    float values[256];
};

struct d3d9_set_render_state_params {
    uint32_t state;
    uint32_t value;
};

struct d3d9_set_sampler_state_params {
    uint32_t sampler;
    uint32_t type;
    uint32_t value;
};

struct d3d9_set_texture_params {
    uint32_t stage;
    uint64_t texture_handle;
};

struct d3d9_draw_params {
    uint32_t primitive_type;
    uint32_t start_vertex;
    uint32_t primitive_count;
};

struct d3d9_draw_indexed_params {
    uint32_t primitive_type;
    int32_t base_vertex_index;
    uint32_t min_vertex_index;
    uint32_t num_vertices;
    uint32_t start_index;
    uint32_t primitive_count;
};

struct d3d9_clear_params {
    uint32_t flags;
    uint32_t color;
    float z;
    uint32_t stencil;
};

struct d3d9_viewport {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    float min_z;
    float max_z;
};

struct d3d9_scissor_rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct d3d9_reset_params {
    uint32_t width;
    uint32_t height;
    uint32_t back_buffer_format;
    int windowed;
    uint32_t back_buffer_count;
    int enable_auto_depth_stencil;
    uint32_t auto_depth_stencil_format;
};

struct d3d9_map_params {
    uint64_t resource_handle;
    void* out_data;
    uint32_t out_row_pitch;
    uint32_t out_depth_pitch;
};

#ifdef __cplusplus
}
#endif

#endif
