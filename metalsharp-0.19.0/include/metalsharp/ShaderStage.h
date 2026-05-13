/// @file ShaderStage.h
/// @brief Enumeration of all supported shader stages in the D3D→Metal pipeline.
///
/// Covers the traditional rasterization stages (Vertex, Pixel, Geometry, Hull, Domain),
/// compute (Compute), mesh shader stages (Mesh, Amplification), and ray tracing stages
/// (RayGeneration, ClosestHit, Miss, Intersection, AnyHit, Callable). Used as a
/// discriminator throughout the shader compilation and binding infrastructure.

#pragma once

namespace metalsharp {

enum class ShaderStage {
    Vertex,
    Pixel,
    Compute,
    Geometry,
    Hull,
    Domain,
    Mesh,
    Amplification,
    RayGeneration,
    ClosestHit,
    Miss,
    Intersection,
    AnyHit,
    Callable,
};

}
