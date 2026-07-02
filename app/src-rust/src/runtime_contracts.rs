use serde::Serialize;
use serde_json::{json, Value};

#[derive(Debug, Clone, Copy, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RuntimeSurfaceId {
    Wine,
    Dxmt,
    DxmtM12,
    Dxvk,
    Vkd3d,
    D3DMetal,
    MonoArm64,
    MonoX86,
    MetalsharpHooks,
    SteamBridge,
    Gogdl,
}

impl RuntimeSurfaceId {
    pub fn canonical_id(self) -> &'static str {
        match self {
            RuntimeSurfaceId::Wine => "wine",
            RuntimeSurfaceId::Dxmt => "dxmt",
            RuntimeSurfaceId::DxmtM12 => "dxmt_m12",
            RuntimeSurfaceId::Dxvk => "dxvk",
            RuntimeSurfaceId::Vkd3d => "vkd3d",
            RuntimeSurfaceId::D3DMetal => "d3dmetal",
            RuntimeSurfaceId::MonoArm64 => "mono_arm64",
            RuntimeSurfaceId::MonoX86 => "mono_x86",
            RuntimeSurfaceId::MetalsharpHooks => "metalsharp_hooks",
            RuntimeSurfaceId::SteamBridge => "steam_bridge",
            RuntimeSurfaceId::Gogdl => "gogdl",
        }
    }

    pub fn installed_path(self) -> Option<&'static str> {
        match self {
            RuntimeSurfaceId::Wine => Some("runtime/wine"),
            RuntimeSurfaceId::Dxmt => Some("runtime/wine/lib/dxmt"),
            RuntimeSurfaceId::DxmtM12 => Some("runtime/wine/lib/dxmt_m12"),
            RuntimeSurfaceId::Dxvk => Some("runtime/wine/lib/dxvk"),
            RuntimeSurfaceId::Vkd3d => Some("runtime/wine/lib/vkd3d"),
            RuntimeSurfaceId::D3DMetal => Some("prefix-gptk + Homebrew Game Porting Toolkit"),
            RuntimeSurfaceId::MonoArm64 => Some("runtime/mono-arm64"),
            RuntimeSurfaceId::MonoX86 => Some("runtime/mono-x86"),
            RuntimeSurfaceId::MetalsharpHooks => Some("runtime/wine/lib/metalsharp"),
            RuntimeSurfaceId::SteamBridge => Some("runtime/steam-bridge"),
            RuntimeSurfaceId::Gogdl => Some("tools/gogdl"),
        }
    }
}

#[derive(Debug, Clone, Copy, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RuntimeLaneStatus {
    Available,
    External,
    Planned,
}

#[derive(Debug, Clone, Serialize)]
pub struct RuntimeLaneContract {
    pub id: &'static str,
    pub name: &'static str,
    pub family: &'static str,
    pub status: RuntimeLaneStatus,
    pub source_scopes: Vec<&'static str>,
    pub requires_wine: bool,
    pub supports_win32: bool,
    pub supports_win64: bool,
    pub host_arch: &'static str,
    pub windows_arch: &'static str,
    pub graphics_apis: Vec<&'static str>,
    pub prefix_policy: &'static str,
    pub runtime_surfaces: Vec<RuntimeSurfaceId>,
    pub runtime_surface_paths: Vec<&'static str>,
    pub required_pe_dlls: Vec<&'static str>,
    pub required_unix_sidecars: Vec<&'static str>,
    pub dyld_paths: Vec<&'static str>,
    pub winedllpath_dirs: Vec<&'static str>,
    pub shader_cache_lane: Option<&'static str>,
    pub fallback_lanes: Vec<&'static str>,
    pub doctor_checks: Vec<&'static str>,
    pub repair_actions: Vec<&'static str>,
    pub notes: &'static str,
}

fn paths_for(surfaces: &[RuntimeSurfaceId]) -> Vec<&'static str> {
    surfaces.iter().filter_map(|surface| surface.installed_path()).collect()
}

