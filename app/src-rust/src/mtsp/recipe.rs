use super::engine::{PipelineId, PipelineNode};
use serde::Serialize;
use std::path::{Path, PathBuf};
use walkdir::WalkDir;

#[derive(Debug, Clone, Serialize)]
pub struct LaunchRecipe {
    pub appid: u32,
    pub pipeline: PipelineId,
    pub pipeline_name: String,
    pub backend: String,
    pub game_dir: Option<PathBuf>,
    pub exe_path: Option<PathBuf>,
    pub exe_name: Option<String>,
    pub launch_args: Vec<String>,
    pub env: Vec<RecipeEnv>,
    pub dlls: Vec<RecipeDll>,
    pub runtime_assets: Vec<RuntimeAsset>,
    pub anti_cheat: Vec<String>,
    pub anti_cheat_status: Vec<AntiCheatCompatibility>,
    pub warnings: Vec<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct RecipeEnv {
    pub key: String,
    pub value: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct RecipeDll {
    pub source_subpath: String,
    pub filename: String,
    pub source_path: PathBuf,
    pub dest_path: PathBuf,
    pub source_present: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct RuntimeAsset {
    pub name: String,
    pub path: PathBuf,
    pub required: bool,
    pub present: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct AntiCheatCompatibility {
    pub name: String,
    pub status: String,
    pub reason: String,
    pub evidence: Vec<String>,
    pub allowed_actions: Vec<String>,
    pub forbidden_actions: Vec<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct LaunchDoctorReport {
    pub ready: bool,
    pub summary: String,
    pub blockers: Vec<String>,
    pub warnings: Vec<String>,
    pub checks: Vec<LaunchDoctorCheck>,
    pub recipe: LaunchRecipe,
}

#[derive(Debug, Clone, Serialize)]
pub struct LaunchDoctorCheck {
    pub id: String,
    pub label: String,
    pub ok: bool,
    pub detail: String,
}

#[derive(Debug, Clone)]
struct ExeCandidate {
    path: PathBuf,
    score: i32,
}

pub fn build_launch_recipe(appid: u32, node: &PipelineNode) -> Result<LaunchRecipe, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let direct_wine_pipeline = matches!(
        node.id,
        PipelineId::Dxmt
            | PipelineId::M9
            | PipelineId::M10
            | PipelineId::M11
            | PipelineId::M12
            | PipelineId::M13
            | PipelineId::D3DMetal
            | PipelineId::M32
            | PipelineId::FnaArm64
            | PipelineId::WineBare
    );
    let game_dir = if direct_wine_pipeline {
        crate::setup::resolve_windows_game_dir(appid)
    } else {
        crate::setup::resolve_game_dir(appid)
    };

    let exe_path = match node.id {
        PipelineId::Dxmt
        | PipelineId::M9
        | PipelineId::M10
        | PipelineId::M11
        | PipelineId::M12
        | PipelineId::M13
        | PipelineId::D3DMetal
        | PipelineId::M32
        | PipelineId::FnaArm64
        | PipelineId::WineBare => {
            let dir = game_dir.as_ref().ok_or_else(|| format!("game directory not found for appid {}", appid))?;
            Some(resolve_game_exe(appid, dir)?)
        },
        _ => None,
    };

    let dlls = match game_dir.as_ref() {
        Some(dir) => selected_deploy_dlls_for_pipeline(dir, exe_path.as_deref(), node, &ms_root),
        None => Vec::new(),
    };

    let anti_cheat = game_dir.as_ref().map(detect_anti_cheat).unwrap_or_default();
    let anti_cheat_status =
        game_dir.as_ref().map(|dir| classify_anti_cheat_components(dir, &anti_cheat)).unwrap_or_default();
    let mut warnings = Vec::new();
    if !anti_cheat.is_empty() {
        warnings.push(format!(
            "Detected anti-cheat components: {}. MetalSharp should use publisher-supported modes only.",
            anti_cheat.join(", ")
        ));
    }
    if exe_path.as_ref().map(|p| is_likely_launcher_exe(p)).unwrap_or(false) {
        warnings.push(
            "Selected executable still looks like a launcher; add an app-specific exe override if launch stalls."
                .into(),
        );
    }

    Ok(LaunchRecipe {
        appid,
        pipeline: node.id,
        pipeline_name: node.name.to_string(),
        backend: node.backend.to_string(),
        game_dir,
        exe_name: exe_path.as_ref().and_then(|p| p.file_name()).map(|n| n.to_string_lossy().to_string()),
        exe_path,
        launch_args: effective_launch_args(appid, node),
        env: node
            .env_vars
            .iter()
            .map(|ev| RecipeEnv { key: ev.key.to_string(), value: ev.value.to_string() })
            .collect(),
        dlls,
        runtime_assets: runtime_assets_for_node(node, &ms_root),
        anti_cheat,
        anti_cheat_status,
        warnings,
    })
}

pub fn effective_launch_args(appid: u32, node: &PipelineNode) -> Vec<String> {
    let mut launch_args: Vec<String> = node.launch_args.iter().map(|arg| arg.to_string()).collect();
    if appid == 1962700 && node.id == PipelineId::M12 {
        launch_args.retain(|arg| !arg.eq_ignore_ascii_case("-NOSPLASH"));
    }
    if appid == 2050650 && node.id == PipelineId::M12 {
        launch_args.clear();
    }
    append_app_launch_args(appid, node.id, &mut launch_args);
    launch_args
}

fn append_app_launch_args(appid: u32, pipeline: PipelineId, launch_args: &mut Vec<String>) {
    if appid == 1962700 && pipeline == PipelineId::M12 {
        let dpcvars = [
            "r.Nanite=0",
            "r.Nanite.ProjectEnabled=0",
            "r.Nanite.AllowTessellation=0",
            "r.Nanite.Tessellation=0",
            "r.Nanite.SkinnedMeshes=0",
            "r.Nanite.AsyncRasterization=0",
            "r.GeometryCollection.Nanite=0",
            "r.RayTracing=0",
            "r.Lumen.HardwareRayTracing=0",
            "r.Shadow.Virtual.Enable=0",
        ]
        .join(",");
        launch_args.push("-dx12".into());
        launch_args.push("-d3d12".into());
        launch_args.push(format!("-dpcvars={}", dpcvars));
        launch_args.push("-NoNanite".into());
        launch_args.push(
            "-ExecCmds=r.Nanite 0;r.Nanite.ProjectEnabled 0;r.Nanite.Tessellation 0;r.GeometryCollection.Nanite 0"
                .into(),
        );
    }

    append_database_default_launch_args(appid, pipeline, launch_args);

    if uses_steam_launch_model(appid, pipeline) {
        append_unique_launch_arg(launch_args, "-steam");
    }
    if uses_steam_secure_launch_model(appid, pipeline) {
        append_unique_launch_arg(launch_args, "-secure");
    }

    match (appid, pipeline) {
        (1196590 | 1623730 | 1928870 | 2358720 | 2456740, PipelineId::M12) => {
            append_unique_launch_arg(launch_args, "-dx12");
            append_unique_launch_arg(launch_args, "-d3d12");
        },
        (1623730 | 2358720, PipelineId::M11) => {
            append_unique_launch_arg(launch_args, "-dx11");
            append_unique_launch_arg(launch_args, "-d3d11");
        },
        _ => {},
    }
}

fn append_database_default_launch_args(appid: u32, pipeline: PipelineId, launch_args: &mut Vec<String>) {
    for arg in database_default_launch_args(appid, pipeline) {
        append_unique_launch_arg(launch_args, arg);
    }
}

fn database_default_launch_args(appid: u32, pipeline: PipelineId) -> &'static [&'static str] {
    match appid {
        379720 | 275850 | 892970 | 252490 | 570 | 548430 | 526870 | 1272080 => &["-vulkan"],
        949230 => &["-force-vulkan"],
        1174180 => &["-api", "Vulkan"],
        400 | 620 | 4000 if pipeline == PipelineId::M9 => &["-dxlevel", "90", "-novid"],
        240 | 500 | 550 if pipeline == PipelineId::M9 => &["-dxlevel", "90"],
        7670 if pipeline == PipelineId::M9 => &["-dx9"],
        12210 if pipeline == PipelineId::M10 => &["-d3d10"],
        17300 if pipeline == PipelineId::M10 => &["-dx10"],
        _ => &[],
    }
}

pub(crate) fn requires_steam_secure_launch_args(appid: u32) -> bool {
    matches!(appid, 440 | 252490 | 271590 | 284160 | 292030 | 1172380 | 3241660)
}

pub(crate) fn requires_steam_launch_args(appid: u32) -> bool {
    matches!(appid, 620 | 4000 | 1260320) || requires_steam_secure_launch_args(appid)
}

pub(crate) fn uses_steam_launch_model(appid: u32, pipeline: PipelineId) -> bool {
    requires_steam_launch_args(appid) && !matches!(pipeline, PipelineId::M13 | PipelineId::D3DMetal)
}

pub(crate) fn uses_steam_secure_launch_model(appid: u32, pipeline: PipelineId) -> bool {
    requires_steam_secure_launch_args(appid) && !matches!(pipeline, PipelineId::M13 | PipelineId::D3DMetal)
}

fn append_unique_launch_arg(launch_args: &mut Vec<String>, arg: &str) {
    if !launch_args.iter().any(|existing| existing.eq_ignore_ascii_case(arg)) {
        launch_args.push(arg.to_string());
    }
}

pub fn build_custom_launch_recipe(
    appid: u32,
    node: &PipelineNode,
    game_dir: &Path,
    exe_path: Option<&Path>,
) -> Result<LaunchRecipe, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let exe_path = match node.id {
        PipelineId::Dxmt
        | PipelineId::M9
        | PipelineId::M10
        | PipelineId::M11
        | PipelineId::M12
        | PipelineId::M13
        | PipelineId::M32
        | PipelineId::WineBare => Some(match exe_path {
            Some(path) => path.to_path_buf(),
            None => resolve_game_exe(appid, game_dir)?,
        }),
        _ => None,
    };
    let game_dir = game_dir.to_path_buf();
    let dlls = selected_deploy_dlls_for_pipeline(&game_dir, exe_path.as_deref(), node, &ms_root);
    let anti_cheat = detect_anti_cheat(&game_dir);
    let anti_cheat_status = classify_anti_cheat_components(&game_dir, &anti_cheat);
    let mut warnings = Vec::new();

    if !anti_cheat.is_empty() {
        warnings.push(format!(
            "Detected anti-cheat components: {}. MetalSharp should use publisher-supported modes only.",
            anti_cheat.join(", ")
        ));
    }
    if exe_path.as_ref().map(|p| is_likely_launcher_exe(p)).unwrap_or(false) {
        warnings.push(
            "Selected executable still looks like a launcher; add an app-specific exe override if launch stalls."
                .into(),
        );
    }

    Ok(LaunchRecipe {
        appid,
        pipeline: node.id,
        pipeline_name: node.name.to_string(),
        backend: node.backend.to_string(),
        game_dir: Some(game_dir),
        exe_name: exe_path.as_ref().and_then(|p| p.file_name()).map(|n| n.to_string_lossy().to_string()),
        exe_path,
        launch_args: effective_launch_args(appid, node),
        env: node
            .env_vars
            .iter()
            .map(|ev| RecipeEnv { key: ev.key.to_string(), value: ev.value.to_string() })
            .collect(),
        dlls,
        runtime_assets: runtime_assets_for_node(node, &ms_root),
        anti_cheat,
        anti_cheat_status,
        warnings,
    })
}

pub fn resolve_game_exe(appid: u32, game_dir: &Path) -> Result<PathBuf, Box<dyn std::error::Error>> {
    for preferred in preferred_exe_names(appid) {
        let path = find_case_insensitive(game_dir, preferred);
        if let Some(path) = path {
            return Ok(path);
        }
    }

    let mut candidates = Vec::new();
    let dir_name = game_dir.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
    for entry in WalkDir::new(game_dir).max_depth(5).into_iter().flatten() {
        let path = entry.path();
        if !path.is_file() || path.extension().map(|ext| ext.to_string_lossy().to_lowercase()) != Some("exe".into()) {
            continue;
        }
        let Some(name) = path.file_name().map(|n| n.to_string_lossy().to_string()) else {
            continue;
        };
        if !is_valid_game_exe(&name) {
            continue;
        }
        candidates.push(ExeCandidate { score: score_exe_candidate(path, &name, &dir_name), path: path.to_path_buf() });
    }

    candidates.sort_by(|a, b| b.score.cmp(&a.score).then_with(|| a.path.cmp(&b.path)));
    candidates
        .into_iter()
        .next()
        .map(|c| c.path)
        .ok_or_else(|| format!("no launchable .exe found in {}", game_dir.display()).into())
}

pub fn selected_deploy_dlls_for_pipeline(
    game_dir: &Path,
    exe_path: Option<&Path>,
    node: &PipelineNode,
    ms_root: &Path,
) -> Vec<RecipeDll> {
    let d3d9_subpath = if node.id == PipelineId::M9 { m9_d3d9_source_subpath(game_dir, exe_path) } else { "" };
    let target_dirs = deploy_target_dirs_for_pipeline(game_dir, exe_path, node);

    node.deploy_dlls
        .iter()
        .filter(|dll| node.id != PipelineId::M9 || dll.source_subpath == d3d9_subpath)
        .flat_map(|dll| {
            let source_path = ms_root.join(dll.source_subpath).join(dll.filename);
            let dest_name = dll.dest_filename.unwrap_or(dll.filename);
            target_dirs.iter().map(move |target_dir| RecipeDll {
                source_subpath: dll.source_subpath.to_string(),
                filename: dll.filename.to_string(),
                source_present: source_path.exists(),
                source_path: source_path.clone(),
                dest_path: target_dir.join(dest_name),
            })
        })
        .collect()
}

fn deploy_target_dirs_for_pipeline(game_dir: &Path, exe_path: Option<&Path>, node: &PipelineNode) -> Vec<PathBuf> {
    let primary = exe_path.and_then(Path::parent).unwrap_or(game_dir).to_path_buf();
    let mut dirs = vec![primary.clone()];

    if node.id == PipelineId::M12 {
        let engine_bin = game_dir.join("Engine").join("Binaries").join("Win64");
        if engine_bin.is_dir() && engine_bin != primary {
            dirs.push(engine_bin);
        }
    }

    dirs
}

pub fn is_likely_launcher_exe(path: &Path) -> bool {
    let name = path.file_name().map(|n| n.to_string_lossy().to_lowercase()).unwrap_or_default();
    ["launcher", "bootstrap", "updater", "webhelper"].iter().any(|needle| name.contains(needle))
}

pub fn diagnose_launch_request(appid: u32, node: &PipelineNode) -> LaunchDoctorReport {
    match build_launch_recipe(appid, node) {
        Ok(recipe) => diagnose_recipe(recipe),
        Err(error) => {
            let home = dirs::home_dir().unwrap_or_else(|| PathBuf::from("."));
            let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
            let error = error.to_string();
            let recipe = LaunchRecipe {
                appid,
                pipeline: node.id,
                pipeline_name: node.name.to_string(),
                backend: node.backend.to_string(),
                game_dir: crate::setup::resolve_game_dir(appid),
                exe_path: None,
                exe_name: None,
                launch_args: effective_launch_args(appid, node),
                env: node
                    .env_vars
                    .iter()
                    .map(|ev| RecipeEnv { key: ev.key.to_string(), value: ev.value.to_string() })
                    .collect(),
                dlls: Vec::new(),
                runtime_assets: runtime_assets_for_node(node, &ms_root),
                anti_cheat: Vec::new(),
                anti_cheat_status: Vec::new(),
                warnings: Vec::new(),
            };
            let mut report = diagnose_recipe(recipe);
            report.ready = false;
            report.blockers.insert(0, format!("Recipe build did not complete: {}", error));
            report.summary = format!("Blocked: {}", error);
            report
        },
    }
}

pub fn diagnose_recipe(recipe: LaunchRecipe) -> LaunchDoctorReport {
    let mut checks = Vec::new();
    let mut blockers = Vec::new();
    let mut warnings = recipe.warnings.clone();
    let direct_wine_pipeline = matches!(
        recipe.pipeline,
        PipelineId::Dxmt
            | PipelineId::M9
            | PipelineId::M10
            | PipelineId::M11
            | PipelineId::M12
            | PipelineId::M13
            | PipelineId::M32
            | PipelineId::FnaArm64
            | PipelineId::WineBare
    );
    let requires_game_dir = !matches!(recipe.pipeline, PipelineId::Steam | PipelineId::MacSteam);

    if requires_game_dir {
        match recipe.game_dir.as_deref() {
            Some(path) if path.is_dir() => {
                push_check(&mut checks, "game_dir", "Game folder", true, format!("Found {}", path.display()))
            },
            Some(path) => {
                let detail = format!("Missing {}", path.display());
                blockers.push(detail.clone());
                push_check(&mut checks, "game_dir", "Game folder", false, detail);
            },
            None => {
                let detail = "No installed game folder was resolved".to_string();
                blockers.push(detail.clone());
                push_check(&mut checks, "game_dir", "Game folder", false, detail);
            },
        }
    } else {
        push_check(&mut checks, "game_dir", "Game folder", true, "Steam owns install resolution");
    }

    if direct_wine_pipeline {
        match recipe.exe_path.as_deref() {
            Some(path) if path.is_file() => {
                push_check(&mut checks, "exe", "Executable", true, format!("Selected {}", path.display()))
            },
            Some(path) => {
                let detail = format!("Selected executable is missing: {}", path.display());
                blockers.push(detail.clone());
                push_check(&mut checks, "exe", "Executable", false, detail);
            },
            None => {
                let detail = "No Windows executable was selected".to_string();
                blockers.push(detail.clone());
                push_check(&mut checks, "exe", "Executable", false, detail);
            },
        }
    } else {
        push_check(&mut checks, "exe", "Executable", true, "Not required for this pipeline");
    }

    if direct_wine_pipeline {
        inspect_exe_route_compatibility(&recipe, &mut checks, &mut blockers, &mut warnings);
    }

    let missing_assets: Vec<_> =
        recipe.runtime_assets.iter().filter(|asset| asset.required && !asset.present).collect();
    if missing_assets.is_empty() {
        let detail = if recipe.runtime_assets.is_empty() {
            "No runtime assets required".to_string()
        } else {
            format!("{} runtime asset(s) available", recipe.runtime_assets.len())
        };
        push_check(&mut checks, "runtime_assets", "Runtime assets", true, detail);
    } else {
        let detail = missing_assets
            .iter()
            .map(|asset| format!("{} ({})", asset.name, asset.path.display()))
            .collect::<Vec<_>>()
            .join(", ");
        blockers.push(format!("Missing required runtime asset(s): {}", detail));
        push_check(&mut checks, "runtime_assets", "Runtime assets", false, detail);
    }

    let missing_dlls: Vec<_> = recipe.dlls.iter().filter(|dll| !dll.source_present).collect();
    if missing_dlls.is_empty() {
        let detail = if recipe.dlls.is_empty() {
            "No DLL deployment needed".to_string()
        } else {
            format!("{} DLL source(s) available", recipe.dlls.len())
        };
        push_check(&mut checks, "dll_sources", "DLL sources", true, detail);
    } else {
        let detail = missing_dlls
            .iter()
            .map(|dll| format!("{} ({})", dll.filename, dll.source_path.display()))
            .collect::<Vec<_>>()
            .join(", ");
        blockers.push(format!("Missing DLL source(s): {}", detail));
        push_check(&mut checks, "dll_sources", "DLL sources", false, detail);
    }

    let missing_target_dirs: Vec<_> =
        recipe.dlls.iter().filter_map(|dll| dll.dest_path.parent()).filter(|parent| !parent.is_dir()).collect();
    if missing_target_dirs.is_empty() {
        let detail = if recipe.dlls.is_empty() {
            "No DLL target needed".to_string()
        } else {
            "DLLs will be placed next to the selected executable".to_string()
        };
        push_check(&mut checks, "dll_targets", "DLL targets", true, detail);
    } else {
        let detail =
            missing_target_dirs.iter().map(|parent| parent.display().to_string()).collect::<Vec<_>>().join(", ");
        blockers.push(format!("Missing DLL target folder(s): {}", detail));
        push_check(&mut checks, "dll_targets", "DLL targets", false, detail);
    }

    if recipe.anti_cheat.is_empty() {
        push_check(&mut checks, "anti_cheat", "Anti-cheat", true, "No common anti-cheat folders detected");
    } else {
        let blocking = recipe
            .anti_cheat_status
            .iter()
            .filter(|entry| {
                matches!(entry.status.as_str(), "unsupported_kernel_driver" | "blocked_pending_vendor_support")
            })
            .collect::<Vec<_>>();
        for entry in &blocking {
            blockers.push(format!("{}: {}", entry.name, entry.reason));
        }
        let detail = if recipe.anti_cheat_status.is_empty() {
            format!("Detected {}; use publisher-supported modes only", recipe.anti_cheat.join(", "))
        } else {
            recipe
                .anti_cheat_status
                .iter()
                .map(|entry| format!("{}={}", entry.name, entry.status))
                .collect::<Vec<_>>()
                .join(", ")
        };
        push_check(&mut checks, "anti_cheat", "Anti-cheat", blocking.is_empty(), detail);
    }

    if recipe.exe_path.as_ref().map(|path| is_likely_launcher_exe(path)).unwrap_or(false) {
        let detail = "Selected executable looks like a launcher and may stall".to_string();
        warnings.push(detail.clone());
        push_check(&mut checks, "launcher_exe", "Launcher check", true, detail);
    } else {
        push_check(
            &mut checks,
            "launcher_exe",
            "Launcher check",
            true,
            "Selected executable does not look like a launcher",
        );
    }

    let ready = blockers.is_empty();
    let summary = if ready {
        format!("Ready for {} via {}", recipe.pipeline_name, recipe.backend)
    } else {
        format!("Blocked by {} launch prerequisite(s)", blockers.len())
    };

    LaunchDoctorReport { ready, summary, blockers, warnings: dedupe_strings(warnings), checks, recipe }
}

fn inspect_exe_route_compatibility(
    recipe: &LaunchRecipe,
    checks: &mut Vec<LaunchDoctorCheck>,
    blockers: &mut Vec<String>,
    warnings: &mut Vec<String>,
) {
    let Some(exe_path) = recipe.exe_path.as_deref() else {
        push_check(checks, "exe_route", "Route compatibility", false, "No executable to inspect");
        return;
    };
    if !exe_path.is_file() {
        push_check(
            checks,
            "exe_route",
            "Route compatibility",
            false,
            "Selected executable is not available for route inspection",
        );
        return;
    }

    let Ok(data) = std::fs::read(exe_path) else {
        push_check(checks, "exe_route", "Route compatibility", false, "Could not read selected executable");
        warnings.push("Could not inspect selected executable headers".into());
        return;
    };
    let Some(pe) = super::pe::parse_pe_imports(&data) else {
        push_check(
            checks,
            "exe_route",
            "Route compatibility",
            true,
            "Selected executable is not a readable PE file; route compatibility was not verified",
        );
        warnings.push("Selected executable headers could not be parsed; route compatibility was not verified".into());
        return;
    };

    let arch = if pe.is_64_bit { "PE32+ x86_64" } else { "PE32 i386" };
    let api = d3d_api_label(pe.detected_api);
    let detail = format!("{} executable, imports {}", arch, api);

    if !pe.is_64_bit
        && matches!(recipe.pipeline, PipelineId::Dxmt | PipelineId::M10 | PipelineId::M11 | PipelineId::M12)
    {
        let message = format!(
            "{} route requires a 64-bit Windows executable, but {} is 32-bit",
            recipe.pipeline_name,
            exe_path.display()
        );
        blockers.push(message.clone());
        push_check(checks, "exe_route", "Route compatibility", false, format!("{}; {}", detail, message));
        return;
    }

    if pe.is_64_bit && recipe.pipeline == PipelineId::M32 {
        let message = format!("M32 is reserved for 32-bit Windows executables, but {} is 64-bit", exe_path.display());
        blockers.push(message.clone());
        push_check(checks, "exe_route", "Route compatibility", false, format!("{}; {}", detail, message));
        return;
    }

    if route_api_mismatch(recipe.pipeline, pe.detected_api) {
        warnings.push(format!(
            "{} imports {}, which does not match the selected {} route",
            exe_path.display(),
            api,
            recipe.pipeline_name
        ));
    }

    push_check(checks, "exe_route", "Route compatibility", true, detail);
}

fn d3d_api_label(api: super::pe::D3dApi) -> &'static str {
    match api {
        super::pe::D3dApi::D3D9 => "D3D9",
        super::pe::D3dApi::D3D10 => "D3D10",
        super::pe::D3dApi::D3D11 => "D3D11",
        super::pe::D3dApi::D3D12 => "D3D12",
        super::pe::D3dApi::Unknown => "no Direct3D import",
    }
}

fn route_api_mismatch(pipeline: PipelineId, api: super::pe::D3dApi) -> bool {
    !matches!(
        (pipeline, api),
        (_, super::pe::D3dApi::Unknown)
            | (PipelineId::Dxmt, _)
            | (PipelineId::WineBare, _)
            | (PipelineId::M9, super::pe::D3dApi::D3D9)
            | (PipelineId::M10, super::pe::D3dApi::D3D10)
            | (PipelineId::M11, super::pe::D3dApi::D3D11)
            | (PipelineId::M12, super::pe::D3dApi::D3D12)
            | (PipelineId::M13, super::pe::D3dApi::D3D12)
            | (PipelineId::M32, _)
    )
}

fn push_check(
    checks: &mut Vec<LaunchDoctorCheck>,
    id: impl Into<String>,
    label: impl Into<String>,
    ok: bool,
    detail: impl Into<String>,
) {
    checks.push(LaunchDoctorCheck { id: id.into(), label: label.into(), ok, detail: detail.into() });
}

fn dedupe_strings(values: Vec<String>) -> Vec<String> {
    let mut deduped = Vec::new();
    for value in values {
        if !deduped.iter().any(|existing| existing == &value) {
            deduped.push(value);
        }
    }
    deduped
}

fn preferred_exe_names(appid: u32) -> &'static [&'static str] {
    match appid {
        379720 => &["DOOMx64vk.exe", "DOOMx64.exe"],
        782330 => &["DOOMEternalx64vk.exe", "DOOMEternalx64.exe"],
        105600 => &["TerrariaLauncher.exe", "Terraria.exe"],
        1196590 => &["re8.exe"],
        2358720 => &["b1-Win64-Shipping.exe", "b1.exe"],
        305620 => &["tld.exe"],
        1245620 => &["start_protected_game.exe", "eldenring.exe"],
        1962700 => &["Subnautica2.exe"],
        220 => &["hl2.exe"],
        440 => &["tf/win32/tf.exe", "tf.exe"],
        620 => &["portal2.exe"],
        2552430 => &[
            "KINGDOM HEARTS FINAL MIX.exe",
            "KINGDOM HEARTS Re_Chain of Memories.exe",
            "KINGDOM HEARTS II FINAL MIX.exe",
            "KINGDOM HEARTS Birth by Sleep FINAL MIX.exe",
        ],
        2552440 => &[
            "KINGDOM HEARTS 0.2 Birth by Sleep -A fragmentary passage-.exe",
            "KINGDOM HEARTS Dream Drop Distance.exe",
            "KINGDOM HEARTS Re_coded.exe",
        ],
        _ => &[],
    }
}

