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
                id: PipelineId::M12,
                name: "M12",
                description: "D3D12 -> Metal via DXMT",
                backend: "dxmt",
                experimental: false,
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
                launch_args: vec![],
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
                launch_args: vec![],
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
                id: PipelineId::M10,
                name: "M10",
                description: "D3D10 -> Metal via DXMT",
                backend: "dxmt",
                experimental: false,
                requires_wine: true,
                wine_overrides: Some(
                    "d3d10,d3d10_1,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
                ),
                dyld_paths: vec!["lib/wine/x86_64-unix", "lib/dxmt/x86_64-unix"],
                deploy_dlls: vec![
                    DllDeploy { source_subpath: "lib/wine/x86_64-windows", filename: "d3d10.dll" },
                    DllDeploy { source_subpath: "lib/wine/x86_64-windows", filename: "d3d10_1.dll" },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "d3d11.dll" },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "dxgi.dll" },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "d3d10core.dll" },
                    DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "winemetal.dll" },
                ],
                env_vars: vec![
                    EnvVar { key: "DXMT_METALFX_SPATIAL_SWAPCHAIN", value: "1" },
                    EnvVar { key: "DXMT_ASYNC_PIPELINE_COMPILE", value: "1" },
                ],
                launch_args: vec![],
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
                description: "D3D9 -> Metal via DXMT launch family",
                backend: "dxmt",
                experimental: false,
                requires_wine: true,
                wine_overrides: Some("d3d9=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"),
                dyld_paths: vec!["lib/wine/x86_64-unix", "lib/dxmt/x86_64-unix"],
                deploy_dlls: vec![
                    DllDeploy { source_subpath: "lib/wine/x86_64-windows", filename: "d3d9.dll" },
                    DllDeploy { source_subpath: "lib/wine/i386-windows", filename: "d3d9.dll" },
                ],
                env_vars: vec![
                    EnvVar { key: "DXMT_METALFX_SPATIAL_SWAPCHAIN", value: "1" },
                    EnvVar { key: "DXMT_ASYNC_PIPELINE_COMPILE", value: "1" },
                ],
                launch_args: vec![],
                alternatives: vec![PipelineId::M11, PipelineId::M10, PipelineId::Steam, PipelineId::MacSteam],
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
        match method.trim().to_ascii_lowercase().as_str() {
            "dxmt_metal" | "steam_d3dmetal_perf" | "steam_metalfx" => Some(PipelineId::M11),
            "dxmt_metal12" => Some(PipelineId::M12),
            "d3d9_metal" => Some(PipelineId::M9),
            "wined3d_32" => Some(PipelineId::M32),
            "metalsharp_wine" => Some(PipelineId::WineBare),
            "steam" => Some(PipelineId::Steam),
            "macos_steam" | "mac_steam" | "native_steam" => Some(PipelineId::MacSteam),
            "xna_fna_arm64" | "xna_fna_x86" | "xna_fna" => Some(PipelineId::FnaArm64),
            _ => None,
        }
    }

    pub fn from_str_flexible(s: &str) -> Option<PipelineId> {
        let normalized = s.trim().to_ascii_lowercase().replace('-', "_");
        if let Some(p) = Self::from_legacy_method(&normalized) {
            return Some(p);
        }
        match normalized.as_str() {
            "m11" | "d3d11" | "dx11" | "steam_d3dmetal_perf" | "steam_metalfx" => Some(PipelineId::M11),
            "m12" | "d3d12" | "dx12" => Some(PipelineId::M12),
            "m10" | "d3d10" | "dx10" => Some(PipelineId::M10),
            "m9" | "d3d9" | "dx9" => Some(PipelineId::M9),
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn m12_is_primary_stable_pipeline() {
        let pipeline_list = pipelines();
        assert_eq!(pipeline_list.first().map(|p| p.id), Some(PipelineId::M12));

        let m12 = get_pipeline(PipelineId::M12);
        assert!(!m12.experimental);
        assert_eq!(m12.backend, "dxmt");
        assert!(m12.launch_args.is_empty());
        assert!(m12.deploy_dlls.iter().any(|dll| dll.filename == "d3d12.dll"));
        assert_eq!(m12.shader_cache_subdir, Some("m12"));
    }

    #[test]
    fn m12_is_stronger_than_other_dxmt_d3d_paths() {
        let m12 = get_pipeline(PipelineId::M12);

        for required in ["lib/wine/x86_64-unix", "lib/dxmt/x86_64-unix"] {
            assert!(m12.dyld_paths.contains(&required));
        }

        let m12_dlls: std::collections::HashSet<_> = m12.deploy_dlls.iter().map(|dll| dll.filename).collect();
        for required in ["d3d12.dll", "d3d11.dll", "dxgi.dll", "d3d10core.dll", "winemetal.dll"] {
            assert!(m12_dlls.contains(required), "M12 missing shared DXMT DLL {}", required);
        }

        let m12_env: std::collections::HashSet<_> = m12.env_vars.iter().map(|env| env.key).collect();
        assert!(m12_env.contains("DXMT_ASYNC_PIPELINE_COMPILE"));
        assert!(m12_env.contains("DXMT_METALFX_SPATIAL_SWAPCHAIN"));
        assert!(m12_env.contains("DXMT_METALFX_SPATIAL"));
        assert!(m12_env.contains("DXMT_METALFX_TEMPORAL"));

        assert_eq!(
            m12.wine_overrides,
            Some("d3d12,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d")
        );
        assert!(m12.alternatives.contains(&PipelineId::M11));
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
            Some("d3d10,d3d10_1,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d")
        );

        let m10_dlls: std::collections::HashSet<_> = m10.deploy_dlls.iter().map(|dll| dll.filename).collect();
        for required in ["d3d10.dll", "d3d10_1.dll", "d3d11.dll", "dxgi.dll", "d3d10core.dll", "winemetal.dll"] {
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
        assert_eq!(
            m9_dlls,
            std::collections::HashSet::from([
                ("lib/wine/x86_64-windows", "d3d9.dll"),
                ("lib/wine/i386-windows", "d3d9.dll"),
            ])
        );
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
    fn launch_method_parsing_is_case_and_separator_tolerant() {
        assert_eq!(PipelineId::from_str_flexible(" M12 "), Some(PipelineId::M12));
        assert_eq!(PipelineId::from_str_flexible("d3d12"), Some(PipelineId::M12));
        assert_eq!(PipelineId::from_str_flexible("dx10"), Some(PipelineId::M10));
        assert_eq!(PipelineId::from_str_flexible("wine-steam"), Some(PipelineId::Steam));
        assert_eq!(PipelineId::from_legacy_method("DXMT_METAL"), Some(PipelineId::M11));
    }
}