pub fn runtime_contract_id_for_pipeline(pipeline: crate::mtsp::engine::PipelineId) -> Option<&'static str> {
    match pipeline {
        crate::mtsp::engine::PipelineId::M9 => Some("m9"),
        crate::mtsp::engine::PipelineId::M10 => Some("m10"),
        crate::mtsp::engine::PipelineId::M11 => Some("m11"),
        crate::mtsp::engine::PipelineId::M12 => Some("m12_dxmt_m12"),
        crate::mtsp::engine::PipelineId::DxvkD9 => Some("dxvk_d9"),
        crate::mtsp::engine::PipelineId::DxvkD11 => Some("dxvk_d11"),
        crate::mtsp::engine::PipelineId::Vkd3dD12 => Some("vkd3d_d12"),
        crate::mtsp::engine::PipelineId::D3DMetal => Some("d3dmetal_gptk"),
        crate::mtsp::engine::PipelineId::WineBare => Some("wine_bare"),
        crate::mtsp::engine::PipelineId::Steam => Some("steam_background"),
        crate::mtsp::engine::PipelineId::FnaArm64 => Some("native_mono_arm64"),
        _ => None,
    }
}

pub fn runtime_lane_pipeline_id(lane_id: &str) -> Option<crate::mtsp::engine::PipelineId> {
    match lane_id {
        "m9" => Some(crate::mtsp::engine::PipelineId::M9),
        "m10" => Some(crate::mtsp::engine::PipelineId::M10),
        "m11" => Some(crate::mtsp::engine::PipelineId::M11),
        "m12_dxmt_m12" => Some(crate::mtsp::engine::PipelineId::M12),
        "dxvk_d9" => Some(crate::mtsp::engine::PipelineId::DxvkD9),
        "dxvk_d11" => Some(crate::mtsp::engine::PipelineId::DxvkD11),
        "vkd3d_d12" => Some(crate::mtsp::engine::PipelineId::Vkd3dD12),
        "d3dmetal_gptk" => Some(crate::mtsp::engine::PipelineId::D3DMetal),
        "wine_bare" => Some(crate::mtsp::engine::PipelineId::WineBare),
        "steam_background" => Some(crate::mtsp::engine::PipelineId::Steam),
        _ => None,
    }
}

