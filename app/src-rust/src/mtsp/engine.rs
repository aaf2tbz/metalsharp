use serde::Serialize;
use std::sync::OnceLock;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum PipelineId {
    M11,
    M12,
    FnaArm64,
    Steam,
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
                name: "DXMT Metal",
                description: "D3D9/10/11/12 via DXMT Metal",
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
                alternatives: vec![PipelineId::WineBare],
                shader_cache_subdir: Some("dxmt-metal"),
            },
            PipelineNode {
                id: PipelineId::M12,
                name: "DXMT Metal 12",
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
                alternatives: vec![PipelineId::M11],
                shader_cache_subdir: Some("dxmt-metal12"),
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
                alternatives: vec![PipelineId::Steam],
                shader_cache_subdir: Some("fna-arm64"),
            },
            PipelineNode {
                id: PipelineId::Steam,
                name: "Steam",
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
            "dxmt_metal" | "d3d9_metal" | "wined3d_32" | "dxvk_metal32" | "steam_d3dmetal_perf"
            | "steam_metalfx" => Some(PipelineId::M11),
            "dxmt_metal12" => Some(PipelineId::M12),
            "metalsharp_wine" => Some(PipelineId::WineBare),
            "steam" => Some(PipelineId::Steam),
            "xna_fna_arm64" | "xna_fna_x86" | "xna_fna" => Some(PipelineId::FnaArm64),
            _ => None,
        }
    }

    pub fn from_str_flexible(s: &str) -> Option<PipelineId> {
        if let Some(p) = Self::from_legacy_method(s) {
            return Some(p);
        }
        match s {
            "m11" | "m9" | "m9_gl" | "m32_vk" | "m32_w" | "steam_d3dmetal_perf" | "steam_metalfx" => {
                Some(PipelineId::M11)
            },
            "m12" => Some(PipelineId::M12),
            "fna_arm64" | "fna_x86" | "mono_generic" => Some(PipelineId::FnaArm64),
            "steam" => Some(PipelineId::Steam),
            "wine_bare" | "m64" => Some(PipelineId::WineBare),
            _ => None,
        }
    }

    pub fn to_legacy_method(self) -> &'static str {
        match self {
            PipelineId::M11 => "dxmt_metal",
            PipelineId::M12 => "dxmt_metal12",
            PipelineId::FnaArm64 => "xna_fna_arm64",
            PipelineId::Steam => "steam",
            PipelineId::WineBare => "metalsharp_wine",
        }
    }
}
