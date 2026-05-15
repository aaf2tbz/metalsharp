use serde::Serialize;
use std::sync::OnceLock;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum PipelineId {
    M11,
    M12,
    M9,
    M9Gl,
    M32Vk,
    M32W,
    M64,
    Steam,
    SteamMetalfx,
    #[serde(rename = "steam_d3dmetal_perf")]
    SteamD3DMetalPerf,
    FnaArm64,
    FnaX86,
    MonoGeneric,
    WineBare,
}

#[derive(Debug, Clone, Serialize)]
pub struct DllDeploy {
    pub source_subpath: &'static str,
    pub filename: &'static str,
}

#[derive(Debug, Clone, Serialize)]
pub struct EnvVar {
    pub key: &'static str,
    pub value: &'static str,
}

#[derive(Debug, Clone, Serialize)]
pub struct PipelineNode {
    pub id: PipelineId,
    pub name: &'static str,
    pub description: &'static str,
    pub backend: &'static str,
    pub experimental: bool,
    pub requires_wine: bool,
    pub wine_overrides: Option<&'static str>,
    pub dyld_paths: Vec<&'static str>,
    pub deploy_dlls: Vec<DllDeploy>,
    pub env_vars: Vec<EnvVar>,
    pub alternatives: Vec<PipelineId>,
    pub shader_cache_subdir: Option<&'static str>,
}

static PIPELINES: OnceLock<Vec<PipelineNode>> = OnceLock::new();