pub fn runtime_lane_contracts() -> Vec<RuntimeLaneContract> {
    vec![
        RuntimeLaneContract {
            id: "native_mono_arm64",
            name: "Native Mono/FNA ARM64",
            family: "native-mono-fna",
            status: RuntimeLaneStatus::Available,
            source_scopes: vec!["steam", "sharp_library"],
            requires_wine: false,
            supports_win32: false,
            supports_win64: false,
            host_arch: "arm64",
            windows_arch: "managed/native-macos",
            graphics_apis: vec!["fna", "xna", "monogame"],
            prefix_policy: "none",
            runtime_surfaces: vec![RuntimeSurfaceId::MonoArm64],
            runtime_surface_paths: paths_for(&[RuntimeSurfaceId::MonoArm64]),
            required_pe_dlls: vec![],
            required_unix_sidecars: vec!["libmonosgen-2.0.1.dylib", "FNA/FNA3D/FAudio support assets"],
            dyld_paths: vec!["runtime/mono-arm64/lib"],
            winedllpath_dirs: vec![],
            shader_cache_lane: Some("fna-arm64"),
            fallback_lanes: vec!["native_mono_x86", "wine_bare", "m11"],
            doctor_checks: vec!["mono_binary", "mono_arch", "fna_signals", "native_shims", "asset_receipts"],
            repair_actions: vec!["repair_fna_support_assets", "precompile_fna_shims"],
            notes: "Native Mono/FNA should be preferred over Wine when a game is proven on this lane.",
        },
        RuntimeLaneContract {
            id: "native_mono_x86",
            name: "Native Mono/FNA x86_64",
            family: "native-mono-fna",
            status: RuntimeLaneStatus::Available,
            source_scopes: vec!["steam", "sharp_library"],
            requires_wine: false,
            supports_win32: false,
            supports_win64: false,
            host_arch: "x86_64-rosetta",
            windows_arch: "managed/native-macos",
            graphics_apis: vec!["fna", "xna", "monogame"],
            prefix_policy: "none",
            runtime_surfaces: vec![RuntimeSurfaceId::MonoX86],
            runtime_surface_paths: paths_for(&[RuntimeSurfaceId::MonoX86]),
            required_pe_dlls: vec![],
            required_unix_sidecars: vec!["libmonosgen-2.0.1.dylib", "FNA/FNA3D/FAudio support assets"],
            dyld_paths: vec!["runtime/mono-x86/lib"],
            winedllpath_dirs: vec![],
            shader_cache_lane: Some("fna-x86"),
            fallback_lanes: vec!["native_mono_arm64", "wine_bare", "m11"],
            doctor_checks: vec!["mono_binary", "mono_arch", "fna_signals", "native_shims", "asset_receipts"],
            repair_actions: vec!["repair_fna_support_assets", "precompile_fna_shims"],
            notes: "Rosetta-backed native Mono lane for legacy FNA/XNA titles such as Celeste-style games.",
        },
        RuntimeLaneContract {
            id: "m9",
            name: "M9",
            family: "wine-dxmt",
            status: RuntimeLaneStatus::Available,
            source_scopes: vec!["steam", "sharp_library"],
            requires_wine: true,
            supports_win32: true,
            supports_win64: true,
            host_arch: "x86_64-rosetta",
            windows_arch: "wow64",
            graphics_apis: vec!["d3d9"],
            prefix_policy: "source-owned-prefix",
            runtime_surfaces: vec![RuntimeSurfaceId::Wine, RuntimeSurfaceId::Dxmt, RuntimeSurfaceId::MetalsharpHooks],
            runtime_surface_paths: paths_for(&[
                RuntimeSurfaceId::Wine,
                RuntimeSurfaceId::Dxmt,
                RuntimeSurfaceId::MetalsharpHooks,
            ]),
            required_pe_dlls: vec!["d3d9.dll"],
            required_unix_sidecars: vec![],
            dyld_paths: vec!["runtime/wine/lib/wine/x86_64-unix", "runtime/wine/lib/dxmt/x86_64-unix"],
            winedllpath_dirs: vec![
                "runtime/wine/lib/wine/x86_64-windows",
                "runtime/wine/lib/wine/i386-windows",
                "runtime/wine/lib/dxmt/x86_64-windows",
                "runtime/wine/lib/metalsharp/x86_64-windows",
            ],
            shader_cache_lane: Some("m9"),
            fallback_lanes: vec!["dxvk_d9", "m11", "wine_bare"],
            doctor_checks: vec!["wine_runtime", "d3d9_dll", "prefix_wineboot", "shader_cache"],
            repair_actions: vec!["repair_wine_runtime", "stage_route_dlls", "run_wineboot_update"],
            notes: "Protected D3D9 compatibility lane. DXVK-D9 should be an advanced fallback, not an automatic replacement.",
        },
        RuntimeLaneContract {
            id: "m10",
            name: "M10",
            family: "wine-dxmt",
            status: RuntimeLaneStatus::Available,
            source_scopes: vec!["steam", "sharp_library"],
            requires_wine: true,
            supports_win32: false,
            supports_win64: true,
            host_arch: "x86_64-rosetta",
            windows_arch: "win64",
            graphics_apis: vec!["d3d10", "d3d10_1", "d3d10core"],
            prefix_policy: "source-owned-prefix",
            runtime_surfaces: vec![RuntimeSurfaceId::Wine, RuntimeSurfaceId::Dxmt, RuntimeSurfaceId::MetalsharpHooks],
            runtime_surface_paths: paths_for(&[
                RuntimeSurfaceId::Wine,
                RuntimeSurfaceId::Dxmt,
                RuntimeSurfaceId::MetalsharpHooks,
            ]),
            required_pe_dlls: vec!["d3d10.dll", "d3d10_1.dll", "d3d10core.dll", "d3d11.dll", "dxgi.dll"],
            required_unix_sidecars: vec!["winemetal.so"],
            dyld_paths: vec!["runtime/wine/lib/wine/x86_64-unix", "runtime/wine/lib/dxmt/x86_64-unix"],
            winedllpath_dirs: vec![
                "runtime/wine/lib/wine/x86_64-windows",
                "runtime/wine/lib/dxmt/x86_64-windows",
                "runtime/wine/lib/metalsharp/x86_64-windows",
            ],
            shader_cache_lane: Some("m10"),
            fallback_lanes: vec!["dxvk_d11", "m11", "wine_bare"],
            doctor_checks: vec!["wine_runtime", "dxmt_runtime", "prefix_wineboot", "shader_cache"],
            repair_actions: vec!["repair_dxmt_runtime", "stage_route_dlls", "run_wineboot_update"],
            notes: "Protected D3D10 lane. It shares the legacy dxmt surface with M11 but has D3D10-specific entrypoints.",
        },
        RuntimeLaneContract {
            id: "m11",
            name: "M11",
            family: "wine-dxmt",
            status: RuntimeLaneStatus::Available,
            source_scopes: vec!["steam", "sharp_library"],
            requires_wine: true,
            supports_win32: false,
            supports_win64: true,
            host_arch: "x86_64-rosetta",
            windows_arch: "win64",
            graphics_apis: vec!["d3d11", "dxgi"],
            prefix_policy: "source-owned-prefix",
            runtime_surfaces: vec![RuntimeSurfaceId::Wine, RuntimeSurfaceId::Dxmt, RuntimeSurfaceId::MetalsharpHooks],
            runtime_surface_paths: paths_for(&[
                RuntimeSurfaceId::Wine,
                RuntimeSurfaceId::Dxmt,
                RuntimeSurfaceId::MetalsharpHooks,
            ]),
            required_pe_dlls: vec!["d3d11.dll", "dxgi.dll", "dxgi_dxmt.dll", "d3d10core.dll", "winemetal.dll"],
            required_unix_sidecars: vec!["winemetal.so"],
            dyld_paths: vec!["runtime/wine/lib/wine/x86_64-unix", "runtime/wine/lib/dxmt/x86_64-unix"],
            winedllpath_dirs: vec!["runtime/wine/lib/dxmt/x86_64-windows", "runtime/wine/lib/metalsharp/x86_64-windows"],
            shader_cache_lane: Some("m11"),
            fallback_lanes: vec!["dxvk_d11", "d3dmetal_gptk", "wine_bare"],
            doctor_checks: vec!["wine_runtime", "dxmt_runtime", "prefix_wineboot", "shader_cache"],
            repair_actions: vec!["repair_dxmt_runtime", "stage_route_dlls", "run_wineboot_update"],
            notes: "Protected D3D11 lane and primary fallback for D3D12 titles that cannot use M12.",
        },
        RuntimeLaneContract {
            id: "m12_dxmt_m12",
            name: "M12",
            family: "wine-dxmt",
            status: RuntimeLaneStatus::Available,
            source_scopes: vec!["steam", "sharp_library"],
            requires_wine: true,
            supports_win32: false,
            supports_win64: true,
            host_arch: "x86_64-rosetta",
            windows_arch: "win64",
            graphics_apis: vec!["d3d12", "d3d11", "dxgi"],
            prefix_policy: "source-owned-prefix",
            runtime_surfaces: vec![RuntimeSurfaceId::Wine, RuntimeSurfaceId::DxmtM12],
            runtime_surface_paths: paths_for(&[RuntimeSurfaceId::Wine, RuntimeSurfaceId::DxmtM12]),
            required_pe_dlls: vec![
                "d3d12.dll",
                "d3d11.dll",
                "dxgi.dll",
                "dxgi_dxmt.dll",
                "d3d10core.dll",
                "winemetal.dll",
            ],
            required_unix_sidecars: vec!["winemetal.so", "libc++.1.dylib", "libc++abi.1.dylib", "libunwind.1.dylib"],
            dyld_paths: vec!["runtime/wine/lib/dxmt_m12/x86_64-unix", "runtime/wine/lib/wine/x86_64-unix"],
            winedllpath_dirs: vec!["runtime/wine/lib/dxmt_m12/x86_64-windows"],
            shader_cache_lane: Some("m12"),
            fallback_lanes: vec!["d3dmetal_gptk", "vkd3d_d12", "m11"],
            doctor_checks: vec![
                "wine_runtime",
                "dxmt_m12_manifest",
                "dxmt_m12_hashes",
                "dxmt_m12_sidecars",
                "prefix_wineboot",
                "shader_cache",
            ],
            repair_actions: vec!["repair_dxmt_m12_runtime", "stage_route_dlls", "run_wineboot_update"],
            notes: "Primary modern D3D12 route. The installed runtime surface is canonically dxmt_m12.",
        },
        RuntimeLaneContract {
            id: "dxvk_d9",
            name: "DXVK D3D9",
            family: "wine-vulkan",
            status: RuntimeLaneStatus::Available,
            source_scopes: vec!["steam", "sharp_library"],
            requires_wine: true,
            supports_win32: true,
            supports_win64: true,
            host_arch: "x86_64-rosetta",
            windows_arch: "wow64",
            graphics_apis: vec!["d3d9", "vulkan", "moltenvk"],
            prefix_policy: "source-owned-prefix",
            runtime_surfaces: vec![RuntimeSurfaceId::Wine, RuntimeSurfaceId::Dxvk],
            runtime_surface_paths: paths_for(&[RuntimeSurfaceId::Wine, RuntimeSurfaceId::Dxvk]),
            required_pe_dlls: vec!["d3d9.dll", "dxgi.dll"],
            required_unix_sidecars: vec!["libMoltenVK.dylib", "Vulkan ICD"],
            dyld_paths: vec!["runtime/wine/lib/wine/x86_64-unix"],
            winedllpath_dirs: vec!["runtime/wine/lib/dxvk/x86_64-windows", "runtime/wine/lib/dxvk/i386-windows"],
            shader_cache_lane: Some("dxvk-d9"),
            fallback_lanes: vec!["m9", "wine_bare"],
            doctor_checks: vec!["dxvk_dlls", "moltenvk", "vulkan_icd", "state_cache"],
            repair_actions: vec!["install_dxvk_runtime", "fix_moltenvk_icd"],
            notes: "Available experimental fallback for D3D9 games where M9 regresses.",
        },
        RuntimeLaneContract {
            id: "dxvk_d11",
            name: "DXVK D3D10/D3D11",
            family: "wine-vulkan",
            status: RuntimeLaneStatus::Available,
            source_scopes: vec!["steam", "sharp_library"],
            requires_wine: true,
            supports_win32: true,
            supports_win64: true,
            host_arch: "x86_64-rosetta",
            windows_arch: "wow64",
            graphics_apis: vec!["d3d10", "d3d11", "dxgi", "vulkan", "moltenvk"],
            prefix_policy: "source-owned-prefix",
            runtime_surfaces: vec![RuntimeSurfaceId::Wine, RuntimeSurfaceId::Dxvk],
            runtime_surface_paths: paths_for(&[RuntimeSurfaceId::Wine, RuntimeSurfaceId::Dxvk]),
            required_pe_dlls: vec!["d3d10core.dll", "d3d11.dll", "dxgi.dll"],
            required_unix_sidecars: vec!["libMoltenVK.dylib", "Vulkan ICD"],
            dyld_paths: vec!["runtime/wine/lib/wine/x86_64-unix"],
            winedllpath_dirs: vec!["runtime/wine/lib/dxvk/x86_64-windows", "runtime/wine/lib/dxvk/i386-windows"],
            shader_cache_lane: Some("dxvk-d11"),
            fallback_lanes: vec!["m11", "m10", "wine_bare"],
            doctor_checks: vec!["dxvk_dlls", "moltenvk", "vulkan_icd", "state_cache"],
            repair_actions: vec!["install_dxvk_runtime", "fix_moltenvk_icd"],
            notes: "Available experimental fallback for D3D10/D3D11 games where DXMT regresses.",
        },
        RuntimeLaneContract {
            id: "vkd3d_d12",
            name: "VKD3D-Proton D3D12",
            family: "wine-vulkan",
            status: RuntimeLaneStatus::Available,
            source_scopes: vec!["steam", "sharp_library"],
            requires_wine: true,
            supports_win32: false,
            supports_win64: true,
            host_arch: "x86_64-rosetta",
            windows_arch: "win64",
            graphics_apis: vec!["d3d12", "dxgi", "vulkan", "moltenvk"],
            prefix_policy: "source-owned-prefix",
            runtime_surfaces: vec![RuntimeSurfaceId::Wine, RuntimeSurfaceId::Vkd3d],
            runtime_surface_paths: paths_for(&[RuntimeSurfaceId::Wine, RuntimeSurfaceId::Vkd3d]),
            required_pe_dlls: vec!["d3d12.dll", "d3d12core.dll", "dxgi.dll"],
            required_unix_sidecars: vec!["libMoltenVK.dylib", "Vulkan ICD"],
            dyld_paths: vec!["runtime/wine/lib/wine/x86_64-unix", "runtime/wine/lib/vkd3d/x86_64-unix"],
            winedllpath_dirs: vec!["runtime/wine/lib/vkd3d/x86_64-windows"],
            shader_cache_lane: Some("vkd3d-d12"),
            fallback_lanes: vec!["m12_dxmt_m12", "d3dmetal_gptk", "m11"],
            doctor_checks: vec!["vkd3d_dlls", "vkd3d_sidecars", "moltenvk", "vulkan_icd", "feature_limits"],
            repair_actions: vec!["install_vkd3d_runtime", "fix_moltenvk_icd"],
            notes: "Available experimental D3D12 escape hatch below M12/dxmt_m12 and D3DMetal priority.",
        },
        RuntimeLaneContract {
            id: "d3dmetal_gptk",
            name: "D3DMetal",
            family: "gptk-d3dmetal",
            status: RuntimeLaneStatus::External,
            source_scopes: vec!["steam", "sharp_library"],
            requires_wine: true,
            supports_win32: false,
            supports_win64: true,
            host_arch: "x86_64-rosetta",
            windows_arch: "win64",
            graphics_apis: vec!["d3d11", "d3d12", "d3dmetal"],
            prefix_policy: "prefix-gptk",
            runtime_surfaces: vec![RuntimeSurfaceId::D3DMetal],
            runtime_surface_paths: paths_for(&[RuntimeSurfaceId::D3DMetal]),
            required_pe_dlls: vec!["d3d10.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "nvapi64.dll"],
            required_unix_sidecars: vec!["D3DMetal.framework"],
            dyld_paths: vec!["Game Porting Toolkit.app/Contents/Resources/wine/lib/wine/x86_64-unix"],
            winedllpath_dirs: vec![],
            shader_cache_lane: Some("d3dmetal"),
            fallback_lanes: vec!["m12_dxmt_m12", "m11", "vkd3d_d12"],
            doctor_checks: vec!["homebrew_gptk", "rosetta", "gptk_prefix", "vcrun2019"],
            repair_actions: vec!["install_homebrew_gptk", "seed_gptk_prefix", "install_gptk_redist"],
            notes: "Explicit external GPTK lane. It must not mix GPTK route DLLs into DXMT runtime surfaces.",
        },
        RuntimeLaneContract {
            id: "wine_bare",
            name: "Plain Wine",
            family: "wine",
            status: RuntimeLaneStatus::Available,
            source_scopes: vec!["sharp_library", "steam", "gog"],
            requires_wine: true,
            supports_win32: true,
            supports_win64: true,
            host_arch: "x86_64-rosetta",
            windows_arch: "wow64",
            graphics_apis: vec!["wined3d", "opengl", "builtin"],
            prefix_policy: "source-owned-prefix",
            runtime_surfaces: vec![RuntimeSurfaceId::Wine],
            runtime_surface_paths: paths_for(&[RuntimeSurfaceId::Wine]),
            required_pe_dlls: vec![],
            required_unix_sidecars: vec![],
            dyld_paths: vec!["runtime/wine/lib/wine/x86_64-unix"],
            winedllpath_dirs: vec![],
            shader_cache_lane: Some("wine-bare"),
            fallback_lanes: vec!["m9", "m11"],
            doctor_checks: vec!["wine_runtime", "prefix_wineboot"],
            repair_actions: vec!["repair_wine_runtime", "run_wineboot_update"],
            notes: "Minimal Wine route for installers, launchers, and debugging.",
        },
        RuntimeLaneContract {
            id: "steam_background",
            name: "Wine Steam Background Client",
            family: "store-steam",
            status: RuntimeLaneStatus::Available,
            source_scopes: vec!["steam"],
            requires_wine: true,
            supports_win32: true,
            supports_win64: true,
            host_arch: "x86_64-rosetta",
            windows_arch: "wow64",
            graphics_apis: vec!["store", "cef"],
            prefix_policy: "prefix-steam",
            runtime_surfaces: vec![RuntimeSurfaceId::Wine, RuntimeSurfaceId::SteamBridge],
            runtime_surface_paths: paths_for(&[RuntimeSurfaceId::Wine, RuntimeSurfaceId::SteamBridge]),
            required_pe_dlls: vec!["steamwebhelper.exe wrapper", "steam_api.dll", "steam_api64.dll"],
            required_unix_sidecars: vec!["libsteam_api.dylib"],
            dyld_paths: vec!["runtime/wine/lib/wine/x86_64-unix"],
            winedllpath_dirs: vec![],
            shader_cache_lane: Some("steam-wine"),
            fallback_lanes: vec!["macos_steam"],
            doctor_checks: vec!["steam_installed", "steam_running", "steam_cef_wrapper", "prefix_wineboot"],
            repair_actions: vec!["install_steam", "redeploy_steamwebhelper_wrapper", "run_wineboot_update"],
            notes: "Store/session owner for Steam games. Env-dependent routes launch game executables directly while Steam stays alive.",
        },
        RuntimeLaneContract {
            id: "gogdl_wine",
            name: "GOGDL Wine",
            family: "store-gog",
            status: RuntimeLaneStatus::Available,
            source_scopes: vec!["gog"],
            requires_wine: true,
            supports_win32: true,
            supports_win64: true,
            host_arch: "x86_64-rosetta",
            windows_arch: "wow64",
            graphics_apis: vec!["store", "gogdl", "wine"],
            prefix_policy: "bottles/gog-prefix/prefix",
            runtime_surfaces: vec![RuntimeSurfaceId::Wine, RuntimeSurfaceId::Gogdl],
            runtime_surface_paths: paths_for(&[RuntimeSurfaceId::Wine, RuntimeSurfaceId::Gogdl]),
            required_pe_dlls: vec![],
            required_unix_sidecars: vec![],
            dyld_paths: vec!["runtime/wine/lib/wine/x86_64-unix"],
            winedllpath_dirs: vec![],
            shader_cache_lane: Some("gog-wine"),
            fallback_lanes: vec!["wine_bare", "m11", "m12_dxmt_m12"],
            doctor_checks: vec!["gogdl_binary", "gog_auth", "gog_prefix", "wine_runtime"],
            repair_actions: vec!["provision_gogdl", "initialize_gog_prefix", "refresh_gog_auth"],
            notes: "GOG source route using heroic-gogdl and dedicated gog-prefix. It must never use prefix-steam.",
        },
    ]
}

pub fn handle_runtime_contracts() -> Value {
    let surfaces: Vec<Value> = [
        RuntimeSurfaceId::Wine,
        RuntimeSurfaceId::Dxmt,
        RuntimeSurfaceId::DxmtM12,
        RuntimeSurfaceId::Dxvk,
        RuntimeSurfaceId::Vkd3d,
        RuntimeSurfaceId::D3DMetal,
        RuntimeSurfaceId::MonoArm64,
        RuntimeSurfaceId::MonoX86,
        RuntimeSurfaceId::MetalsharpHooks,
        RuntimeSurfaceId::SteamBridge,
        RuntimeSurfaceId::Gogdl,
    ]
    .into_iter()
    .map(|surface| {
        json!({
            "id": surface.canonical_id(),
            "installedPath": surface.installed_path(),
        })
    })
    .collect();

    json!({
        "ok": true,
        "schema": "metalsharp.runtime.contracts.v1",
        "canonicalM12Surface": RuntimeSurfaceId::DxmtM12.canonical_id(),
        "canonicalM12InstalledPath": RuntimeSurfaceId::DxmtM12.installed_path(),
        "surfaces": surfaces,
        "contracts": runtime_lane_contracts(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dxmt_m12_surface_is_canonical() {
        assert_eq!(RuntimeSurfaceId::DxmtM12.canonical_id(), "dxmt_m12");
        assert_eq!(RuntimeSurfaceId::DxmtM12.installed_path(), Some("runtime/wine/lib/dxmt_m12"));
    }

    #[test]
    fn m12_contract_uses_dxmt_m12_not_hyphenated_runtime_path() {
        let contracts = runtime_lane_contracts();
        let m12 = contracts.iter().find(|contract| contract.id == "m12_dxmt_m12").expect("M12 contract");
        assert!(m12.runtime_surfaces.contains(&RuntimeSurfaceId::DxmtM12));
        assert!(m12.runtime_surface_paths.iter().any(|path| path.ends_with("dxmt_m12")));
        assert!(m12.dyld_paths.iter().all(|path| !path.contains("dxmt-m12")));
        assert!(m12.winedllpath_dirs.iter().all(|path| !path.contains("dxmt-m12")));
        assert_eq!(m12.status, RuntimeLaneStatus::Available);
    }

    #[test]
    fn runtime_contracts_cover_native_dxmt_vulkan_store_and_gptk_lanes() {
        let ids: std::collections::HashSet<_> =
            runtime_lane_contracts().into_iter().map(|contract| contract.id).collect();
        for required in [
            "native_mono_arm64",
            "native_mono_x86",
            "m9",
            "m10",
            "m11",
            "m12_dxmt_m12",
            "dxvk_d9",
            "dxvk_d11",
            "vkd3d_d12",
            "d3dmetal_gptk",
            "wine_bare",
            "steam_background",
            "gogdl_wine",
        ] {
            assert!(ids.contains(required), "missing runtime contract {required}");
        }
    }

    #[test]
    fn vulkan_lanes_are_available_experimental_fallbacks() {
        for contract in runtime_lane_contracts().into_iter().filter(|contract| contract.family == "wine-vulkan") {
            assert_eq!(contract.status, RuntimeLaneStatus::Available);
            assert!(contract.doctor_checks.iter().any(|check| check.contains("vulkan") || check.contains("moltenvk")));
            assert!(contract.notes.contains("experimental"));
        }
    }

    #[test]
    fn runtime_lane_contracts_map_to_mtsp_pipelines() {
        use crate::mtsp::engine::PipelineId;
        assert_eq!(runtime_contract_id_for_pipeline(PipelineId::M12), Some("m12_dxmt_m12"));
        assert_eq!(runtime_contract_id_for_pipeline(PipelineId::DxvkD9), Some("dxvk_d9"));
        assert_eq!(runtime_contract_id_for_pipeline(PipelineId::DxvkD11), Some("dxvk_d11"));
        assert_eq!(runtime_contract_id_for_pipeline(PipelineId::Vkd3dD12), Some("vkd3d_d12"));
        assert_eq!(runtime_contract_id_for_pipeline(PipelineId::FnaArm64), Some("native_mono_arm64"));
        assert_eq!(runtime_lane_pipeline_id("m12_dxmt_m12"), Some(PipelineId::M12));
        assert_eq!(runtime_lane_pipeline_id("dxvk_d9"), Some(PipelineId::DxvkD9));
        assert_eq!(runtime_lane_pipeline_id("dxvk_d11"), Some(PipelineId::DxvkD11));
        assert_eq!(runtime_lane_pipeline_id("vkd3d_d12"), Some(PipelineId::Vkd3dD12));
        assert_eq!(runtime_lane_pipeline_id("d3dmetal_gptk"), Some(PipelineId::D3DMetal));
        assert_eq!(runtime_lane_pipeline_id("gogdl_wine"), None);
    }

    #[test]
    fn user_selectable_pipelines_have_runtime_contract_ids() {
        let contracts: std::collections::HashSet<_> =
            runtime_lane_contracts().into_iter().map(|contract| contract.id).collect();
        for pipeline in crate::mtsp::engine::pipelines().iter().filter(|pipeline| pipeline.id.is_user_selectable()) {
            let contract_id = runtime_contract_id_for_pipeline(pipeline.id)
                .unwrap_or_else(|| panic!("{} has no runtime contract id", pipeline.name));
            assert!(contracts.contains(contract_id), "{} maps to missing contract {}", pipeline.name, contract_id);
        }
    }

    #[test]
    fn contract_surface_paths_match_pipeline_route_surfaces() {
        use crate::mtsp::engine::{get_pipeline, PipelineId};
        let contracts = runtime_lane_contracts();
        for (lane_id, pipeline_id, expected_path) in [
            ("dxvk_d9", PipelineId::DxvkD9, "runtime/wine/lib/dxvk"),
            ("dxvk_d11", PipelineId::DxvkD11, "runtime/wine/lib/dxvk"),
            ("vkd3d_d12", PipelineId::Vkd3dD12, "runtime/wine/lib/vkd3d"),
        ] {
            let contract = contracts.iter().find(|contract| contract.id == lane_id).expect("contract");
            let pipeline = get_pipeline(pipeline_id);
            assert!(contract.runtime_surface_paths.contains(&expected_path), "{lane_id} missing {expected_path}");
            assert_eq!(contract.shader_cache_lane, pipeline.shader_cache_subdir);
            for dir in &pipeline.winedllpath_dirs {
                assert!(
                    contract.winedllpath_dirs.iter().any(|contract_dir| contract_dir.ends_with(dir)),
                    "{lane_id} contract missing WINEDLLPATH dir {dir}"
                );
            }
        }
    }
}