pub fn find_case_insensitive(dir: &Path, name: &str) -> Option<PathBuf> {
    let target = name.to_lowercase();
    for entry in WalkDir::new(dir).max_depth(5).into_iter().flatten() {
        if !entry.path().is_file() {
            continue;
        }
        if entry.file_name().to_string_lossy().to_lowercase() == target {
            return Some(entry.path().to_path_buf());
        }
    }
    None
}

fn is_valid_game_exe(name: &str) -> bool {
    let lower = name.to_lowercase();
    lower.ends_with(".exe")
        && !lower.contains("setup")
        && !lower.contains("redist")
        && !lower.contains("dotnet")
        && !lower.contains("installer")
        && !lower.contains("uninstall")
        && !lower.contains("vcredist")
        && !lower.contains("crashreport")
        && !lower.contains("crashhandler")
        && !lower.contains("server")
        && !lower.contains("steamwebhelper")
        && !lower.contains("start_protected")
        && !lower.contains("easyanticheat")
        && !lower.contains("d3dconfig")
}

fn score_exe_candidate(path: &Path, name: &str, dir_name: &str) -> i32 {
    let lower = name.to_lowercase();
    let rel = path.to_string_lossy().to_lowercase();
    let mut score = 0;

    if is_likely_launcher_exe(path) {
        score -= 75;
    }
    if lower == "game.exe" {
        score += 25;
    }

    let parent_count = path.parent().map(|p| p.components().count()).unwrap_or(0);
    if parent_count == 0 {
        score += 20;
    }

    if rel.contains("/bin/") || rel.contains("\\bin\\") {
        score -= 15;
    }

    if lower.contains("shipping") || rel.contains("binaries/win64") || rel.contains("binaries\\win64") {
        score += 35;
    }
    if lower.contains("win64") || lower.contains("x64") {
        score += 15;
    }
    if lower.contains("dx12") || lower.contains("d3d12") || lower.contains("vk") {
        score += 10;
    }

    for token in normalized_tokens(dir_name) {
        if token.len() >= 4 && lower.contains(&token) {
            score += 20;
        }
    }

    if let Ok(data) = std::fs::read(path) {
        if let Some(pe) = super::pe::parse_pe_imports(&data) {
            if pe.is_64_bit {
                score += 10;
            }
            if pe.detected_api != super::pe::D3dApi::Unknown {
                score += 15;
            }
        }
    }

    score
}