pub fn pipelines() -> &'static Vec<PipelineNode> {
    PIPELINES.get_or_init(|| {
        vec![
            PipelineNode {
                id: PipelineId::M11,
                name: "M11",
                description: "D3D11 via DXMT Metal",
                backend: "dxmt",
                experimental: false,
                requires_wine: true,
                wine_overrides: Some("dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"),
                dyld_paths: vec!["lib/wine/x86_64-unix", "lib/dxmt/x86_64-unix"],
                deploy_dlls: vec![
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "d3d11.dll" },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "dxgi.dll" },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "d3d10core.dll" },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "winemetal.dll" },
                ],
                env_vars: vec![EnvVar { key: "DXMT_METALFX_SPATIAL_SWAPCHAIN", value: "1" }],
                alternatives: vec![PipelineId::SteamD3DMetalPerf],
                shader_cache_subdir: Some("dxmt-metal"),
            },
            PipelineNode {
                id: PipelineId::M12,
                name: "M12",
                description: "D3D12 via DXMT Metal",
                backend: "dxmt",
                experimental: true,
                requires_wine: true,
                wine_overrides: Some("d3d12,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"),
                dyld_paths: vec!["lib/wine/x86_64-unix", "lib/dxmt/x86_64-unix"],
                deploy_dlls: vec![
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "d3d12.dll" },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "d3d11.dll" },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "dxgi.dll" },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "d3d10core.dll" },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "winemetal.dll" },
                ],
                env_vars: vec![
                    EnvVar { key: "DXMT_METALFX_SPATIAL_SWAPCHAIN", value: "1" },
                    EnvVar { key: "DXMT_METALFX_SPATIAL", value: "1" },
                    EnvVar { key: "DXMT_METALFX_TEMPORAL", value: "1" },
                ],
                alternatives: vec![PipelineId::SteamD3DMetalPerf],
                shader_cache_subdir: Some("dxmt-metal12"),
            },
            PipelineNode {
                id: PipelineId::M9,
                name: "M9",
                description: "D3D9 via DXMT Metal",
                backend: "d3d9-metal",
                experimental: true,
                requires_wine: true,
                wine_overrides: Some("d3d9=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"),
                dyld_paths: vec!["lib/wine/x86_64-unix"],
                deploy_dlls: vec![],
                env_vars: vec![],
                alternatives: vec![PipelineId::M9Gl, PipelineId::M32Vk],
                shader_cache_subdir: Some("d3d9-metal"),
            },
            PipelineNode {
                id: PipelineId::M9Gl,
                name: "M9 GL",
                description: "D3D9 via WineD3D OpenGL",
                backend: "wined3d",
                experimental: false,
                requires_wine: true,
                wine_overrides: Some(
                    "dxgi,d3d11=b;gameoverlayrenderer,gameoverlayrenderer64=d;steamclient64,steamclient=d",
                ),
                dyld_paths: vec!["lib/wine/x86_64-unix"],
                deploy_dlls: vec![],
                env_vars: vec![EnvVar { key: "SteamOverlayDisabled", value: "1" }],
                alternatives: vec![PipelineId::M9, PipelineId::M32Vk],
                shader_cache_subdir: Some("wined3d-gl"),
            },
            PipelineNode {
                id: PipelineId::M32Vk,
                name: "M32 VK",
                description: "32-bit D3D9 via DXVK MoltenVK",
                backend: "dxvk",
                experimental: false,
                requires_wine: true,
                wine_overrides: Some("d3d9=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"),
                dyld_paths: vec!["lib/wine/x86_64-unix"],
                deploy_dlls: vec![DllDeploy { source_subpath: "lib/dxvk/i386-windows", filename: "d3d9.dll" }],
                env_vars: vec![],
                alternatives: vec![PipelineId::M9, PipelineId::M9Gl],
                shader_cache_subdir: Some("dxvk-metal32"),
            },
            PipelineNode {
                id: PipelineId::M32W,
                name: "M32 GL",
                description: "32-bit D3D9 via WineD3D OpenGL",
                backend: "wined3d",
                experimental: false,
                requires_wine: true,
                wine_overrides: Some("d3d9=b;gameoverlayrenderer,gameoverlayrenderer64=d"),
                dyld_paths: vec!["lib/wine/x86_64-unix"],
                deploy_dlls: vec![],
                env_vars: vec![],
                alternatives: vec![PipelineId::M32Vk, PipelineId::M9],
                shader_cache_subdir: Some("wined3d-32"),
            },
            PipelineNode {
                id: PipelineId::M64,
                name: "M64",
                description: "64-bit Wine bare",
                backend: "wine",
                experimental: false,
                requires_wine: true,
                wine_overrides: None,
                dyld_paths: vec!["lib/wine/x86_64-unix"],
                deploy_dlls: vec![],
                env_vars: vec![],
                alternatives: vec![],
                shader_cache_subdir: Some("wine-64"),
            },
            PipelineNode {
                id: PipelineId::Steam,
                name: "Steam Native",
                description: "macOS native launch via Steam",
                backend: "steam",
                experimental: false,
                requires_wine: false,
                wine_overrides: None,
                dyld_paths: vec![],
                deploy_dlls: vec![],
                env_vars: vec![],
                alternatives: vec![],
                shader_cache_subdir: Some("steam-native"),
            },
            PipelineNode {
                id: PipelineId::SteamMetalfx,
                name: "D3DMetal FX",
                description: "Apple D3DMetal with MetalFX upscaling",
                backend: "steam-d3dmetal",
                experimental: false,
                requires_wine: true,
                wine_overrides: None,
                dyld_paths: vec![],
                deploy_dlls: vec![],
                env_vars: vec![
                    EnvVar { key: "D3DM_ENABLE_METALFX", value: "1" },
                    EnvVar { key: "D3DM_ENABLE_ASYNC_COMMIT", value: "1" },
                    EnvVar { key: "D3DM_MULTITHREADED_INTERFACE_ENABLE", value: "1" },
                    EnvVar { key: "D3DM_IGNORE_D3D11_RENDER_BARRIERS", value: "1" },
                    EnvVar { key: "D3DM_SAMPLE_NAN_TO_ZERO", value: "1" },
                    EnvVar { key: "D3DM_FLUSH_POS_INF_TO_NAN", value: "1" },
                    EnvVar { key: "MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE", value: "1" },
                    EnvVar { key: "MVK_ALLOW_METAL_FENCES", value: "1" },
                ],
                alternatives: vec![PipelineId::SteamD3DMetalPerf],
                shader_cache_subdir: Some("steam-metalfx"),
            },
            PipelineNode {
                id: PipelineId::SteamD3DMetalPerf,
                name: "D3DMetal",
                description: "Apple D3DMetal",
                backend: "steam-d3dmetal",
                experimental: false,
                requires_wine: true,
                wine_overrides: None,
                dyld_paths: vec![],
                deploy_dlls: vec![],
                env_vars: vec![
                    EnvVar { key: "D3DM_ENABLE_ASYNC_COMMIT", value: "1" },
                    EnvVar { key: "D3DM_MULTITHREADED_INTERFACE_ENABLE", value: "1" },
                    EnvVar { key: "D3DM_IGNORE_D3D11_RENDER_BARRIERS", value: "1" },
                    EnvVar { key: "D3DM_SAMPLE_NAN_TO_ZERO", value: "1" },
                    EnvVar { key: "D3DM_FLUSH_POS_INF_TO_NAN", value: "1" },
                ],
                alternatives: vec![PipelineId::SteamMetalfx, PipelineId::M11],
                shader_cache_subdir: Some("steam-d3dmetal"),
            },
            PipelineNode {
                id: PipelineId::FnaArm64,
                name: "Mono ARM",
                description: "FNA/XNA via Mono ARM64",
                backend: "mono",
                experimental: false,
                requires_wine: false,
                wine_overrides: None,
                dyld_paths: vec![],
                deploy_dlls: vec![],
                env_vars: vec![
                    EnvVar { key: "FNA3D_DRIVER", value: "OpenGL" },
                    EnvVar { key: "METAL_DEVICE_WRAPPER_TYPE", value: "0" },
                ],
                alternatives: vec![],
                shader_cache_subdir: Some("fna-arm64"),
            },
            PipelineNode {
                id: PipelineId::FnaX86,
                name: "Mono x86",
                description: "FNA/XNA via Mono x86 Rosetta",
                backend: "mono",
                experimental: false,
                requires_wine: false,
                wine_overrides: None,
                dyld_paths: vec![],
                deploy_dlls: vec![],
                env_vars: vec![
                    EnvVar { key: "FNA3D_DRIVER", value: "OpenGL" },
                    EnvVar { key: "METAL_DEVICE_WRAPPER_TYPE", value: "0" },
                ],
                alternatives: vec![],
                shader_cache_subdir: Some("fna-x86"),
            },
            PipelineNode {
                id: PipelineId::MonoGeneric,
                name: "Mono",
                description: "Generic Mono runtime",
                backend: "mono",
                experimental: false,
                requires_wine: false,
                wine_overrides: None,
                dyld_paths: vec![],
                deploy_dlls: vec![],
                env_vars: vec![EnvVar { key: "METAL_DEVICE_WRAPPER_TYPE", value: "0" }],
                alternatives: vec![],
                shader_cache_subdir: Some("mono-generic"),
            },
            PipelineNode {
                id: PipelineId::WineBare,
                name: "Wine",
                description: "Plain Wine",
                backend: "wine",
                experimental: false,
                requires_wine: true,
                wine_overrides: None,
                dyld_paths: vec!["lib/wine/x86_64-unix"],
                deploy_dlls: vec![],
                env_vars: vec![],
                alternatives: vec![],
                shader_cache_subdir: Some("wine-bare"),
            },
        ]
    })
}

