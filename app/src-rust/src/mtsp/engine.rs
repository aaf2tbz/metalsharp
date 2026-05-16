use serde::Serialize;
use std::sync::OnceLock;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum PipelineId {
    M9,
    M10,
    M11,
    M12,
    M32,
    FnaArm64,
    Steam,
    MacSteam,
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
    pub launch_args: Vec<&'static str>,
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
                description: "D3D11 -> Metal via DXMT",
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
                env_vars: vec![
                    EnvVar { key: "DXMT_METALFX_SPATIAL_SWAPCHAIN", value: "1" },
                    EnvVar { key: "DXMT_ASYNC_PIPELINE_COMPILE", value: "1" },
                ],
                launch_args: vec!["-dx11"],
                alternatives: vec![
                    PipelineId::M12,
                    PipelineId::M10,
                    PipelineId::M9,
                    PipelineId::Steam,
                    PipelineId::MacSteam,
                    PipelineId::WineBare,
                ],
                shader_cache_subdir: Some("m11"),
            },
            PipelineNode {
                id: PipelineId::M12,
                name: "M12",
                description: "D3D12 -> Metal via DXMT",
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
                    EnvVar { key: "DXMT_ASYNC_PIPELINE_COMPILE", value: "1" },
                ],
                launch_args: vec!["-dx12"],
                alternatives: vec![
                    PipelineId::M11,
                    PipelineId::M10,
                    PipelineId::M9,
                    PipelineId::Steam,
                    PipelineId::MacSteam,
                ],
                shader_cache_subdir: Some("m12"),
            },
            PipelineNode {
                id: PipelineId::M10,
                name: "M10",
                description: "D3D10 -> Metal via DXMT",
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
                env_vars: vec![EnvVar { key: "DXMT_ASYNC_PIPELINE_COMPILE", value: "1" }],
                launch_args: vec!["-dx10"],
                alternatives: vec![
                    PipelineId::M11,
                    PipelineId::M9,
                    PipelineId::Steam,
                    PipelineId::MacSteam,
                    PipelineId::WineBare,
                ],
                shader_cache_subdir: Some("m10"),
            },
            PipelineNode {
                id: PipelineId::M9,
                name: "M9",
                description: "D3D9 -> Metal via DXVK/MoltenVK",
                backend: "dxvk",
                experimental: false,
                requires_wine: true,
                wine_overrides: Some("d3d9=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"),
                dyld_paths: vec!["lib/wine/x86_64-unix"],
                deploy_dlls: vec![DllDeploy { source_subpath: "lib/dxvk/i386-windows", filename: "d3d9.dll" }],
                env_vars: vec![],
                launch_args: vec!["-dx9"],
                alternatives: vec![
                    PipelineId::M11,
                    PipelineId::M10,
                    PipelineId::M32,
                    PipelineId::Steam,
                    PipelineId::MacSteam,
                    PipelineId::WineBare,
                ],
                shader_cache_subdir: Some("m9"),
            },
            PipelineNode {
                id: PipelineId::M32,
                name: "M32",
                description: "32-bit Wine fallback",
                backend: "wine32",
                experimental: false,
                requires_wine: true,
                wine_overrides: None,
                dyld_paths: vec!["lib/wine/x86_64-unix"],
                deploy_dlls: vec![],
                env_vars: vec![],
                launch_args: vec![],
                alternatives: vec![PipelineId::M9, PipelineId::M11, PipelineId::Steam, PipelineId::MacSteam],
                shader_cache_subdir: Some("m32"),
            },
            PipelineNode {
                id: PipelineId::FnaArm64,
                name: "Native macOS",
                description: "FNA/XNA/Mono via native macOS runtime",
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
                launch_args: vec![],
                alternatives: vec![PipelineId::MacSteam, PipelineId::Steam],
                shader_cache_subdir: Some("fna-arm64"),
            },
            PipelineNode {
                id: PipelineId::Steam,
                name: "Steam",
                description: "Wine Steam",
                backend: "wine-steam",
                experimental: false,
                requires_wine: false,
                wine_overrides: None,
                dyld_paths: vec![],
                deploy_dlls: vec![],
                env_vars: vec![],
                launch_args: vec![],
                alternatives: vec![PipelineId::MacSteam, PipelineId::M11, PipelineId::WineBare],
                shader_cache_subdir: Some("steam-wine"),
            },
            PipelineNode {
                id: PipelineId::MacSteam,
                name: "MacOS Steam",
                description: "Native macOS Steam",
                backend: "macos-steam",
                experimental: false,
                requires_wine: false,
                wine_overrides: None,
                dyld_paths: vec![],
                deploy_dlls: vec![],
                env_vars: vec![],
                launch_args: vec![],
                alternatives: vec![PipelineId::Steam, PipelineId::FnaArm64, PipelineId::M11],
                shader_cache_subdir: Some("steam-native"),
            },
            PipelineNode {
                id: PipelineId::WineBare,
                name: "Wine",
                description: "Plain Wine (Custom Library)",
                backend: "wine",
                experimental: false,
                requires_wine: true,
                wine_overrides: None,
                dyld_paths: vec!["lib/wine/x86_64-unix"],
                deploy_dlls: vec![],
                env_vars: vec![],
                launch_args: vec![],
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
            "dxmt_metal" | "steam_d3dmetal_perf" | "steam_metalfx" => Some(PipelineId::M11),
            "dxmt_metal12" => Some(PipelineId::M12),
            "d3d9_metal" | "dxvk_metal32" => Some(PipelineId::M9),
            "wined3d_32" => Some(PipelineId::M32),
            "metalsharp_wine" => Some(PipelineId::WineBare),
            "steam" => Some(PipelineId::Steam),
            "macos_steam" | "mac_steam" | "native_steam" => Some(PipelineId::MacSteam),
            "xna_fna_arm64" | "xna_fna_x86" | "xna_fna" => Some(PipelineId::FnaArm64),
            _ => None,
        }
    }

    pub fn from_str_flexible(s: &str) -> Option<PipelineId> {
        if let Some(p) = Self::from_legacy_method(s) {
            return Some(p);
        }
        match s {
            "m11" | "steam_d3dmetal_perf" | "steam_metalfx" => Some(PipelineId::M11),
            "m12" => Some(PipelineId::M12),
            "m10" => Some(PipelineId::M10),
            "m9" | "m9_gl" | "m32_vk" => Some(PipelineId::M9),
            "m32" | "m32_w" => Some(PipelineId::M32),
            "fna_arm64" | "fna_x86" | "mono_generic" => Some(PipelineId::FnaArm64),
            "steam" | "wine_steam" => Some(PipelineId::Steam),
            "macos_steam" | "mac_steam" | "native_steam" => Some(PipelineId::MacSteam),
            "wine_bare" | "m64" => Some(PipelineId::WineBare),
            _ => None,
        }
    }

    pub fn to_legacy_method(self) -> &'static str {
        match self {
            PipelineId::M9 => "d3d9_metal",
            PipelineId::M10 => "d3d10_metal",
            PipelineId::M11 => "dxmt_metal",
            PipelineId::M12 => "dxmt_metal12",
            PipelineId::M32 => "wined3d_32",
            PipelineId::FnaArm64 => "xna_fna_arm64",
            PipelineId::Steam => "steam",
            PipelineId::MacSteam => "macos_steam",
            PipelineId::WineBare => "metalsharp_wine",
        }
    }
}