fn normalized_tokens(name: &str) -> Vec<String> {
    name.split(|c: char| !c.is_ascii_alphanumeric())
        .map(|s| s.to_lowercase())
        .filter(|s| !s.is_empty() && !["the", "and", "goty", "edition"].contains(&s.as_str()))
        .collect()
}

fn m9_d3d9_source_subpath(game_dir: &Path, exe_path: Option<&Path>) -> &'static str {
    let exe = match exe_path {
        Some(path) => path.to_path_buf(),
        None => match resolve_game_exe(0, game_dir) {
            Ok(path) => path,
            Err(_) => return "lib/wine/x86_64-windows",
        },
    };

    if let Ok(data) = std::fs::read(&exe) {
        if let Some(pe) = super::pe::parse_pe_imports(&data) {
            if !pe.is_64_bit {
                return "lib/wine/i386-windows";
            }
        }
    }

    "lib/wine/x86_64-windows"
}

fn runtime_assets_for_node(node: &PipelineNode, ms_root: &Path) -> Vec<RuntimeAsset> {
    let mut assets = Vec::new();

    if node.requires_wine {
        let wine = crate::platform::runtime_wine_binary(ms_root);
        assets.push(RuntimeAsset { name: "wine".into(), present: wine.exists(), path: wine, required: true });
    }

    for path in &node.dyld_paths {
        let p = ms_root.join(path);
        assets.push(RuntimeAsset { name: path.to_string(), present: p.exists(), path: p, required: true });
    }

    if node.backend == "dxmt" {
        let conf = ms_root.join("etc").join("dxmt.conf");
        assets.push(RuntimeAsset { name: "dxmt.conf".into(), present: conf.exists(), path: conf, required: false });
    }

    if node.backend == "d3dmetal" {
        let gptk_wine64 = crate::platform::gptk_wine64_binary();
        assets.push(RuntimeAsset {
            name: "GPTK wine64".into(),
            present: gptk_wine64.exists(),
            path: gptk_wine64,
            required: true,
        });
        let gptk_ws = crate::platform::gptk_wineserver_binary();
        assets.push(RuntimeAsset {
            name: "GPTK wineserver".into(),
            present: gptk_ws.exists(),
            path: gptk_ws,
            required: true,
        });
    }

    if node.backend == "gptk" {
        let framework = ms_root.join("lib").join("external").join("D3DMetal.framework");
        assets.push(RuntimeAsset {
            name: "D3DMetal.framework".into(),
            present: framework.exists(),
            path: framework,
            required: true,
        });
    }

    for dir in &node.winedllpath_dirs {
        let p = ms_root.join(dir);
        assets.push(RuntimeAsset { name: dir.to_string(), present: p.exists(), path: p, required: true });
    }

    assets
}

