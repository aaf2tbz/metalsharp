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

#[derive(Debug, Clone)]
struct ExeCandidate {
    path: PathBuf,
    score: i32,
}

pub fn build_launch_recipe(appid: u32, node: &PipelineNode) -> Result<LaunchRecipe, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let game_dir = crate::setup::resolve_game_dir(appid);

    let exe_path = match node.id {
        PipelineId::M9
        | PipelineId::M10
        | PipelineId::M11
        | PipelineId::M12
        | PipelineId::M32
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
        launch_args: node.launch_args.iter().map(|arg| arg.to_string()).collect(),
        env: node
            .env_vars
            .iter()
            .map(|ev| RecipeEnv { key: ev.key.to_string(), value: ev.value.to_string() })
            .collect(),
        dlls,
        runtime_assets: runtime_assets_for_node(node, &ms_root),
        anti_cheat,
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
    let target_dir = exe_path.and_then(Path::parent).unwrap_or(game_dir);

    node.deploy_dlls
        .iter()
        .filter(|dll| node.id != PipelineId::M9 || dll.source_subpath == d3d9_subpath)
        .map(|dll| {
            let source_path = ms_root.join(dll.source_subpath).join(dll.filename);
            let dest_path = target_dir.join(dll.filename);
            RecipeDll {
                source_subpath: dll.source_subpath.to_string(),
                filename: dll.filename.to_string(),
                source_present: source_path.exists(),
                source_path,
                dest_path,
            }
        })
        .collect()
}

pub fn is_likely_launcher_exe(path: &Path) -> bool {
    let name = path.file_name().map(|n| n.to_string_lossy().to_lowercase()).unwrap_or_default();
    ["launcher", "bootstrap", "updater", "webhelper"].iter().any(|needle| name.contains(needle))
}

fn preferred_exe_names(appid: u32) -> &'static [&'static str] {
    match appid {
        379720 => &["DOOMx64vk.exe", "DOOMx64.exe"],
        782330 => &["DOOMEternalx64vk.exe", "DOOMEternalx64.exe"],
        105600 => &["TerrariaLauncher.exe", "Terraria.exe"],
        _ => &[],
    }
}

fn find_case_insensitive(dir: &Path, name: &str) -> Option<PathBuf> {
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
        && !lower.contains("crashhandler")
        && !lower.contains("server")
        && !lower.contains("steamwebhelper")
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

    fn test_dir(name: &str) -> PathBuf {
        let mut dir = std::env::temp_dir();
        dir.push(format!("metalsharp-recipe-{}-{}-{}", name, std::process::id(), unique_suffix()));
        dir
    }

    fn unique_suffix() -> u128 {
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).expect("system time").as_nanos()
    }
}