pub fn get_pipeline(id: PipelineId) -> &'static PipelineNode {
    pipelines().iter().find(|p| p.id == id).expect("pipeline not found")
}

impl PipelineId {
    pub fn from_legacy_method(method: &str) -> Option<PipelineId> {
        match method {
            "dxmt_metal" => Some(PipelineId::M11),
            "dxmt_metal12" => Some(PipelineId::M12),
            "wined3d_32" => Some(PipelineId::M9Gl),
            "dxvk_metal32" => Some(PipelineId::M32Vk),
            "metalsharp_wine" => Some(PipelineId::M64),
            "steam" => Some(PipelineId::Steam),
            "steam_metalfx" => Some(PipelineId::SteamMetalfx),
            "steam_d3dmetal_perf" => Some(PipelineId::SteamD3DMetalPerf),
            "xna_fna_arm64" => Some(PipelineId::FnaArm64),
            "xna_fna_x86" => Some(PipelineId::FnaX86),
            "xna_fna" => Some(PipelineId::FnaArm64),
            _ => None,
        }
    }

    pub fn from_str_flexible(s: &str) -> Option<PipelineId> {
        if let Some(p) = Self::from_legacy_method(s) {
            return Some(p);
        }
        match s {
            "m11" => Some(PipelineId::M11),
            "m12" => Some(PipelineId::M12),
            "m9" => Some(PipelineId::M9),
            "m9_gl" => Some(PipelineId::M9Gl),
            "m32_vk" => Some(PipelineId::M32Vk),
            "m32_w" => Some(PipelineId::M32W),
            "m64" => Some(PipelineId::M64),
            "steam" => Some(PipelineId::Steam),
            "steam_metalfx" => Some(PipelineId::SteamMetalfx),
            "steam_d3dmetal_perf" | "steam_d3_d_metal_perf" => Some(PipelineId::SteamD3DMetalPerf),
            "fna_arm64" => Some(PipelineId::FnaArm64),
            "fna_x86" => Some(PipelineId::FnaX86),
            "mono_generic" => Some(PipelineId::MonoGeneric),
            "wine_bare" => Some(PipelineId::WineBare),
            _ => None,
        }
    }

    pub fn to_legacy_method(self) -> &'static str {
        match self {
            PipelineId::M11 => "dxmt_metal",
            PipelineId::M12 => "dxmt_metal12",
            PipelineId::M9 => "dxmt_metal",
            PipelineId::M9Gl => "wined3d_32",
            PipelineId::M32Vk => "dxvk_metal32",
            PipelineId::M32W => "wined3d_32",
            PipelineId::M64 => "metalsharp_wine",
            PipelineId::Steam => "steam",
            PipelineId::SteamMetalfx => "steam_metalfx",
            PipelineId::SteamD3DMetalPerf => "steam_d3dmetal_perf",
            PipelineId::FnaArm64 => "xna_fna_arm64",
            PipelineId::FnaX86 => "xna_fna_x86",
            PipelineId::MonoGeneric => "xna_fna",
            PipelineId::WineBare => "metalsharp_wine",
        }
    }
}
