use serde::Serialize;
use std::sync::OnceLock;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum PipelineId {
    Dxmt,
    M9,
    M10,
    M11,
    M12,
    DxvkD9,
    DxvkD11,
    Vkd3dD12,
    D3DMetalNative,
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
    pub dest_filename: Option<&'static str>,
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
    pub graphics_backend: &'static str,
    pub experimental: bool,
    pub requires_wine: bool,
    pub wine_overrides: Option<&'static str>,
    pub dyld_paths: Vec<&'static str>,
    pub winedllpath_dirs: Vec<&'static str>,
    pub deploy_dlls: Vec<DllDeploy>,
    pub env_vars: Vec<EnvVar>,
    pub launch_args: Vec<&'static str>,
    pub alternatives: Vec<PipelineId>,
    pub shader_cache_subdir: Option<&'static str>,
}

impl PipelineNode {
    pub fn uses_winedllpath_routing(&self) -> bool {
        !self.winedllpath_dirs.is_empty()
    }
}

static PIPELINES: OnceLock<Vec<PipelineNode>> = OnceLock::new();
const DXMT_70_PERCENT_UPSCALE_CONFIG: &str = "d3d11.metalSpatialUpscaleFactor=1.43;d3d11.preferredMaxFrameRate=60";
const DXMT_M12_SAFE_CONFIG: &str = "d3d11.metalSpatialUpscaleFactor=1.43;d3d11.preferredMaxFrameRate=60";

/// Backend value and MetalSharp-owned ABI variable names for the native
/// D3DMetal lane. These must never carry the oracle's branding; the oracle
/// is only ever consulted for behavior. See
/// `docs/runtime/d3dmetal-native-contract.md` for the full contract.
pub const D3DMETAL_NATIVE_BACKEND: &str = "d3dmetal_native";
pub const MS_GRAPHICS_BACKEND_ENV: &str = "MS_GRAPHICS_BACKEND";
pub const MS_ACTIVE_GRAPHICS_BACKEND_ENV: &str = "MS_ACTIVE_GRAPHICS_BACKEND";
pub const MS_D3DMETAL_SHARED_PATH_ENV: &str = "MS_D3DMETAL_SHARED_PATH";
pub const MS_D3DMETAL_FRAMEWORK_PATH_ENV: &str = "MS_D3DMETAL_FRAMEWORK_PATH";