fn detect_anti_cheat(game_dir: &PathBuf) -> Vec<String> {
    let mut found = Vec::new();
    for entry in WalkDir::new(game_dir).max_depth(4).into_iter().flatten() {
        let name = entry.file_name().to_string_lossy().to_lowercase();
        let label = if name.contains("easyanticheat") || name == "eac.exe" {
            Some("Easy Anti-Cheat")
        } else if name.contains("battleye") {
            Some("BattlEye")
        } else if name.contains("gameguard") || name.contains("nprotect") {
            Some("nProtect GameGuard")
        } else if name.contains("equ8") {
            Some("EQU8")
        } else if name.contains("ace") && name.contains("anti") {
            Some("Anti-Cheat Expert")
        } else {
            None
        };

        if let Some(label) = label {
            if !found.iter().any(|existing| existing == label) {
                found.push(label.to_string());
            }
        }
    }
    found
}

fn classify_anti_cheat_components(game_dir: &Path, detected: &[String]) -> Vec<AntiCheatCompatibility> {
    detected.iter().map(|name| classify_anti_cheat_component(game_dir, name)).collect()
}

fn classify_anti_cheat_component(game_dir: &Path, name: &str) -> AntiCheatCompatibility {
    let evidence = anti_cheat_evidence(game_dir, name);
    let has_proton_assets = evidence.iter().any(|item| item.ends_with(".so"));
    let (status, reason) = match name {
        "Easy Anti-Cheat" | "BattlEye" if has_proton_assets => (
            "vendor_supported_on_proton_assets_present",
            "Proton/Linux anti-cheat module assets are present; MetalSharp still needs publisher/vendor-supported macOS/Wine enablement.",
        ),
        "Easy Anti-Cheat" | "BattlEye" => (
            "blocked_pending_vendor_support",
            "Protected title requires publisher/vendor opt-in and compatible Unix module assets; MetalSharp should not bypass or spoof support.",
        ),
        "nProtect GameGuard" | "Anti-Cheat Expert" => (
            "unsupported_kernel_driver",
            "Detected anti-cheat family normally depends on Windows kernel-driver behavior that Wine/macOS cannot honestly provide.",
        ),
        "EQU8" => (
            "unknown",
            "Detected anti-cheat requires title-specific validation before MetalSharp can classify it safely.",
        ),
        _ => (
            "user_mode_possible",
            "Detected anti-cheat appears user-mode or legacy; launch may be possible only in publisher-supported configurations.",
        ),
    };
    AntiCheatCompatibility {
        name: name.to_string(),
        status: status.to_string(),
        reason: reason.to_string(),
        evidence,
        allowed_actions: vec![
            "collect_logs".to_string(),
            "try_offline_mode_if_supported".to_string(),
            "request_publisher_vendor_support".to_string(),
        ],
        forbidden_actions: vec!["bypass".to_string(), "tamper".to_string(), "spoof_kernel_trust".to_string()],
    }
}