pub fn pipelines() -> &'static Vec<PipelineNode> {
    PIPELINES.get_or_init(|| {
        vec![
            PipelineNode {
                id: PipelineId::Dxmt,
                name: "DXMT",
                description: "Auto-selected D3D9/D3D10/D3D11/D3D12 -> Metal via unified DXMT runtime",
                backend: "dxmt",
                graphics_backend: "dxmt",
                experimental: false,
                requires_wine: true,
                wine_overrides: None,
                dyld_paths: vec![],
                winedllpath_dirs: vec![],
                deploy_dlls: vec![],
                env_vars: vec![],
                launch_args: vec![],
                alternatives: vec![PipelineId::M12, PipelineId::M11, PipelineId::M10, PipelineId::M9],
                shader_cache_subdir: None,
            },
            PipelineNode {
                id: PipelineId::M12,
                name: "M12",
                description: "D3D12 -> Metal via DXMT",
                backend: "dxmt",
                graphics_backend: "dxmt",
                experimental: false,
                requires_wine: true,
                wine_overrides: Some(
                    "winemetal,d3d12,dxgi,dxgi_dxmt,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
                ),
                dyld_paths: vec!["lib/dxmt_m12/x86_64-unix", "lib/wine/x86_64-unix"],
                winedllpath_dirs: vec!["lib/dxmt_m12/x86_64-windows"],
                deploy_dlls: vec![
                    DllDeploy {
                        source_subpath: "lib/dxmt_m12/x86_64-windows",
                        filename: "d3d12.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt_m12/x86_64-windows",
                        filename: "d3d11.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt_m12/x86_64-windows",
                        filename: "dxgi.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt_m12/x86_64-windows",
                        filename: "dxgi_dxmt.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt_m12/x86_64-windows",
                        filename: "d3d10core.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt_m12/x86_64-windows",
                        filename: "winemetal.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt_m12/x86_64-windows",
                        filename: "nvapi64.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt_m12/x86_64-windows",
                        filename: "nvngx.dll",
                        dest_filename: None,
                    },
                ],
                env_vars: vec![
                    EnvVar { key: "DXMT_METALFX_SPATIAL_SWAPCHAIN", value: "1" },
                    EnvVar { key: "DXMT_METALFX_SPATIAL", value: "1" },
                    EnvVar { key: "DXMT_METALFX_TEMPORAL", value: "1" },
                    EnvVar { key: "DXMT_ASYNC_PIPELINE_COMPILE", value: "1" },
                    EnvVar { key: "DXMT_D3D12_UE_SM6_COMPAT", value: "1" },
                    EnvVar { key: "DXMT_D3D12_PSO_WORKERS", value: "6" },
                    EnvVar { key: "DXMT_CONFIG", value: DXMT_M12_SAFE_CONFIG },
                ],
                launch_args: vec!["-windowed", "-ResX=1280", "-ResY=720", "-ForceRes"],
                alternatives: vec![
                    PipelineId::Vkd3dD12,
                    PipelineId::M11,
                    PipelineId::M10,
                    PipelineId::M9,
                    PipelineId::Steam,
                    PipelineId::MacSteam,
                ],
                shader_cache_subdir: Some("m12"),
            },
            PipelineNode {
                id: PipelineId::M11,
                name: "M11",
                description: "D3D11 -> Metal via DXMT",
                backend: "dxmt",
                graphics_backend: "dxmt",
                experimental: false,
                requires_wine: true,
                wine_overrides: Some("winemetal,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"),
                dyld_paths: vec!["lib/wine/x86_64-unix", "lib/dxmt/x86_64-unix"],
                winedllpath_dirs: vec!["lib/dxmt/x86_64-windows", "lib/metalsharp/x86_64-windows"],
                deploy_dlls: vec![
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "d3d11.dll", dest_filename: None },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "dxgi.dll", dest_filename: None },
                    DllDeploy {
                        source_subpath: "lib/dxmt/x86_64-windows",
                        filename: "dxgi_dxmt.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt/x86_64-windows",
                        filename: "d3d10core.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt/x86_64-windows",
                        filename: "winemetal.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt/x86_64-windows",
                        filename: "nvapi64.dll",
                        dest_filename: None,
                    },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "nvngx.dll", dest_filename: None },
                    DllDeploy {
                        source_subpath: "lib/metalsharp/x86_64-windows",
                        filename: "metalsharp_ntdll_hook.dll",
                        dest_filename: None,
                    },
                ],
                env_vars: vec![
                    EnvVar { key: "DXMT_METALFX_SPATIAL_SWAPCHAIN", value: "1" },
                    EnvVar { key: "DXMT_ASYNC_PIPELINE_COMPILE", value: "1" },
                    EnvVar { key: "DXMT_CONFIG", value: DXMT_70_PERCENT_UPSCALE_CONFIG },
                ],
                launch_args: vec![],
                alternatives: vec![
                    PipelineId::M12,
                    PipelineId::DxvkD11,
                    PipelineId::M10,
                    PipelineId::M9,
                    PipelineId::Steam,
                    PipelineId::MacSteam,
                    PipelineId::WineBare,
                ],
                shader_cache_subdir: Some("m11"),
            },
            PipelineNode {
                id: PipelineId::M10,
                name: "M10",
                description: "D3D10 -> Metal via DXMT",
                backend: "dxmt",
                graphics_backend: "dxmt",
                experimental: false,
                requires_wine: true,
                wine_overrides: Some(
                    "winemetal,d3d10,d3d10_1,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
                ),
                dyld_paths: vec!["lib/wine/x86_64-unix", "lib/dxmt/x86_64-unix"],
                winedllpath_dirs: vec![
                    "lib/wine/x86_64-windows",
                    "lib/dxmt/x86_64-windows",
                    "lib/metalsharp/x86_64-windows",
                ],
                deploy_dlls: vec![
                    DllDeploy { source_subpath: "lib/wine/x86_64-windows", filename: "d3d10.dll", dest_filename: None },
                    DllDeploy {
                        source_subpath: "lib/wine/x86_64-windows",
                        filename: "d3d10_1.dll",
                        dest_filename: None,
                    },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "d3d11.dll", dest_filename: None },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "dxgi.dll", dest_filename: None },
                    DllDeploy {
                        source_subpath: "lib/dxmt/x86_64-windows",
                        filename: "dxgi_dxmt.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt/x86_64-windows",
                        filename: "d3d10core.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt/x86_64-windows",
                        filename: "winemetal.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/dxmt/x86_64-windows",
                        filename: "nvapi64.dll",
                        dest_filename: None,
                    },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "nvngx.dll", dest_filename: None },
                    DllDeploy {
                        source_subpath: "lib/metalsharp/x86_64-windows",
                        filename: "metalsharp_ntdll_hook.dll",
                        dest_filename: None,
                    },
                ],
                env_vars: vec![
                    EnvVar { key: "DXMT_METALFX_SPATIAL_SWAPCHAIN", value: "1" },
                    EnvVar { key: "DXMT_ASYNC_PIPELINE_COMPILE", value: "1" },
                    EnvVar { key: "DXMT_CONFIG", value: DXMT_70_PERCENT_UPSCALE_CONFIG },
                ],
                launch_args: vec![],
                alternatives: vec![
                    PipelineId::M11,
                    PipelineId::DxvkD11,
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
                description: "D3D9 -> Metal via DXMT launch family",
                backend: "dxmt",
                graphics_backend: "dxmt",
                experimental: false,
                requires_wine: true,
                wine_overrides: Some("d3d9=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"),
                dyld_paths: vec!["lib/wine/x86_64-unix", "lib/dxmt/x86_64-unix"],
                winedllpath_dirs: vec![
                    "lib/wine/x86_64-windows",
                    "lib/wine/i386-windows",
                    "lib/dxmt/x86_64-windows",
                    "lib/metalsharp/x86_64-windows",
                ],
                deploy_dlls: vec![
                    DllDeploy { source_subpath: "lib/wine/x86_64-windows", filename: "d3d9.dll", dest_filename: None },
                    DllDeploy { source_subpath: "lib/wine/i386-windows", filename: "d3d9.dll", dest_filename: None },
                    DllDeploy { source_subpath: "lib/wine/i386-windows", filename: "dxgi.dll", dest_filename: None },
                    DllDeploy {
                        source_subpath: "lib/dxmt/x86_64-windows",
                        filename: "nvapi64.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/metalsharp/x86_64-windows",
                        filename: "metalsharp_ntdll_hook.dll",
                        dest_filename: None,
                    },
                ],
                env_vars: vec![
                    EnvVar { key: "DXMT_METALFX_SPATIAL_SWAPCHAIN", value: "1" },
                    EnvVar { key: "DXMT_ASYNC_PIPELINE_COMPILE", value: "1" },
                    EnvVar { key: "DXMT_CONFIG", value: DXMT_70_PERCENT_UPSCALE_CONFIG },
                ],
                launch_args: vec![],
                alternatives: vec![
                    PipelineId::DxvkD9,
                    PipelineId::M11,
                    PipelineId::M10,
                    PipelineId::Steam,
                    PipelineId::MacSteam,
                ],
                shader_cache_subdir: Some("m9"),
            },
            PipelineNode {
                id: PipelineId::DxvkD9,
                name: "DXVK D3D9",
                description: "D3D9 -> Vulkan via DXVK, then MoltenVK",
                backend: "dxvk",
                graphics_backend: "vulkan",
                experimental: true,
                requires_wine: true,
                wine_overrides: Some("d3d9,dxgi=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"),
                dyld_paths: vec!["lib/wine/x86_64-unix", "lib/dxvk/x86_64-unix"],
                winedllpath_dirs: vec!["lib/dxvk/x86_64-windows", "lib/dxvk/i386-windows"],
                deploy_dlls: vec![
                    DllDeploy { source_subpath: "lib/dxvk/x86_64-windows", filename: "d3d9.dll", dest_filename: None },
                    DllDeploy { source_subpath: "lib/dxvk/x86_64-windows", filename: "dxgi.dll", dest_filename: None },
                    DllDeploy { source_subpath: "lib/dxvk/i386-windows", filename: "d3d9.dll", dest_filename: None },
                    DllDeploy { source_subpath: "lib/dxvk/i386-windows", filename: "dxgi.dll", dest_filename: None },
                ],
                env_vars: vec![
                    EnvVar { key: "DXVK_ASYNC", value: "1" },
                    EnvVar { key: "DXVK_STATE_CACHE", value: "1" },
                ],
                launch_args: vec![],
                alternatives: vec![PipelineId::M9, PipelineId::WineBare],
                shader_cache_subdir: Some("dxvk-d9"),
            },
            PipelineNode {
                id: PipelineId::DxvkD11,
                name: "DXVK D3D11",
                description: "D3D10/D3D11 -> Vulkan via DXVK, then MoltenVK",
                backend: "dxvk",
                graphics_backend: "vulkan",
                experimental: true,
                requires_wine: true,
                wine_overrides: Some("d3d10,d3d10_1,d3d11,dxgi=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"),
                dyld_paths: vec!["lib/wine/x86_64-unix", "lib/dxvk/x86_64-unix"],
                winedllpath_dirs: vec!["lib/dxvk/x86_64-windows", "lib/dxvk/i386-windows"],
                deploy_dlls: vec![
                    DllDeploy {
                        source_subpath: "lib/dxvk/x86_64-windows",
                        filename: "d3d10core.dll",
                        dest_filename: None,
                    },
                    DllDeploy { source_subpath: "lib/dxvk/x86_64-windows", filename: "d3d11.dll", dest_filename: None },
                    DllDeploy { source_subpath: "lib/dxvk/x86_64-windows", filename: "dxgi.dll", dest_filename: None },
                ],
                env_vars: vec![
                    EnvVar { key: "DXVK_ASYNC", value: "1" },
                    EnvVar { key: "DXVK_STATE_CACHE", value: "1" },
                ],
                launch_args: vec![],
                alternatives: vec![PipelineId::M11, PipelineId::M10, PipelineId::WineBare],
                shader_cache_subdir: Some("dxvk-d11"),
            },
            PipelineNode {
                id: PipelineId::Vkd3dD12,
                name: "VKD3D-Proton D3D12",
                description: "D3D12 -> Vulkan via VKD3D-Proton, then MoltenVK",
                backend: "vkd3d",
                graphics_backend: "vulkan",
                experimental: true,
                requires_wine: true,
                wine_overrides: Some("d3d12,dxgi=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"),
                dyld_paths: vec!["lib/wine/x86_64-unix", "lib/vkd3d/x86_64-unix"],
                winedllpath_dirs: vec!["lib/vkd3d/x86_64-windows"],
                deploy_dlls: vec![
                    DllDeploy {
                        source_subpath: "lib/vkd3d/x86_64-windows",
                        filename: "d3d12.dll",
                        dest_filename: None,
                    },
                    DllDeploy {
                        source_subpath: "lib/vkd3d/x86_64-windows",
                        filename: "d3d12core.dll",
                        dest_filename: None,
                    },
                    DllDeploy { source_subpath: "lib/vkd3d/x86_64-windows", filename: "dxgi.dll", dest_filename: None },
                ],
                env_vars: vec![EnvVar { key: "VKD3D_CONFIG", value: "dxr" }],
                launch_args: vec![],
                alternatives: vec![PipelineId::M12, PipelineId::M11],
                shader_cache_subdir: Some("vkd3d-d12"),
            },
            PipelineNode {
                id: PipelineId::D3DMetalNative,
                name: "D3DMetal Native",
                description:
                    "MetalSharp-owned native D3DMetal (Wine 11.5 host ABI + Apple D3DMetal payload) — reserved",
                backend: D3DMETAL_NATIVE_BACKEND,
                graphics_backend: D3DMETAL_NATIVE_BACKEND,
                experimental: true,
                requires_wine: true,
                wine_overrides: Some(
                    "d3d10,d3d11,d3d12,dxgi,nvapi64,nvngx-on-metalfx=b,n;gameoverlayrenderer,gameoverlayrenderer64=d",
                ),
                // WINEDLLPATH/DYLD paths and the MS_D3DMETAL_*_PATH vars are
                // resolved at launch in a later phase; declared here so the
                // ABI names are frozen and grep-discoverable now.
                dyld_paths: vec![],
                winedllpath_dirs: vec![],
                deploy_dlls: vec![],
                env_vars: vec![
                    EnvVar { key: MS_GRAPHICS_BACKEND_ENV, value: D3DMETAL_NATIVE_BACKEND },
                    EnvVar { key: MS_ACTIVE_GRAPHICS_BACKEND_ENV, value: D3DMETAL_NATIVE_BACKEND },
                ],
                launch_args: vec![],
                alternatives: vec![PipelineId::M12, PipelineId::M11],
                shader_cache_subdir: Some("d3dmetal_native"),
            },
            PipelineNode {
                id: PipelineId::M32,
                name: "M32",
                description: "32-bit Wine fallback",
                backend: "wine32",
                graphics_backend: "wine",
                experimental: false,
                requires_wine: true,
                wine_overrides: None,
                dyld_paths: vec!["lib/wine/x86_64-unix"],
                winedllpath_dirs: vec![],
                deploy_dlls: vec![],
                env_vars: vec![],
                launch_args: vec![],
                alternatives: vec![PipelineId::M9, PipelineId::M11, PipelineId::Steam, PipelineId::MacSteam],
                shader_cache_subdir: Some("m32"),
            },
            PipelineNode {
                id: PipelineId::FnaArm64,
                name: "Mono/FNA",
                description: "Windows XNA/FNA via MetalSharp Mono runtime",
                backend: "mono",
                graphics_backend: "native",
                experimental: false,
                requires_wine: false,
                wine_overrides: None,
                dyld_paths: vec![],
                winedllpath_dirs: vec![],
                deploy_dlls: vec![],
                env_vars: vec![EnvVar { key: "METAL_DEVICE_WRAPPER_TYPE", value: "0" }],
                launch_args: vec![],
                alternatives: vec![PipelineId::MacSteam, PipelineId::Steam],
                shader_cache_subdir: Some("fna-arm64"),
            },
            PipelineNode {
                id: PipelineId::Steam,
                name: "Steam",
                description: "Wine Steam",
                backend: "wine-steam",
                graphics_backend: "wine",
                experimental: false,
                requires_wine: false,
                wine_overrides: None,
                dyld_paths: vec![],
                winedllpath_dirs: vec![],
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
                graphics_backend: "native",
                experimental: false,
                requires_wine: false,
                wine_overrides: None,
                dyld_paths: vec![],
                winedllpath_dirs: vec![],
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
                graphics_backend: "wine",
                experimental: false,
                requires_wine: true,
                wine_overrides: None,
                dyld_paths: vec!["lib/wine/x86_64-unix"],
                winedllpath_dirs: vec![],
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
    pub fn is_dxmt_family(self) -> bool {
        matches!(self, PipelineId::Dxmt | PipelineId::M9 | PipelineId::M10 | PipelineId::M11 | PipelineId::M12)
    }

    pub fn is_vulkan_family(self) -> bool {
        matches!(self, PipelineId::DxvkD9 | PipelineId::DxvkD11 | PipelineId::Vkd3dD12)
    }

    pub fn is_user_selectable(self) -> bool {
        matches!(
            self,
            PipelineId::M12
                | PipelineId::Vkd3dD12
                | PipelineId::M11
                | PipelineId::DxvkD11
                | PipelineId::M10
                | PipelineId::M9
                | PipelineId::DxvkD9
                | PipelineId::FnaArm64
        )
    }

    pub fn user_selectable_id(self) -> Option<&'static str> {
        match self {
            PipelineId::M12 => Some("m12"),
            PipelineId::Vkd3dD12 => Some("vkd3d_d12"),
            PipelineId::M11 => Some("m11"),
            PipelineId::DxvkD11 => Some("dxvk_d11"),
            PipelineId::M10 => Some("m10"),
            PipelineId::M9 => Some("m9"),
            PipelineId::DxvkD9 => Some("dxvk_d9"),
            PipelineId::FnaArm64 => Some("fna_arm64"),
            _ => None,
        }
    }

    pub fn user_selectable_name(self) -> Option<&'static str> {
        match self {
            PipelineId::M12 => Some("M12"),
            PipelineId::Vkd3dD12 => Some("VKD3D D3D12"),
            PipelineId::M11 => Some("M11"),
            PipelineId::DxvkD11 => Some("DXVK D3D11"),
            PipelineId::M10 => Some("M10"),
            PipelineId::M9 => Some("M9"),
            PipelineId::DxvkD9 => Some("DXVK D3D9"),
            PipelineId::FnaArm64 => Some("Mono/FNA"),
            _ => None,
        }
    }

    pub fn from_legacy_method(method: &str) -> Option<PipelineId> {
        match method.trim().to_ascii_lowercase().as_str() {
            "dxmt" => Some(PipelineId::Dxmt),
            "dxmt_metal" | "steam_d3dmetal_perf" | "steam_metalfx" => Some(PipelineId::M11),
            "dxmt_metal12" => Some(PipelineId::M12),
            "d3d9_metal" => Some(PipelineId::M9),
            "dxvk_d9" | "dxvk9" => Some(PipelineId::DxvkD9),
            "dxvk_d11" | "dxvk11" => Some(PipelineId::DxvkD11),
            "vkd3d_d12" | "vkd3d12" => Some(PipelineId::Vkd3dD12),
            "wined3d_32" => Some(PipelineId::M32),
            "metalsharp_wine" => Some(PipelineId::WineBare),
            "steam" => Some(PipelineId::Steam),
            "macos_steam" | "mac_steam" | "native_steam" => Some(PipelineId::MacSteam),
            "xna_fna_arm64" | "xna_fna_x86" | "xna_fna" | "fna_mono_xna" | "mono_fna_xna" => Some(PipelineId::FnaArm64),
            _ => None,
        }
    }

    pub fn from_str_flexible(s: &str) -> Option<PipelineId> {
        let normalized = s.trim().to_ascii_lowercase().replace('-', "_");
        if let Some(p) = Self::from_legacy_method(&normalized) {
            return Some(p);
        }
        match normalized.as_str() {
            "dxmt" | "auto_dxmt" | "metalsharp_dxmt" => Some(PipelineId::Dxmt),
            "m11" | "d3d11" | "dx11" | "steam_d3dmetal_perf" | "steam_metalfx" => Some(PipelineId::M11),
            "m12" | "d3d12" | "dx12" => Some(PipelineId::M12),
            "dxvk_d9" | "dxvk9" | "d9vk" => Some(PipelineId::DxvkD9),
            "dxvk_d11" | "dxvk11" | "dxvk" => Some(PipelineId::DxvkD11),
            "vkd3d_d12" | "vkd3d12" | "vkd3d" | "vkd3d_proton" => Some(PipelineId::Vkd3dD12),
            // Legacy `d3dmetal`/`d3dmetal_native`/`gptk`/`m13` aliases all
            // resolve to the single reserved native variant. There is no GPTK
            // lane anymore; native is not user-selectable/launchable until the
            // host ABI and payload are ready.
            "d3dmetal" | "d3dmetal_native" | "m13" | "gptk" | "steam_d3dmetal" => Some(PipelineId::D3DMetalNative),
            "m10" | "d3d10" | "dx10" => Some(PipelineId::M10),
            "m9" | "d3d9" | "dx9" => Some(PipelineId::M9),
            "m32" | "m32_w" => Some(PipelineId::M32),
            "fna_arm64" | "fna_x86" | "mono_generic" | "fna_mono_xna" | "mono_fna_xna" => Some(PipelineId::FnaArm64),
            "steam" | "wine_steam" => Some(PipelineId::Steam),
            "macos_steam" | "mac_steam" | "native_steam" => Some(PipelineId::MacSteam),
            "wine_bare" | "m64" => Some(PipelineId::WineBare),
            _ => None,
        }
    }

    pub fn to_legacy_method(self) -> &'static str {
        match self {
            PipelineId::Dxmt | PipelineId::M9 | PipelineId::M10 | PipelineId::M11 | PipelineId::M12 => "dxmt",
            PipelineId::DxvkD9 => "dxvk_d9",
            PipelineId::DxvkD11 => "dxvk_d11",
            PipelineId::Vkd3dD12 => "vkd3d_d12",
            PipelineId::D3DMetalNative => "d3dmetal_native",
            PipelineId::M32 => "wined3d_32",
            PipelineId::FnaArm64 => "xna_fna_arm64",
            PipelineId::Steam => "steam",
            PipelineId::MacSteam => "macos_steam",
            PipelineId::WineBare => "metalsharp_wine",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dxmt_is_primary_user_facing_pipeline() {
        let pipeline_list = pipelines();
        assert_eq!(pipeline_list.first().map(|p| p.id), Some(PipelineId::Dxmt));

        let dxmt = get_pipeline(PipelineId::Dxmt);
        assert_eq!(dxmt.name, "DXMT");
        assert_eq!(dxmt.backend, "dxmt");
        assert_eq!(dxmt.graphics_backend, "dxmt");
        assert!(dxmt.alternatives.contains(&PipelineId::M12));
        assert!(dxmt.alternatives.contains(&PipelineId::M11));
        assert!(dxmt.alternatives.contains(&PipelineId::M10));
        assert!(dxmt.alternatives.contains(&PipelineId::M9));
    }

    #[test]
    fn d3dmetal_native_is_reserved_and_gptk_lane_is_gone() {
        // The external GPTK lane (M13 / Apple-GPTK-Wine / Homebrew GPTK) is
        // removed entirely. `d3dmetal`/`gptk`/`m13` aliases all resolve to the
        // single reserved native variant, which is not selectable/launchable.
        assert!(!PipelineId::D3DMetalNative.is_user_selectable());
        assert_eq!(PipelineId::D3DMetalNative.user_selectable_id(), None);
        assert_eq!(PipelineId::D3DMetalNative.user_selectable_name(), None);
        assert_eq!(PipelineId::from_str_flexible("d3dmetal_native"), Some(PipelineId::D3DMetalNative));
        assert_eq!(PipelineId::from_str_flexible("d3dmetal"), Some(PipelineId::D3DMetalNative));
        assert_eq!(PipelineId::from_str_flexible("gptk"), Some(PipelineId::D3DMetalNative));
        assert_eq!(PipelineId::from_str_flexible("m13"), Some(PipelineId::D3DMetalNative));
        assert_eq!(PipelineId::from_str_flexible("steam_d3dmetal"), Some(PipelineId::D3DMetalNative));

        let node = get_pipeline(PipelineId::D3DMetalNative);
        assert_eq!(node.backend, D3DMETAL_NATIVE_BACKEND);
        assert_eq!(node.graphics_backend, D3DMETAL_NATIVE_BACKEND);
        assert!(node.experimental);
        assert_eq!(node.id.to_legacy_method(), "d3dmetal_native");
        // No selectable pipeline's alternatives may fall back to the reserved lane.
        for pipeline in pipelines() {
            assert!(!pipeline.id.is_user_selectable() || !pipeline.alternatives.contains(&PipelineId::D3DMetalNative));
        }
    }

    #[test]
    fn d3dmetal_native_abi_names_are_frozen() {
        assert_eq!(D3DMETAL_NATIVE_BACKEND, "d3dmetal_native");
        assert_eq!(MS_GRAPHICS_BACKEND_ENV, "MS_GRAPHICS_BACKEND");
        assert_eq!(MS_ACTIVE_GRAPHICS_BACKEND_ENV, "MS_ACTIVE_GRAPHICS_BACKEND");
        assert_eq!(MS_D3DMETAL_SHARED_PATH_ENV, "MS_D3DMETAL_SHARED_PATH");
        assert_eq!(MS_D3DMETAL_FRAMEWORK_PATH_ENV, "MS_D3DMETAL_FRAMEWORK_PATH");
    }

    #[test]
    fn m12_is_primary_d3d12_dxmt_profile() {
        let m12 = get_pipeline(PipelineId::M12);
        assert!(!m12.experimental);
        assert_eq!(m12.backend, "dxmt");
        assert!(m12.launch_args.contains(&"-windowed"));
        assert!(m12.deploy_dlls.iter().any(|dll| dll.filename == "d3d12.dll"));
        assert_eq!(m12.shader_cache_subdir, Some("m12"));
    }

    #[test]
    fn m12_is_stronger_than_other_dxmt_d3d_paths() {
        let m12 = get_pipeline(PipelineId::M12);

        for required in ["lib/wine/x86_64-unix", "lib/dxmt_m12/x86_64-unix"] {
            assert!(m12.dyld_paths.contains(&required));
        }
        assert!(!m12.dyld_paths.contains(&"lib/dxmt/x86_64-unix"));
        assert!(m12.winedllpath_dirs.contains(&"lib/dxmt_m12/x86_64-windows"));
        assert!(!m12.winedllpath_dirs.contains(&"lib/dxmt/x86_64-windows"));
        assert!(!m12.winedllpath_dirs.contains(&"lib/metalsharp/x86_64-windows"));

        let m12_dlls: std::collections::HashSet<_> =
            m12.deploy_dlls.iter().map(|dll| (dll.source_subpath, dll.filename)).collect();
        for required in ["d3d12.dll", "d3d11.dll", "dxgi.dll", "dxgi_dxmt.dll", "d3d10core.dll", "winemetal.dll"] {
            assert!(
                m12_dlls.contains(&("lib/dxmt_m12/x86_64-windows", required)),
                "M12 missing isolated DXMT DLL {}",
                required
            );
        }
        assert!(!m12.deploy_dlls.iter().any(|dll| dll.source_subpath == "lib/dxmt/x86_64-windows"));
        assert!(!m12.deploy_dlls.iter().any(|dll| dll.filename == "metalsharp_ntdll_hook.dll"));

        let m12_env: std::collections::HashSet<_> = m12.env_vars.iter().map(|env| env.key).collect();
        assert!(m12_env.contains("DXMT_ASYNC_PIPELINE_COMPILE"));
        assert!(m12_env.contains("DXMT_D3D12_PSO_WORKERS"));
        assert!(m12_env.contains("DXMT_METALFX_SPATIAL_SWAPCHAIN"));
        assert!(m12_env.contains("DXMT_METALFX_SPATIAL"));
        assert!(m12_env.contains("DXMT_METALFX_TEMPORAL"));
        let m12_env_values: std::collections::HashMap<_, _> =
            m12.env_vars.iter().map(|env| (env.key, env.value)).collect();
        assert_eq!(m12_env_values.get("DXMT_ASYNC_PIPELINE_COMPILE"), Some(&"1"));
        assert_eq!(m12_env_values.get("DXMT_D3D12_PSO_WORKERS"), Some(&"6"));
        assert_eq!(m12_env_values.get("DXMT_METALFX_SPATIAL_SWAPCHAIN"), Some(&"1"));
        assert_eq!(m12_env_values.get("DXMT_METALFX_SPATIAL"), Some(&"1"));
        assert_eq!(m12_env_values.get("DXMT_METALFX_TEMPORAL"), Some(&"1"));
        assert_eq!(m12_env_values.get("DXMT_CONFIG"), Some(&DXMT_M12_SAFE_CONFIG));

        assert_eq!(
            m12.wine_overrides,
            Some("winemetal,d3d12,dxgi,dxgi_dxmt,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d")
        );
        assert!(m12.alternatives.contains(&PipelineId::M11));
    }

    #[test]
    fn m11_m10_m9_stay_on_legacy_dxmt_surface() {
        let m11 = get_pipeline(PipelineId::M11);
        assert_eq!(
            m11.wine_overrides,
            Some("winemetal,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d")
        );

        let m10 = get_pipeline(PipelineId::M10);
        assert_eq!(
            m10.wine_overrides,
            Some("winemetal,d3d10,d3d10_1,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d")
        );

        let m9 = get_pipeline(PipelineId::M9);
        assert_eq!(m9.wine_overrides, Some("d3d9=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"));

        for pipeline in [m11, m10, m9] {
            assert!(
                pipeline.dyld_paths.iter().all(|path| !path.contains("dxmt_m12")),
                "{} should not load the isolated M12 Unix surface",
                pipeline.name
            );
            assert!(
                pipeline.winedllpath_dirs.iter().all(|path| !path.contains("dxmt_m12")),
                "{} should not route PE DLLs through the isolated M12 surface",
                pipeline.name
            );
            assert!(
                pipeline.deploy_dlls.iter().all(|dll| !dll.source_subpath.contains("dxmt_m12")),
                "{} should not deploy isolated M12 DLLs",
                pipeline.name
            );
        }
    }

    #[test]
    fn m10_is_stable_dxmt_d3d10_pipeline() {
        let m10 = get_pipeline(PipelineId::M10);

        assert_eq!(m10.name, "M10");
        assert_eq!(m10.description, "D3D10 -> Metal via DXMT");
        assert_eq!(m10.backend, "dxmt");
        assert!(!m10.experimental);
        assert!(m10.launch_args.is_empty());
        assert_eq!(m10.shader_cache_subdir, Some("m10"));
    }

    #[test]
    fn m10_matches_shared_dxmt_handoff_shape() {
        let m10 = get_pipeline(PipelineId::M10);
        let m11 = get_pipeline(PipelineId::M11);

        assert_eq!(m10.dyld_paths, m11.dyld_paths);
        assert_eq!(
            m10.wine_overrides,
            Some("winemetal,d3d10,d3d10_1,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d")
        );

        let m10_dlls: std::collections::HashSet<_> = m10.deploy_dlls.iter().map(|dll| dll.filename).collect();
        for required in
            ["d3d10.dll", "d3d10_1.dll", "d3d11.dll", "dxgi.dll", "dxgi_dxmt.dll", "d3d10core.dll", "winemetal.dll"]
        {
            assert!(m10_dlls.contains(required), "M10 missing {}", required);
        }
        assert!(!m10_dlls.contains("d3d12.dll"));

        let m10_env: std::collections::HashSet<_> = m10.env_vars.iter().map(|env| env.key).collect();
        assert!(m10_env.contains("DXMT_ASYNC_PIPELINE_COMPILE"));
        assert!(m10_env.contains("DXMT_METALFX_SPATIAL_SWAPCHAIN"));

        assert!(m10.alternatives.contains(&PipelineId::M11));
        assert!(m10.alternatives.contains(&PipelineId::M9));
        assert!(m10.alternatives.contains(&PipelineId::WineBare));
    }

    #[test]
    fn m9_is_dxmt_family_without_dxvk_or_wined3d_fallbacks() {
        let m9 = get_pipeline(PipelineId::M9);

        assert_eq!(m9.name, "M9");
        assert_eq!(m9.description, "D3D9 -> Metal via DXMT launch family");
        assert_eq!(m9.backend, "dxmt");
        assert!(!m9.experimental);
        assert!(m9.launch_args.is_empty());
        assert_eq!(m9.shader_cache_subdir, Some("m9"));
        assert_eq!(m9.wine_overrides, Some("d3d9=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"));

        let m9_dlls: std::collections::HashSet<_> =
            m9.deploy_dlls.iter().map(|dll| (dll.source_subpath, dll.filename)).collect();
        assert!(m9_dlls.contains(&("lib/wine/x86_64-windows", "d3d9.dll")));
        assert!(m9_dlls.contains(&("lib/wine/i386-windows", "d3d9.dll")));
        assert!(m9_dlls.contains(&("lib/wine/i386-windows", "dxgi.dll")));
        assert!(m9_dlls.contains(&("lib/dxmt/x86_64-windows", "nvapi64.dll")));
        assert!(m9.deploy_dlls.iter().all(|dll| !dll.source_subpath.contains("dxvk")));
        assert!(m9.dyld_paths.contains(&"lib/dxmt/x86_64-unix"));

        let m9_env: std::collections::HashSet<_> = m9.env_vars.iter().map(|env| env.key).collect();
        assert!(m9_env.contains("DXMT_ASYNC_PIPELINE_COMPILE"));
        assert!(m9_env.contains("DXMT_METALFX_SPATIAL_SWAPCHAIN"));

        assert!(m9.alternatives.contains(&PipelineId::M11));
        assert!(m9.alternatives.contains(&PipelineId::M10));
        assert!(!m9.alternatives.contains(&PipelineId::M32));
        assert!(!m9.alternatives.contains(&PipelineId::WineBare));
    }

    #[test]
    fn legacy_dxvk_and_wined3d_m9_aliases_are_not_m9() {
        assert_eq!(PipelineId::from_legacy_method("dxvk_metal32"), None);
        assert_eq!(PipelineId::from_str_flexible("m9_gl"), None);
        assert_eq!(PipelineId::from_str_flexible("m32_vk"), None);
    }

    #[test]
    fn user_selectable_pipelines_are_the_public_bottle_options() {
        let selectable: Vec<_> = pipelines()
            .iter()
            .filter(|pipeline| pipeline.id.is_user_selectable())
            .map(|pipeline| pipeline.id)
            .collect();
        assert_eq!(
            selectable,
            vec![
                PipelineId::M12,
                PipelineId::M11,
                PipelineId::M10,
                PipelineId::M9,
                PipelineId::DxvkD9,
                PipelineId::DxvkD11,
                PipelineId::Vkd3dD12,
                PipelineId::FnaArm64
            ]
        );

        let labels: Vec<_> = selectable.iter().map(|pipeline| pipeline.user_selectable_name().unwrap()).collect();
        assert_eq!(labels, vec!["M12", "M11", "M10", "M9", "DXVK D3D9", "DXVK D3D11", "VKD3D D3D12", "Mono/FNA"]);

        for hidden in [
            PipelineId::Dxmt,
            PipelineId::D3DMetalNative,
            PipelineId::M32,
            PipelineId::Steam,
            PipelineId::MacSteam,
            PipelineId::WineBare,
        ] {
            assert!(!hidden.is_user_selectable());
            assert_eq!(hidden.user_selectable_id(), None);
        }
    }

    #[test]
    fn launch_method_parsing_is_case_and_separator_tolerant() {
        assert_eq!(PipelineId::from_str_flexible("dxmt"), Some(PipelineId::Dxmt));
        assert_eq!(PipelineId::from_str_flexible(" M12 "), Some(PipelineId::M12));
        assert_eq!(PipelineId::from_str_flexible("d3d12"), Some(PipelineId::M12));
        assert_eq!(PipelineId::from_str_flexible("dx10"), Some(PipelineId::M10));
        assert_eq!(PipelineId::from_str_flexible("dxvk-d9"), Some(PipelineId::DxvkD9));
        assert_eq!(PipelineId::from_str_flexible("dxvk"), Some(PipelineId::DxvkD11));
        assert_eq!(PipelineId::from_str_flexible("vkd3d-proton"), Some(PipelineId::Vkd3dD12));
        assert_eq!(PipelineId::from_str_flexible("wine-steam"), Some(PipelineId::Steam));
        assert_eq!(PipelineId::from_legacy_method("DXMT_METAL"), Some(PipelineId::M11));
    }

    #[test]
    fn vulkan_family_lanes_are_selectable_experimental_fallbacks() {
        let dxvk9 = get_pipeline(PipelineId::DxvkD9);
        assert_eq!(dxvk9.backend, "dxvk");
        assert!(dxvk9.experimental);
        assert!(dxvk9.requires_wine);
        assert_eq!(dxvk9.shader_cache_subdir, Some("dxvk-d9"));
        assert_eq!(dxvk9.wine_overrides, Some("d3d9,dxgi=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"));
        assert!(dxvk9.deploy_dlls.iter().any(|dll| dll.filename == "d3d9.dll"));
        assert!(dxvk9.deploy_dlls.iter().any(|dll| dll.filename == "dxgi.dll"));
        assert!(dxvk9.deploy_dlls.iter().all(|dll| dll.source_subpath.contains("dxvk")));

        let dxvk11 = get_pipeline(PipelineId::DxvkD11);
        assert_eq!(dxvk11.backend, "dxvk");
        assert!(dxvk11.wine_overrides.unwrap().contains("d3d11"));
        assert!(dxvk11.deploy_dlls.iter().any(|dll| dll.filename == "d3d11.dll"));
        assert!(dxvk11.deploy_dlls.iter().any(|dll| dll.filename == "dxgi.dll"));

        let vkd3d = get_pipeline(PipelineId::Vkd3dD12);
        assert_eq!(vkd3d.backend, "vkd3d");
        assert!(vkd3d.wine_overrides.unwrap().contains("d3d12"));
        assert_eq!(vkd3d.shader_cache_subdir, Some("vkd3d-d12"));
        assert!(vkd3d.deploy_dlls.iter().any(|dll| dll.filename == "d3d12.dll"));
        assert!(vkd3d.deploy_dlls.iter().all(|dll| dll.source_subpath.contains("vkd3d")));

        for pipeline in [PipelineId::DxvkD9, PipelineId::DxvkD11, PipelineId::Vkd3dD12] {
            assert!(pipeline.is_user_selectable());
            assert!(pipeline.is_vulkan_family());
            assert!(get_pipeline(pipeline).experimental);
        }
    }

    #[test]
    fn dxmt_family_serializes_to_canonical_launch_method() {
        for pipeline in [PipelineId::Dxmt, PipelineId::M9, PipelineId::M10, PipelineId::M11, PipelineId::M12] {
            assert_eq!(pipeline.to_legacy_method(), "dxmt");
            assert!(pipeline.is_dxmt_family());
        }
        assert_eq!(PipelineId::DxvkD9.to_legacy_method(), "dxvk_d9");
        assert_eq!(PipelineId::DxvkD11.to_legacy_method(), "dxvk_d11");
        assert_eq!(PipelineId::Vkd3dD12.to_legacy_method(), "vkd3d_d12");
    }
}