fn anti_cheat_evidence(game_dir: &Path, name: &str) -> Vec<String> {
    let mut evidence = Vec::new();
    for entry in WalkDir::new(game_dir).max_depth(5).into_iter().flatten() {
        if !entry.file_type().is_file() {
            continue;
        }
        let lower = entry.file_name().to_string_lossy().to_ascii_lowercase();
        let matches = match name {
            "Easy Anti-Cheat" => {
                lower.contains("easyanticheat") || lower == "eac.exe" || lower.contains("easyanticheat_eos")
            },
            "BattlEye" => lower.contains("battleye") || lower.contains("beclient"),
            "nProtect GameGuard" => lower.contains("gameguard") || lower.contains("nprotect"),
            "Anti-Cheat Expert" => lower.contains("anticheatexpert") || lower.contains("ace"),
            "EQU8" => lower.contains("equ8"),
            _ => false,
        };
        if matches {
            evidence.push(entry.path().to_string_lossy().to_string());
        }
    }
    evidence.sort();
    evidence
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn executable_scoring_rejects_launcher_when_real_game_exists() {
        let dir = test_dir("exe-score");
        std::fs::create_dir_all(&dir).expect("create test dir");
        std::fs::write(dir.join("DOOMEternal Launcher.exe"), b"not pe").expect("write launcher");
        std::fs::write(dir.join("DOOMEternalx64vk.exe"), b"not pe").expect("write game");

        let selected = resolve_game_exe(782330, &dir).expect("select exe");

        assert_eq!(selected.file_name().unwrap().to_string_lossy(), "DOOMEternalx64vk.exe");
        let _ = std::fs::remove_dir_all(dir);
    }

    #[test]
    fn launcher_names_are_classified_as_launchers() {
        assert!(is_likely_launcher_exe(Path::new("DOOM-Eternal Launcher.exe")));
        assert!(!is_likely_launcher_exe(Path::new("DOOMEternalx64vk.exe")));
    }

    #[test]
    fn preferred_exes_skip_crash_reporters_for_known_half_working_titles() {
        let village_dir = test_dir("preferred-village-exe");
        std::fs::create_dir_all(&village_dir).expect("create village dir");
        std::fs::write(village_dir.join("CrashReport.exe"), b"not pe").expect("write crash reporter");
        std::fs::write(village_dir.join("re8.exe"), b"not pe").expect("write village exe");

        let village = resolve_game_exe(1196590, &village_dir).expect("select village exe");
        assert_eq!(village.file_name().unwrap().to_string_lossy(), "re8.exe");
        let _ = std::fs::remove_dir_all(village_dir);

        let wukong_dir = test_dir("preferred-wukong-exe");
        let exe_dir = wukong_dir.join("b1").join("Binaries").join("Win64");
        let engine_dir = wukong_dir.join("Engine").join("Binaries").join("Win64");
        std::fs::create_dir_all(&exe_dir).expect("create wukong exe dir");
        std::fs::create_dir_all(&engine_dir).expect("create wukong engine dir");
        std::fs::write(engine_dir.join("CrashReportClient.exe"), b"not pe").expect("write crash reporter");
        std::fs::write(exe_dir.join("b1-Win64-Shipping.exe"), b"not pe").expect("write wukong exe");

        let wukong = resolve_game_exe(2358720, &wukong_dir).expect("select wukong exe");
        assert_eq!(wukong.file_name().unwrap().to_string_lossy(), "b1-Win64-Shipping.exe");
        let _ = std::fs::remove_dir_all(wukong_dir);
    }

    #[test]
    fn subnautica_m12_preserves_startup_movie_handoff() {
        let args = effective_launch_args(1962700, super::super::engine::get_pipeline(PipelineId::M12));

        assert!(!args.iter().any(|arg| arg.eq_ignore_ascii_case("-NoStartupMovies")));
        assert!(!args.iter().any(|arg| arg.eq_ignore_ascii_case("-NOSPLASH")));
    }

    #[test]
    fn subnautica_m12_keeps_startup_pso_cache_for_build_window() {
        let args = effective_launch_args(1962700, super::super::engine::get_pipeline(PipelineId::M12));

        assert!(!args.iter().any(|arg| arg.eq_ignore_ascii_case("-NoShaderPipelineCache")));
        assert!(!args.iter().any(|arg| arg.contains("r.ShaderPipelineCache.Enabled=0")));
        assert!(!args.iter().any(|arg| arg.contains("r.PSOPrecaching=0")));
        assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-dx12")));
        assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-d3d12")));
    }

    #[test]
    fn re4_m12_uses_native_launch_args() {
        let args = effective_launch_args(2050650, super::super::engine::get_pipeline(PipelineId::M12));

        assert!(args.is_empty());
    }

    #[test]
    fn dual_renderer_half_working_titles_get_dx11_args_on_m11() {
        for appid in [1623730, 2358720] {
            let args = effective_launch_args(appid, super::super::engine::get_pipeline(PipelineId::M11));

            assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-dx11")), "appid {appid}");
            assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-d3d11")), "appid {appid}");
        }
    }

    #[test]
    fn m12_half_working_titles_get_explicit_dx12_args() {
        for appid in [1196590, 1623730, 1928870, 2358720, 2456740] {
            let args = effective_launch_args(appid, super::super::engine::get_pipeline(PipelineId::M12));

            assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-dx12")), "appid {appid}");
            assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-d3d12")), "appid {appid}");
        }
    }

    #[test]
    fn source_style_titles_get_steam_secure_launch_args() {
        for appid in [440, 252490, 271590, 284160, 292030, 1172380, 3241660] {
            let args = effective_launch_args(appid, super::super::engine::get_pipeline(PipelineId::M11));

            assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-steam")), "appid {appid}");
            assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-secure")), "appid {appid}");
            assert_eq!(args.iter().filter(|arg| arg.eq_ignore_ascii_case("-steam")).count(), 1, "appid {appid}");
            assert_eq!(args.iter().filter(|arg| arg.eq_ignore_ascii_case("-secure")).count(), 1, "appid {appid}");
        }
    }

    #[test]
    fn party_animals_m11_uses_steam_without_secure() {
        let args = effective_launch_args(1260320, super::super::engine::get_pipeline(PipelineId::M11));

        assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-steam")));
        assert!(!args.iter().any(|arg| arg.eq_ignore_ascii_case("-secure")));
    }

    #[test]
    fn portal_2_m9_uses_source_defaults_without_secure() {
        let args = effective_launch_args(620, super::super::engine::get_pipeline(PipelineId::M9));

        assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-steam")));
        assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-dxlevel")));
        assert!(args.iter().any(|arg| arg == "90"));
        assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-novid")));
        assert!(!args.iter().any(|arg| arg.eq_ignore_ascii_case("-secure")));
    }

    #[test]
    fn garrys_mod_m9_uses_source_defaults_without_secure() {
        let args = effective_launch_args(4000, super::super::engine::get_pipeline(PipelineId::M9));

        assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-steam")));
        assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-dxlevel")));
        assert!(args.iter().any(|arg| arg == "90"));
        assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-novid")));
        assert!(!args.iter().any(|arg| arg.eq_ignore_ascii_case("-secure")));
    }

    #[test]
    fn d3dmetal_launches_skip_steam_secure_args() {
        for pipeline in [PipelineId::D3DMetal, PipelineId::M13] {
            for appid in [440, 620, 4000, 252490, 271590, 284160, 292030, 1172380, 1260320, 3241660] {
                let args = effective_launch_args(appid, super::super::engine::get_pipeline(pipeline));

                assert!(!uses_steam_launch_model(appid, pipeline), "appid {appid} pipeline {pipeline:?}");
                assert!(!uses_steam_secure_launch_model(appid, pipeline), "appid {appid} pipeline {pipeline:?}");
                assert!(
                    !args.iter().any(|arg| arg.eq_ignore_ascii_case("-steam")),
                    "appid {appid} pipeline {pipeline:?}"
                );
                assert!(
                    !args.iter().any(|arg| arg.eq_ignore_ascii_case("-secure")),
                    "appid {appid} pipeline {pipeline:?}"
                );
            }
        }
    }

    #[test]
    fn d3dmetal_launches_keep_game_specific_defaults() {
        for pipeline in [PipelineId::D3DMetal, PipelineId::M13] {
            let rust_args = effective_launch_args(252490, super::super::engine::get_pipeline(pipeline));
            assert!(rust_args.iter().any(|arg| arg.eq_ignore_ascii_case("-vulkan")), "pipeline {pipeline:?}");
            assert!(!rust_args.iter().any(|arg| arg.eq_ignore_ascii_case("-steam")), "pipeline {pipeline:?}");
            assert!(!rust_args.iter().any(|arg| arg.eq_ignore_ascii_case("-secure")), "pipeline {pipeline:?}");

            let rdr2_args = effective_launch_args(1174180, super::super::engine::get_pipeline(pipeline));
            assert!(rdr2_args.iter().any(|arg| arg.eq_ignore_ascii_case("-api")), "pipeline {pipeline:?}");
            assert!(rdr2_args.iter().any(|arg| arg.eq_ignore_ascii_case("Vulkan")), "pipeline {pipeline:?}");
            assert!(!rdr2_args.iter().any(|arg| arg.eq_ignore_ascii_case("-steam")), "pipeline {pipeline:?}");
            assert!(!rdr2_args.iter().any(|arg| arg.eq_ignore_ascii_case("-secure")), "pipeline {pipeline:?}");
        }
    }

    #[test]
    fn database_vulkan_titles_get_default_renderer_args() {
        for (appid, expected_args) in [
            (379720, vec!["-vulkan"]),
            (275850, vec!["-vulkan"]),
            (892970, vec!["-vulkan"]),
            (252490, vec!["-vulkan"]),
            (570, vec!["-vulkan"]),
            (548430, vec!["-vulkan"]),
            (526870, vec!["-vulkan"]),
            (1272080, vec!["-vulkan"]),
            (949230, vec!["-force-vulkan"]),
            (1174180, vec!["-api", "Vulkan"]),
        ] {
            let args = effective_launch_args(appid, super::super::engine::get_pipeline(PipelineId::M11));

            for expected in expected_args {
                assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case(expected)), "appid {appid} missing {expected}");
                assert_eq!(
                    args.iter().filter(|arg| arg.eq_ignore_ascii_case(expected)).count(),
                    1,
                    "appid {appid} duplicated {expected}"
                );
            }
        }
    }

    #[test]
    fn database_source_titles_get_default_dxlevel_args() {
        for appid in [400, 620] {
            let args = effective_launch_args(appid, super::super::engine::get_pipeline(PipelineId::M9));

            assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-dxlevel")), "appid {appid}");
            assert!(args.iter().any(|arg| arg == "90"), "appid {appid}");
            assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-novid")), "appid {appid}");
        }

        for appid in [240, 500, 550] {
            let args = effective_launch_args(appid, super::super::engine::get_pipeline(PipelineId::M9));

            assert!(args.iter().any(|arg| arg.eq_ignore_ascii_case("-dxlevel")), "appid {appid}");
            assert!(args.iter().any(|arg| arg == "90"), "appid {appid}");
            assert!(!args.iter().any(|arg| arg.eq_ignore_ascii_case("-novid")), "appid {appid}");
        }
    }

    #[test]
    fn database_pipeline_specific_renderer_args_stay_on_matching_routes() {
        let bioshock_m9 = effective_launch_args(7670, super::super::engine::get_pipeline(PipelineId::M9));
        assert!(bioshock_m9.iter().any(|arg| arg.eq_ignore_ascii_case("-dx9")));
        let bioshock_m10 = effective_launch_args(7670, super::super::engine::get_pipeline(PipelineId::M10));
        assert!(!bioshock_m10.iter().any(|arg| arg.eq_ignore_ascii_case("-dx9")));

        let gta_iv_m10 = effective_launch_args(12210, super::super::engine::get_pipeline(PipelineId::M10));
        assert!(gta_iv_m10.iter().any(|arg| arg.eq_ignore_ascii_case("-d3d10")));
        let gta_iv_m9 = effective_launch_args(12210, super::super::engine::get_pipeline(PipelineId::M9));
        assert!(!gta_iv_m9.iter().any(|arg| arg.eq_ignore_ascii_case("-d3d10")));

        let crysis_m10 = effective_launch_args(17300, super::super::engine::get_pipeline(PipelineId::M10));
        assert!(crysis_m10.iter().any(|arg| arg.eq_ignore_ascii_case("-dx10")));
        let crysis_m9 = effective_launch_args(17300, super::super::engine::get_pipeline(PipelineId::M9));
        assert!(!crysis_m9.iter().any(|arg| arg.eq_ignore_ascii_case("-dx10")));
    }

    #[test]
    fn dlls_are_deployed_next_to_selected_nested_exe() {
        let game_dir = test_dir("dll-dest");
        let exe_dir = game_dir.join("Engine").join("Binaries").join("Win64");
        let runtime = test_dir("runtime");
        std::fs::create_dir_all(&exe_dir).expect("create exe dir");
        let exe = exe_dir.join("Game-Win64-Shipping.exe");
        std::fs::write(&exe, b"not pe").expect("write exe");

        let dlls = selected_deploy_dlls_for_pipeline(
            &game_dir,
            Some(&exe),
            super::super::engine::get_pipeline(PipelineId::M11),
            &runtime,
        );

        assert!(dlls.iter().all(|dll| dll.dest_path.parent() == Some(exe_dir.as_path())));
        let _ = std::fs::remove_dir_all(game_dir);
        let _ = std::fs::remove_dir_all(runtime);
    }

    #[test]
    fn m12_deploys_dlls_to_unreal_engine_binary_dir_too() {
        let game_dir = test_dir("m12-ue-dll-dest");
        let exe_dir = game_dir.join("Subnautica2").join("Binaries").join("Win64");
        let engine_dir = game_dir.join("Engine").join("Binaries").join("Win64");
        let runtime = test_dir("runtime-m12-ue");
        std::fs::create_dir_all(&exe_dir).expect("create exe dir");
        std::fs::create_dir_all(&engine_dir).expect("create engine dir");
        let exe = exe_dir.join("Subnautica2-Win64-Shipping.exe");
        std::fs::write(&exe, b"not pe").expect("write exe");

        let dlls = selected_deploy_dlls_for_pipeline(
            &game_dir,
            Some(&exe),
            super::super::engine::get_pipeline(PipelineId::M12),
            &runtime,
        );

        let dxgi_targets = dlls
            .iter()
            .filter(|dll| dll.filename == "dxgi.dll")
            .map(|dll| dll.dest_path.parent().map(Path::to_path_buf))
            .collect::<Vec<_>>();
        assert!(dxgi_targets.contains(&Some(exe_dir)));
        assert!(dxgi_targets.contains(&Some(engine_dir)));
        assert!(dlls.iter().any(|dll| {
            dll.filename == "d3d11.dll" && dll.source_path.ends_with("lib/dxmt/x86_64-windows/d3d11.dll")
        }));
        let _ = std::fs::remove_dir_all(game_dir);
        let _ = std::fs::remove_dir_all(runtime);
    }

    #[test]
    fn m9_selects_i386_d3d9_for_32_bit_exes() {
        let game_dir = test_dir("m9-32");
        let runtime = test_dir("runtime-32");
        std::fs::create_dir_all(&game_dir).expect("create test game dir");
        let exe = game_dir.join("portal2.exe");
        write_test_pe(&exe, 0x014c, 0x10b);

        let selected = selected_deploy_dlls_for_pipeline(
            &game_dir,
            Some(&exe),
            super::super::engine::get_pipeline(PipelineId::M9),
            &runtime,
        );
        let sources: std::collections::HashSet<_> = selected.iter().map(|dll| dll.source_subpath.as_str()).collect();

        assert_eq!(sources, std::collections::HashSet::from(["lib/wine/i386-windows"]));
        assert_eq!(selected.len(), 1);
        let _ = std::fs::remove_dir_all(game_dir);
        let _ = std::fs::remove_dir_all(runtime);
    }

    #[test]
    fn m9_selects_x86_64_d3d9_for_64_bit_exes() {
        let game_dir = test_dir("m9-64");
        let runtime = test_dir("runtime-64");
        std::fs::create_dir_all(&game_dir).expect("create test game dir");
        let exe = game_dir.join("valheim.exe");
        write_test_pe(&exe, 0x8664, 0x20b);

        let selected = selected_deploy_dlls_for_pipeline(
            &game_dir,
            Some(&exe),
            super::super::engine::get_pipeline(PipelineId::M9),
            &runtime,
        );
        let sources: std::collections::HashSet<_> = selected.iter().map(|dll| dll.source_subpath.as_str()).collect();

        assert_eq!(sources, std::collections::HashSet::from(["lib/wine/x86_64-windows"]));
        assert_eq!(selected.len(), 1);
        let _ = std::fs::remove_dir_all(game_dir);
        let _ = std::fs::remove_dir_all(runtime);
    }

    #[test]
    fn doctor_blocks_missing_runtime_and_dll_sources() {
        let game_dir = test_dir("doctor-blocks");
        std::fs::create_dir_all(&game_dir).expect("create game dir");
        let exe = game_dir.join("Game.exe");
        std::fs::write(&exe, b"not pe").expect("write exe");

        let report = diagnose_recipe(LaunchRecipe {
            appid: 1,
            pipeline: PipelineId::M11,
            pipeline_name: "M11".into(),
            backend: "dxmt".into(),
            game_dir: Some(game_dir.clone()),
            exe_path: Some(exe),
            exe_name: Some("Game.exe".into()),
            launch_args: vec!["-dx11".into()],
            env: vec![],
            dlls: vec![RecipeDll {
                source_subpath: "lib/dxmt/x86_64-windows".into(),
                filename: "d3d11.dll".into(),
                source_path: game_dir.join("missing-d3d11.dll"),
                dest_path: game_dir.join("d3d11.dll"),
                source_present: false,
            }],
            runtime_assets: vec![RuntimeAsset {
                name: "wine".into(),
                path: game_dir.join("missing-wine"),
                required: true,
                present: false,
            }],
            anti_cheat: vec![],
            anti_cheat_status: vec![],
            warnings: vec![],
        });

        assert!(!report.ready);
        assert_eq!(report.blockers.len(), 2);
        assert!(report.blockers.iter().any(|blocker| blocker.contains("Missing required runtime asset")));
        assert!(report.blockers.iter().any(|blocker| blocker.contains("Missing DLL source")));
        assert!(report.checks.iter().any(|check| check.id == "runtime_assets" && !check.ok));
        assert!(report.checks.iter().any(|check| check.id == "dll_sources" && !check.ok));
        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn doctor_allows_steam_route_without_local_exe_resolution() {
        let report = diagnose_recipe(LaunchRecipe {
            appid: 1,
            pipeline: PipelineId::Steam,
            pipeline_name: "Steam".into(),
            backend: "wine-steam".into(),
            game_dir: None,
            exe_path: None,
            exe_name: None,
            launch_args: vec![],
            env: vec![],
            dlls: vec![],
            runtime_assets: vec![],
            anti_cheat: vec![],
            anti_cheat_status: vec![],
            warnings: vec![],
        });

        assert!(report.ready);
        assert!(report.blockers.is_empty());
        assert!(report.checks.iter().any(|check| check.id == "game_dir" && check.ok));
        assert!(report.checks.iter().any(|check| check.id == "exe" && check.ok));
    }

    #[test]
    fn doctor_blocks_32_bit_exe_on_64_bit_dxmt_route() {
        let game_dir = test_dir("doctor-32-on-m11");
        std::fs::create_dir_all(&game_dir).expect("create game dir");
        let exe = game_dir.join("LegacyGame.exe");
        write_test_pe(&exe, 0x014c, 0x10b);

        let report = diagnose_recipe(LaunchRecipe {
            appid: 1,
            pipeline: PipelineId::M11,
            pipeline_name: "M11".into(),
            backend: "dxmt".into(),
            game_dir: Some(game_dir.clone()),
            exe_path: Some(exe),
            exe_name: Some("LegacyGame.exe".into()),
            launch_args: vec![],
            env: vec![],
            dlls: vec![],
            runtime_assets: vec![],
            anti_cheat: vec![],
            anti_cheat_status: vec![],
            warnings: vec![],
        });

        assert!(!report.ready);
        assert!(report.blockers.iter().any(|blocker| blocker.contains("requires a 64-bit Windows executable")));
        assert!(report.checks.iter().any(|check| check.id == "exe_route" && !check.ok));
        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn doctor_blocks_64_bit_exe_on_m32_route() {
        let game_dir = test_dir("doctor-64-on-m32");
        std::fs::create_dir_all(&game_dir).expect("create game dir");
        let exe = game_dir.join("ModernGame.exe");
        write_test_pe(&exe, 0x8664, 0x20b);

        let report = diagnose_recipe(LaunchRecipe {
            appid: 1,
            pipeline: PipelineId::M32,
            pipeline_name: "M32".into(),
            backend: "wine32".into(),
            game_dir: Some(game_dir.clone()),
            exe_path: Some(exe),
            exe_name: Some("ModernGame.exe".into()),
            launch_args: vec![],
            env: vec![],
            dlls: vec![],
            runtime_assets: vec![],
            anti_cheat: vec![],
            anti_cheat_status: vec![],
            warnings: vec![],
        });

        assert!(!report.ready);
        assert!(report.blockers.iter().any(|blocker| blocker.contains("reserved for 32-bit Windows executables")));
        assert!(report.checks.iter().any(|check| check.id == "exe_route" && !check.ok));
        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn doctor_request_reports_recipe_build_failures_as_blockers() {
        let report = diagnose_launch_request(4_000_000_000, super::super::engine::get_pipeline(PipelineId::M11));

        assert!(!report.ready);
        assert!(report.summary.contains("Blocked"));
        assert!(report.blockers.iter().any(|blocker| blocker.contains("Recipe build did not complete")));
        assert!(report.checks.iter().any(|check| check.id == "exe" && !check.ok));
    }

    #[test]
    fn anticheat_classifier_blocks_eac_without_vendor_assets() {
        let game_dir = test_dir("eac-no-assets");
        let eac_dir = game_dir.join("EasyAntiCheat");
        std::fs::create_dir_all(&eac_dir).expect("create eac dir");
        std::fs::write(eac_dir.join("EasyAntiCheat_EOS_Setup.exe"), b"eac").expect("write eac marker");
        let detected = detect_anti_cheat(&game_dir);

        let report = classify_anti_cheat_components(&game_dir, &detected);

        assert_eq!(detected, vec!["Easy Anti-Cheat"]);
        assert_eq!(report[0].status, "blocked_pending_vendor_support");
        assert!(report[0].forbidden_actions.contains(&"bypass".to_string()));
        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn anticheat_classifier_marks_proton_assets_as_vendor_supported_evidence() {
        let game_dir = test_dir("eac-proton-assets");
        let eac_dir = game_dir.join("EasyAntiCheat");
        std::fs::create_dir_all(&eac_dir).expect("create eac dir");
        std::fs::write(eac_dir.join("easyanticheat_x64.so"), b"eac").expect("write eac proton marker");
        let detected = detect_anti_cheat(&game_dir);

        let report = classify_anti_cheat_components(&game_dir, &detected);

        assert_eq!(report[0].status, "vendor_supported_on_proton_assets_present");
        assert!(report[0].evidence.iter().any(|path| path.ends_with(".so")));
        let _ = std::fs::remove_dir_all(game_dir);
    }

    fn test_dir(name: &str) -> PathBuf {
        let mut dir = std::env::temp_dir();
        dir.push(format!("metalsharp-recipe-{}-{}-{}", name, std::process::id(), unique_suffix()));
        dir
    }

    fn unique_suffix() -> u128 {
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).expect("system time").as_nanos()
    }

    fn write_test_pe(path: &std::path::Path, machine: u16, optional_magic: u16) {
        let mut data = vec![0_u8; 0x200];
        data[0] = b'M';
        data[1] = b'Z';
        data[0x3c..0x40].copy_from_slice(&(0x80_u32).to_le_bytes());
        data[0x80..0x84].copy_from_slice(b"PE\0\0");
        data[0x84..0x86].copy_from_slice(&machine.to_le_bytes());
        data[0x86..0x88].copy_from_slice(&(0_u16).to_le_bytes());
        data[0x94..0x96].copy_from_slice(&(0xf0_u16).to_le_bytes());
        data[0x98..0x9a].copy_from_slice(&optional_magic.to_le_bytes());
        std::fs::write(path, data).expect("write test PE");
    }
}
