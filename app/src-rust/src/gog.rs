use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::fs::{self, OpenOptions};
use std::io::Write;
#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;
use std::path::{Component, Path, PathBuf};
use std::process::{Command, Output, Stdio};
use std::thread;
use std::time::{SystemTime, UNIX_EPOCH};

const GOG_PREFIX_BOTTLE_ID: &str = "gog-prefix";
const GOG_AUTH_URL: &str = "https://auth.gog.com/auth?client_id=46899977096215655&redirect_uri=https%3A%2F%2Fembed.gog.com%2Fon_login_success%3Forigin%3Dclient&response_type=code&layout=galaxy";
const GOG_CLIENT_ID: &str = "46899977096215655";
const HEROIC_GOGDL_REPO_URL: &str = "https://github.com/Heroic-Games-Launcher/heroic-gogdl.git";

#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct GogLibraryCache {
    #[serde(default)]
    pub games: Vec<GogGame>,
    #[serde(default)]
    pub last_sync_at: Option<u64>,
}

#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct GogGame {
    pub product_id: String,
    pub title: String,
    #[serde(default = "default_platform")]
    pub platform: String,
    #[serde(default)]
    pub slug: Option<String>,
    #[serde(default)]
    pub image_url: Option<String>,
    #[serde(default)]
    pub icon_url: Option<String>,
    #[serde(default)]
    pub install_root: Option<String>,
    #[serde(default)]
    pub game_folder: Option<String>,
    #[serde(default)]
    pub primary_exe: Option<String>,
    #[serde(default)]
    pub primary_task_name: Option<String>,
    #[serde(default)]
    pub installed: bool,
    #[serde(default)]
    pub running: bool,
    #[serde(default)]
    pub status: String,
    #[serde(default)]
    pub download_size_bytes: Option<u64>,
    #[serde(default)]
    pub disk_size_bytes: Option<u64>,
    #[serde(default)]
    pub last_install_pid: Option<u32>,
    #[serde(default)]
    pub last_launch_pid: Option<u32>,
    #[serde(default)]
    pub last_log_path: Option<String>,
    #[serde(default)]
    pub last_launch_receipt_path: Option<String>,
    #[serde(default)]
    pub last_error: Option<String>,
}

fn default_platform() -> String {
    "windows".to_string()
}

fn now_secs() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|duration| duration.as_secs()).unwrap_or_default()
}

fn ms_home() -> PathBuf {
    crate::platform::metalsharp_home_dir()
}

fn gog_dir() -> PathBuf {
    ms_home().join("gog")
}

fn gogdl_config_dir() -> PathBuf {
    ms_home().join("gogdl")
}

fn gogdl_support_dir() -> PathBuf {
    gogdl_config_dir().join("gog-support")
}

fn gog_auth_config_path() -> PathBuf {
    ms_home().join("gog_store").join("auth.json")
}

fn library_cache_path() -> PathBuf {
    gog_dir().join("library.json")
}

fn default_install_root(product_id: &str) -> PathBuf {
    ms_home().join("gog-games").join(product_id)
}

fn gog_prefix() -> PathBuf {
    crate::bottles::bottle_dir(GOG_PREFIX_BOTTLE_ID).join("prefix")
}

fn wine_root() -> PathBuf {
    ms_home().join("runtime").join("wine")
}

fn wine_binary() -> PathBuf {
    crate::platform::runtime_wine_binary(&wine_root())
}

fn ensure_parent(path: &Path) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|error| format!("failed to create {}: {}", parent.display(), error))?;
    }
    Ok(())
}

fn load_cache() -> GogLibraryCache {
    fs::read_to_string(library_cache_path()).ok().and_then(|data| serde_json::from_str(&data).ok()).unwrap_or_default()
}

fn save_cache(cache: &GogLibraryCache) -> Result<(), String> {
    let path = library_cache_path();
    ensure_parent(&path)?;
    let data = serde_json::to_string_pretty(cache).map_err(|error| format!("failed to encode GOG cache: {}", error))?;
    fs::write(&path, data).map_err(|error| format!("failed to write {}: {}", path.display(), error))
}

fn file_nonempty(path: &Path) -> bool {
    path.is_file() && path.metadata().map(|meta| meta.len() > 16).unwrap_or(false)
}

fn gogdl_candidates() -> Vec<PathBuf> {
    gogdl_candidates_for(&ms_home())
}

fn gogdl_candidates_for(ms_home: &Path) -> Vec<PathBuf> {
    let mut candidates = Vec::new();
    if let Ok(explicit) = std::env::var("METALSHARP_GOGDL_BIN") {
        let explicit = explicit.trim();
        if !explicit.is_empty() {
            candidates.push(PathBuf::from(explicit));
        }
    }
    candidates.push(ms_home.join("tools").join("gogdl"));
    candidates.push(ms_home.join("tools").join("gogdl-venv").join("bin").join("gogdl"));
    candidates.push(ms_home.join("runtime").join("gogdl"));
    if let Ok(path_env) = std::env::var("PATH") {
        for path in std::env::split_paths(&path_env) {
            candidates.push(path.join("gogdl"));
        }
    }
    candidates
}

fn gogdl_binary() -> Option<PathBuf> {
    gogdl_candidates().into_iter().find(|path| path.is_file())
}

fn tools_dir() -> PathBuf {
    ms_home().join("tools")
}

fn gogdl_wrapper_path() -> PathBuf {
    tools_dir().join("gogdl")
}

fn gogdl_venv_dir() -> PathBuf {
    tools_dir().join("gogdl-venv")
}

fn gogdl_venv_bin() -> PathBuf {
    gogdl_venv_dir().join("bin").join("gogdl")
}

fn gogdl_source_dir() -> PathBuf {
    tools_dir().join("heroic-gogdl")
}

fn command_from_path(name: &str) -> Option<PathBuf> {
    let direct = PathBuf::from(name);
    if direct.is_absolute() && direct.is_file() {
        return Some(direct);
    }
    std::env::var_os("PATH").and_then(|paths| {
        std::env::split_paths(&paths).map(|path| path.join(name)).find(|candidate| candidate.is_file())
    })
}

fn python3_binary() -> Option<PathBuf> {
    ["/usr/bin/python3", "/opt/homebrew/bin/python3", "/usr/local/bin/python3"]
        .into_iter()
        .map(PathBuf::from)
        .find(|path| path.is_file())
        .or_else(|| command_from_path("python3"))
}

fn git_binary() -> Option<PathBuf> {
    ["/usr/bin/git", "/opt/homebrew/bin/git", "/usr/local/bin/git"]
        .into_iter()
        .map(PathBuf::from)
        .find(|path| path.is_file())
        .or_else(|| command_from_path("git"))
}

fn command_failure_text(output: &Output) -> String {
    let stderr = truncate_text(String::from_utf8_lossy(&output.stderr).trim());
    if !stderr.is_empty() {
        stderr
    } else {
        truncate_text(String::from_utf8_lossy(&output.stdout).trim())
    }
}

fn run_bootstrap_command(command: &mut Command, context: &str) -> Result<(), String> {
    let output = command.output().map_err(|error| format!("{} failed to start: {}", context, error))?;
    if output.status.success() {
        Ok(())
    } else {
        let details = command_failure_text(&output);
        if details.is_empty() {
            Err(format!("{} failed with {:?}", context, output.status.code()))
        } else {
            Err(format!("{} failed: {}", context, details))
        }
    }
}

fn shell_single_quote(path: &Path) -> String {
    format!("'{}'", path.to_string_lossy().replace('\'', "'\\''"))
}

fn write_gogdl_wrapper() -> Result<(), String> {
    let wrapper = gogdl_wrapper_path();
    ensure_parent(&wrapper)?;
    let target = gogdl_venv_bin();
    let script = format!("#!/bin/sh\nexec {} \"$@\"\n", shell_single_quote(&target));
    fs::write(&wrapper, script).map_err(|error| format!("failed to write {}: {}", wrapper.display(), error))?;
    #[cfg(unix)]
    {
        let mut permissions = fs::metadata(&wrapper)
            .map_err(|error| format!("failed to stat {}: {}", wrapper.display(), error))?
            .permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&wrapper, permissions)
            .map_err(|error| format!("failed to mark {} executable: {}", wrapper.display(), error))?;
    }
    Ok(())
}

fn ensure_gogdl_available() -> Result<(), String> {
    if gogdl_binary().is_some() {
        return Ok(());
    }
    if gogdl_venv_bin().is_file() {
        write_gogdl_wrapper()?;
        if gogdl_binary().is_some() {
            return Ok(());
        }
    }

    fs::create_dir_all(tools_dir()).map_err(|error| format!("failed to create GOG tools dir: {}", error))?;
    let python = python3_binary().ok_or_else(|| "python3 is required to prepare GOG support".to_string())?;
    let git = git_binary().ok_or_else(|| "git is required to prepare GOG support".to_string())?;
    let venv = gogdl_venv_dir();
    let venv_python = venv.join("bin").join("python");
    if !venv_python.is_file() {
        let mut command = Command::new(&python);
        command.arg("-m").arg("venv").arg(&venv);
        run_bootstrap_command(&mut command, "GOG support environment setup")?;
    }

    let source = gogdl_source_dir();
    if !source.join(".git").is_dir() {
        if source.exists() {
            fs::remove_dir_all(&source).map_err(|error| format!("failed to reset GOG support source: {}", error))?;
        }
        let mut command = Command::new(&git);
        command
            .arg("clone")
            .arg("--depth")
            .arg("1")
            .arg("--recurse-submodules")
            .arg(HEROIC_GOGDL_REPO_URL)
            .arg(&source);
        run_bootstrap_command(&mut command, "GOG support source setup")?;
    } else {
        let mut command = Command::new(&git);
        command.arg("-C").arg(&source).arg("submodule").arg("update").arg("--init").arg("--recursive");
        run_bootstrap_command(&mut command, "GOG support source refresh")?;
    }

    let mut command = Command::new(&venv_python);
    command.arg("-m").arg("pip").arg("install").arg("--upgrade").arg(&source).env("PIP_DISABLE_PIP_VERSION_CHECK", "1");
    run_bootstrap_command(&mut command, "GOG support install")?;
    write_gogdl_wrapper()?;

    let mut command = Command::new(gogdl_wrapper_path());
    command.arg("--version");
    run_bootstrap_command(&mut command, "GOG support verification")?;
    Ok(())
}

fn gogdl_command() -> Result<Command, String> {
    let binary =
        gogdl_binary().ok_or_else(|| "gogdl binary not found; install it or set METALSHARP_GOGDL_BIN".to_string())?;
    ensure_parent(&gog_auth_config_path())?;
    fs::create_dir_all(gogdl_config_dir()).map_err(|error| format!("failed to create GOGDL config: {}", error))?;
    fs::create_dir_all(gogdl_support_dir()).map_err(|error| format!("failed to create GOG support dir: {}", error))?;
    let mut command = Command::new(binary);
    command
        .arg("--auth-config-path")
        .arg(gog_auth_config_path())
        .env("GOGDL_CONFIG_PATH", gogdl_config_dir())
        .env("GOGDL_SUPPORT_PATH", gogdl_support_dir());
    Ok(command)
}

fn run_gogdl(args: &[String]) -> Result<Output, String> {
    let mut command = gogdl_command()?;
    command
        .args(args)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .output()
        .map_err(|error| format!("failed to run gogdl: {}", error))
}

fn truncate_text(text: &str) -> String {
    const LIMIT: usize = 8_192;
    if text.len() <= LIMIT {
        text.to_string()
    } else {
        format!("{}…", &text[..LIMIT])
    }
}

fn output_json(output: &Output) -> Option<Value> {
    let stdout = String::from_utf8_lossy(&output.stdout).trim().to_string();
    if stdout.is_empty() {
        None
    } else {
        serde_json::from_str(&stdout).ok()
    }
}

fn output_error(context: &str, output: &Output) -> Value {
    json!({
        "ok": false,
        "error": format!("{} failed", context),
        "status": output.status.code(),
        "stdout": truncate_text(String::from_utf8_lossy(&output.stdout).trim()),
        "stderr": truncate_text(String::from_utf8_lossy(&output.stderr).trim()),
        "statusInfo": status_value(),
    })
}

fn log_path(label: &str, product_id: &str) -> PathBuf {
    ms_home().join("logs").join("gog").join(format!("{}-{}-{}.log", label, product_id, now_secs()))
}

fn launch_receipt_path(product_id: &str) -> PathBuf {
    gog_dir().join("receipts").join(format!("{}-launch.json", product_id))
}

fn gog_launch_receipt_for(
    game: &GogGame,
    args: &[String],
    pid: u32,
    log_path: &Path,
    wine_path: &Path,
    prefix: &Path,
) -> Value {
    json!({
        "schema": "metalsharp.launch.receipt.v1",
        "preview": false,
        "source": "gog",
        "appId": game.product_id,
        "title": game.title,
        "route": "gogdl_wine",
        "runtimeContractId": "gogdl_wine",
        "pipeline": "gogdl_wine",
        "pipelineName": "GOGDL Wine",
        "backend": "gogdl",
        "prefix": prefix.to_string_lossy(),
        "wine": wine_path.to_string_lossy(),
        "gameDir": game.game_folder,
        "exePath": game.primary_exe,
        "taskName": game.primary_task_name,
        "platform": game.platform,
        "command": "gogdl",
        "args": args,
        "dllsStaged": [],
        "dylibsUsed": [],
        "envKeys": ["GOGDL_CONFIG_PATH", "GOGDL_SUPPORT_PATH"],
        "logPath": log_path.to_string_lossy(),
        "pid": pid,
        "warnings": [],
    })
}

fn persist_gog_launch_receipt(product_id: &str, receipt: &Value) -> Result<PathBuf, String> {
    let path = launch_receipt_path(product_id);
    ensure_parent(&path)?;
    let data = serde_json::to_string_pretty(receipt)
        .map_err(|error| format!("failed to encode GOG launch receipt: {}", error))?;
    fs::write(&path, data).map_err(|error| format!("failed to write {}: {}", path.display(), error))?;
    Ok(path)
}

fn spawn_gogdl_logged(label: &str, product_id: &str, args: &[String]) -> Result<(u32, PathBuf), String> {
    let path = log_path(label, product_id);
    ensure_parent(&path)?;
    let mut log = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&path)
        .map_err(|error| format!("failed to open GOG log: {}", error))?;
    let log_err = log.try_clone().map_err(|error| format!("failed to clone GOG log: {}", error))?;
    writeln!(&mut log, "gogdl {} started at {}", label, now_secs()).ok();
    writeln!(&mut log, "args={:?}", args).ok();

    let mut command = gogdl_command()?;
    command.args(args).stdout(Stdio::from(log)).stderr(Stdio::from(log_err));
    let mut child = command.spawn().map_err(|error| format!("failed to spawn gogdl: {}", error))?;
    let pid = child.id();
    let wait_path = path.clone();
    thread::spawn(move || {
        let status = child.wait();
        if let Ok(mut log) = OpenOptions::new().create(true).append(true).open(&wait_path) {
            match status {
                Ok(status) => {
                    let _ = writeln!(log, "gogdl exited with {:?}", status.code());
                },
                Err(error) => {
                    let _ = writeln!(log, "gogdl wait failed: {}", error);
                },
            }
        }
    });
    Ok((pid, path))
}

fn is_pid_alive(pid: u32) -> bool {
    crate::launch::is_process_active(pid as i32)
}

fn read_log_tail(path: &Path) -> String {
    fs::read_to_string(path).unwrap_or_default()
}

fn parse_progress(log: &str) -> Option<f64> {
    let mut percent = None;
    for line in log.lines() {
        if let Some(rest) = line.split("Progress:").nth(1) {
            if let Some(value) = rest.split_whitespace().next() {
                percent = value.parse::<f64>().ok();
            }
        }
    }
    percent
}

fn log_exit_code(log: &str) -> Option<i32> {
    for line in log.lines().rev() {
        if let Some(rest) = line.split("gogdl exited with Some(").nth(1) {
            if let Some(value) = rest.split(')').next() {
                return value.parse::<i32>().ok();
            }
        }
    }
    None
}

fn valid_token(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 180
        && value.chars().all(|ch| ch.is_ascii_alphanumeric() || matches!(ch, '_' | '-' | '.'))
}

fn body_string(body: &Value, key: &str) -> Option<String> {
    body.get(key).and_then(|value| value.as_str()).map(str::trim).filter(|value| !value.is_empty()).map(str::to_string)
}

fn product_id_from_body(body: &Value) -> Result<String, String> {
    let id = body_string(body, "productId")
        .or_else(|| body_string(body, "id"))
        .ok_or_else(|| "missing productId".to_string())?;
    if !valid_token(&id) {
        return Err("invalid productId".to_string());
    }
    Ok(id)
}

fn platform_from_body(body: &Value) -> Result<String, String> {
    let platform = body_string(body, "platform").unwrap_or_else(default_platform);
    match platform.as_str() {
        "windows" | "osx" | "linux" => Ok(platform),
        _ => Err("platform must be windows, osx, or linux".to_string()),
    }
}

fn normalize_image_url(value: Option<&str>) -> Option<String> {
    value.map(|url| if url.starts_with("//") { format!("https:{}", url) } else { url.to_string() })
}

fn curl_json(url: &str, token: Option<&str>) -> Result<Value, String> {
    let mut command = Command::new("/usr/bin/curl");
    command.arg("--fail").arg("--location").arg("--silent").arg("--show-error").arg(url);
    if let Some(token) = token {
        command.arg("--header").arg(format!("Authorization: Bearer {}", token));
    }
    let output = command.output().map_err(|error| format!("failed to run curl: {}", error))?;
    if !output.status.success() {
        return Err(format!("curl failed for {}: {}", url, String::from_utf8_lossy(&output.stderr).trim()));
    }
    serde_json::from_slice(&output.stdout).map_err(|error| format!("failed to parse JSON from {}: {}", url, error))
}

fn credentials() -> Result<Value, String> {
    let output = run_gogdl(&["auth".to_string()])?;
    if !output.status.success() {
        return Err(format!("gogdl auth failed: {}", String::from_utf8_lossy(&output.stderr).trim()));
    }
    output_json(&output).filter(|value| !value.is_null()).ok_or_else(|| "GOG is not authenticated".to_string())
}

fn access_token() -> Result<String, String> {
    credentials()?
        .get("access_token")
        .and_then(|value| value.as_str())
        .map(str::to_string)
        .ok_or_else(|| "GOG credentials missing access token".to_string())
}

fn status_value() -> Value {
    let binary = gogdl_binary();
    let authenticated = file_nonempty(&gog_auth_config_path());
    let prefix = gog_prefix();
    let status = if binary.is_none() {
        "missing_gogdl"
    } else if !prefix.join("drive_c").is_dir() {
        "needs_prefix"
    } else if !authenticated {
        "needs_login"
    } else {
        "ready"
    };
    json!({
        "id": "gog",
        "name": "GOG",
        "status": status,
        "ready": status == "ready",
        "authUrl": GOG_AUTH_URL,
        "authenticated": authenticated,
        "gogdlAvailable": binary.is_some(),
        "gogdlPath": binary.map(|path| path.to_string_lossy().to_string()),
        "authConfigPath": gog_auth_config_path().to_string_lossy().to_string(),
        "configPath": gogdl_config_dir().to_string_lossy().to_string(),
        "supportPath": gogdl_support_dir().to_string_lossy().to_string(),
        "bottleId": GOG_PREFIX_BOTTLE_ID,
        "winePrefix": prefix.to_string_lossy().to_string(),
        "prefixInitialized": prefix.join("drive_c").is_dir(),
        "winePath": wine_binary().to_string_lossy().to_string(),
    })
}

pub fn handle_status() -> Value {
    json!({"ok": true, "status": status_value()})
}

pub fn handle_doctor() -> Value {
    gog_doctor_for(&ms_home())
}

fn gog_doctor_for(ms_home: &Path) -> Value {
    let gog_prefix = ms_home.join("bottles").join(GOG_PREFIX_BOTTLE_ID).join("prefix");
    let steam_prefix = ms_home.join("prefix-steam");
    let gogdl_binary = gogdl_candidates_for(ms_home).into_iter().find(|path| file_nonempty(path));
    let auth = ms_home.join("gog_store").join("auth.json");
    let config = ms_home.join("gogdl");
    let support = config.join("gog-support");
    let cache = ms_home.join("gog").join("library.json");
    let receipts = ms_home.join("gog").join("receipts");
    let cache_json = read_json_file(&cache).unwrap_or_else(|| json!({"games": []}));
    let games = cache_json.get("games").and_then(|value| value.as_array()).cloned().unwrap_or_default();
    let installed_games =
        games.iter().filter(|game| game.get("installed").and_then(|value| value.as_bool()).unwrap_or(false)).count();
    let running_games =
        games.iter().filter(|game| game.get("running").and_then(|value| value.as_bool()).unwrap_or(false)).count();
    let receipt_paths = json_file_paths(&receipts);
    let dedicated_path_ok = gog_prefix.ends_with(Path::new("bottles/gog-prefix/prefix"));
    let lexically_uses_prefix_steam = gog_prefix == steam_prefix || gog_prefix.starts_with(&steam_prefix);
    let canonical_overlap = canonical_prefix_overlap(&steam_prefix, &gog_prefix);
    let contains_symlink = path_contains_symlink_below(ms_home, &gog_prefix);
    let uses_prefix_steam = lexically_uses_prefix_steam || canonical_overlap;
    let gogdl_available = gogdl_binary.is_some();
    let auth_present = file_nonempty(&auth);
    let prefix_initialized = gog_prefix.join("drive_c").is_dir();
    let mut blockers = Vec::new();
    if !gogdl_available {
        blockers.push("missing_gogdl");
    }
    if !auth_present {
        blockers.push("missing_auth");
    }
    if !prefix_initialized {
        blockers.push("prefix_not_initialized");
    }
    if !dedicated_path_ok || uses_prefix_steam || contains_symlink {
        blockers.push("prefix_policy");
    }
    let ok = blockers.is_empty();

    json!({
        "ok": ok,
        "schema": "metalsharp.gog.diagnostics.v1",
        "readOnly": true,
        "source": "gog",
        "route": "gogdl_wine",
        "runtimeContractId": "gogdl_wine",
        "blockers": blockers,
        "paths": {
            "metalsharpHome": ms_home.to_string_lossy(),
            "prefix": gog_prefix.to_string_lossy(),
            "steamPrefix": steam_prefix.to_string_lossy(),
            "auth": auth.to_string_lossy(),
            "config": config.to_string_lossy(),
            "support": support.to_string_lossy(),
            "cache": cache.to_string_lossy(),
            "receipts": receipts.to_string_lossy(),
        },
        "tools": {
            "gogdlAvailable": gogdl_available,
            "gogdlPath": gogdl_binary.as_ref().map(|path| path.to_string_lossy().to_string()),
        },
        "auth": {
            "present": auth_present,
            "path": auth.to_string_lossy(),
        },
        "prefix": {
            "present": gog_prefix.is_dir(),
            "initialized": prefix_initialized,
            "dedicatedPathOk": dedicated_path_ok,
            "containsSymlink": contains_symlink,
            "usesPrefixSteam": uses_prefix_steam,
            "lexicallyUsesPrefixSteam": lexically_uses_prefix_steam,
            "canonicalOverlapsPrefixSteam": canonical_overlap,
            "mustNotUsePrefixSteam": true,
        },
        "library": {
            "cachePresent": cache.is_file(),
            "gameCount": games.len(),
            "installedGameCount": installed_games,
            "runningGameCount": running_games,
        },
        "receipts": {
            "directoryPresent": receipts.is_dir(),
            "count": receipt_paths.len(),
            "latest": receipt_paths.first().map(|path| path.to_string_lossy().to_string()),
        },
        "limitations": ["Filesystem-only doctor; does not run gogdl, Wine, wineboot, or a game."],
    })
}

fn read_json_file(path: &Path) -> Option<Value> {
    fs::read_to_string(path).ok().and_then(|data| serde_json::from_str(&data).ok())
}

fn json_file_paths(dir: &Path) -> Vec<PathBuf> {
    let mut paths: Vec<PathBuf> = fs::read_dir(dir)
        .ok()
        .into_iter()
        .flat_map(|entries| entries.flatten())
        .map(|entry| entry.path())
        .filter(|path| path.extension().and_then(|ext| ext.to_str()) == Some("json"))
        .collect();
    paths.sort_by_key(|path| std::cmp::Reverse(path.metadata().and_then(|meta| meta.modified()).ok()));
    paths
}

fn canonical_prefix_overlap(steam_prefix: &Path, gog_prefix: &Path) -> bool {
    match (steam_prefix.canonicalize(), gog_prefix.canonicalize()) {
        (Ok(steam), Ok(gog)) => gog == steam || gog.starts_with(steam),
        _ => false,
    }
}

fn path_contains_symlink_below(root: &Path, path: &Path) -> bool {
    let Ok(relative) = path.strip_prefix(root) else {
        return false;
    };
    let mut current = root.to_path_buf();
    for component in relative.components() {
        current.push(component.as_os_str());
        if matches!(component, Component::RootDir | Component::Prefix(_)) {
            continue;
        }
        if std::fs::symlink_metadata(&current).map(|meta| meta.file_type().is_symlink()).unwrap_or(false) {
            return true;
        }
    }
    false
}

pub fn handle_initialize_prefix() -> Value {
    match initialize_prefix() {
        Ok(()) => json!({"ok": true, "status": status_value()}),
        Err(error) => json!({"ok": false, "error": error, "status": status_value()}),
    }
}

fn initialize_prefix() -> Result<(), String> {
    ensure_gogdl_available()?;
    let prefix = gog_prefix();
    fs::create_dir_all(&prefix).map_err(|error| format!("failed to create GOG prefix: {}", error))?;
    if prefix.join("drive_c").is_dir() {
        let _ = crate::prefix_metadata::record_wineboot_decision(
            &prefix,
            "gog",
            "wineboot -u",
            &["metalsharp-wine", "wineboot", "-u"],
            "skipped",
            "GOG prefix already initialized",
            None,
        );
        return Ok(());
    }
    let wine = wine_binary();
    if !wine.is_file() {
        let error = format!("MetalSharp Wine not found: {}", wine.display());
        let _ = crate::prefix_metadata::record_wineboot_decision(
            &prefix,
            "gog",
            "wineboot -u",
            &["metalsharp-wine", "wineboot", "-u"],
            "blocked",
            &error,
            None,
        );
        return Err(error);
    }
    let mut command = Command::new(&wine);
    command
        .arg("wineboot")
        .arg("-u")
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEMSYNC", "1")
        .env("WINEDEBUG", "-all")
        .env("MS_FWD_COMPAT_GL_CTX", "1")
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    crate::platform::set_runtime_library_env(&mut command, &wine_root());
    let status = match command.status() {
        Ok(status) => status,
        Err(error) => {
            let detail = format!("failed to initialize GOG prefix: {}", error);
            let _ = crate::prefix_metadata::record_wineboot_decision(
                &prefix,
                "gog",
                "wineboot -u",
                &["metalsharp-wine", "wineboot", "-u"],
                "spawn_failed",
                &detail,
                None,
            );
            return Err(detail);
        },
    };
    if status.success() {
        let _ = crate::prefix_metadata::record_wineboot_decision(
            &prefix,
            "gog",
            "wineboot -u",
            &["metalsharp-wine", "wineboot", "-u"],
            "success",
            "GOG prefix initialized",
            status.code(),
        );
        Ok(())
    } else {
        let detail = format!("wineboot failed with {:?}", status.code());
        let _ = crate::prefix_metadata::record_wineboot_decision(
            &prefix,
            "gog",
            "wineboot -u",
            &["metalsharp-wine", "wineboot", "-u"],
            "failed",
            &detail,
            status.code(),
        );
        Err(detail)
    }
}

pub fn handle_auth_code(body: &Value) -> Value {
    let Some(code) = body_string(body, "code") else {
        return json!({"ok": false, "error": "missing authorization code", "status": status_value()});
    };
    let output = match run_gogdl(&["auth".to_string(), "--code".to_string(), code]) {
        Ok(output) => output,
        Err(error) => return json!({"ok": false, "error": error, "status": status_value()}),
    };
    if !output.status.success() {
        return output_error("gogdl auth", &output);
    }
    let parsed = output_json(&output);
    if parsed.as_ref().and_then(|value| value.get("error")).and_then(|value| value.as_bool()).unwrap_or(false) {
        return json!({"ok": false, "error": "GOG rejected the authorization code; try logging in again", "status": status_value()});
    }
    if !file_nonempty(&gog_auth_config_path()) {
        return json!({"ok": false, "error": "gogdl auth did not write auth.json", "status": status_value()});
    }
    json!({"ok": true, "authenticated": true, "status": status_value()})
}

pub fn handle_logout() -> Value {
    let _ = fs::remove_file(gog_auth_config_path());
    json!({"ok": true, "status": status_value()})
}

fn gogdl_info(product_id: &str, platform: &str) -> Result<Value, String> {
    let args = vec![
        "info".to_string(),
        product_id.to_string(),
        "--platform".to_string(),
        platform.to_string(),
        "--with-dlcs".to_string(),
    ];
    let output = run_gogdl(&args)?;
    if output.status.success() {
        output_json(&output).ok_or_else(|| "gogdl info returned no JSON".to_string())
    } else {
        Err(format!("gogdl info failed: {}", String::from_utf8_lossy(&output.stderr).trim()))
    }
}

fn product_metadata(product_id: &str) -> Value {
    curl_json(&format!("https://api.gog.com/products/{}", product_id), None).unwrap_or_else(|_| json!({}))
}

fn update_game_from_metadata(mut game: GogGame) -> GogGame {
    let meta = product_metadata(&game.product_id);
    if let Some(title) = meta.get("title").and_then(|value| value.as_str()) {
        game.title = title.to_string();
    }
    game.slug = meta.get("slug").and_then(|value| value.as_str()).map(str::to_string).or(game.slug);
    let images = meta.get("images");
    game.image_url =
        normalize_image_url(images.and_then(|value| value.get("background")).and_then(|value| value.as_str()))
            .or(game.image_url);
    game.icon_url = normalize_image_url(images.and_then(|value| value.get("icon")).and_then(|value| value.as_str()))
        .or(game.icon_url);

    if let Ok(info) = gogdl_info(&game.product_id, &game.platform) {
        if let Some(size) = info.get("size").and_then(|value| value.get("en-US")) {
            game.download_size_bytes =
                size.get("download_size").and_then(|value| value.as_u64()).or(game.download_size_bytes);
            game.disk_size_bytes = size.get("disk_size").and_then(|value| value.as_u64()).or(game.disk_size_bytes);
        }
        if game.title.is_empty() {
            if let Some(folder) = info.get("folder_name").and_then(|value| value.as_str()) {
                game.title = folder.to_string();
            }
        }
    }

    if game.title.is_empty() {
        game.title = format!("GOG {}", game.product_id);
    }
    refresh_installed_state(game)
}

fn find_game_folder(root: &Path, product_id: &str) -> Option<PathBuf> {
    let direct = root.join(format!("goggame-{}.info", product_id));
    if direct.is_file() {
        return Some(root.to_path_buf());
    }
    let entries = fs::read_dir(root).ok()?;
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() && path.join(format!("goggame-{}.info", product_id)).is_file() {
            return Some(path);
        }
    }
    None
}

fn gog_support_product_dir(product_id: &str) -> PathBuf {
    gogdl_support_dir().join(product_id)
}

fn gog_manifest_product_dir(product_id: &str) -> PathBuf {
    gogdl_config_dir().join("heroic_gogdl").join("manifests").join(product_id)
}

fn remove_gog_product_state(product_id: &str) {
    for path in [gog_support_product_dir(product_id), gog_manifest_product_dir(product_id)] {
        if path.is_dir() {
            let _ = fs::remove_dir_all(path);
        } else if path.exists() {
            let _ = fs::remove_file(path);
        }
    }
}

fn uninstall_game_target(game: &GogGame) -> Option<PathBuf> {
    let product_id = &game.product_id;
    if let Some(folder) = game.game_folder.as_ref().map(PathBuf::from) {
        if folder.join(format!("goggame-{}.info", product_id)).is_file() {
            return Some(folder);
        }
    }
    if let Some(root) = game.install_root.as_ref().map(PathBuf::from) {
        if let Some(folder) = find_game_folder(&root, product_id) {
            return Some(folder);
        }
    }
    None
}

fn apply_local_info(mut game: GogGame, folder: &Path) -> GogGame {
    let info_path = folder.join(format!("goggame-{}.info", game.product_id));
    let Some(info) = fs::read_to_string(info_path).ok().and_then(|data| serde_json::from_str::<Value>(&data).ok())
    else {
        return game;
    };
    if let Some(title) = info.get("name").and_then(|value| value.as_str()) {
        game.title = title.to_string();
    }
    let primary = info.get("playTasks").and_then(|value| value.as_array()).and_then(|tasks| {
        tasks
            .iter()
            .find(|task| task.get("isPrimary").and_then(|value| value.as_bool()).unwrap_or(false))
            .or_else(|| tasks.first())
    });
    if let Some(task) = primary {
        game.primary_task_name =
            task.get("name").and_then(|value| value.as_str()).map(str::to_string).or(game.primary_task_name);
        game.primary_exe = task.get("path").and_then(|value| value.as_str()).map(str::to_string).or(game.primary_exe);
    }
    game
}

fn refresh_installed_state(mut game: GogGame) -> GogGame {
    let root = game.install_root.as_ref().map(PathBuf::from).unwrap_or_else(|| default_install_root(&game.product_id));
    if let Some(folder) = find_game_folder(&root, &game.product_id) {
        game = apply_local_info(game, &folder);
        game.install_root = Some(root.to_string_lossy().to_string());
        game.game_folder = Some(folder.to_string_lossy().to_string());
        game.installed = true;
        if game.status.is_empty() || game.status == "not_installed" || game.status == "downloading" {
            game.status = "installed".to_string();
        }
    } else if game.status.is_empty() {
        game.status = "not_installed".to_string();
    }
    if let Some(pid) = game.last_launch_pid {
        game.running = is_pid_alive(pid);
        if game.running {
            game.status = "running".to_string();
        } else if game.installed && game.status == "running" {
            game.status = "installed".to_string();
        }
    }
    game
}

pub fn handle_sync() -> Value {
    let token = match access_token() {
        Ok(token) => token,
        Err(error) => return json!({"ok": false, "error": error, "status": status_value()}),
    };
    let owned = match curl_json("https://embed.gog.com/user/data/games", Some(&token)) {
        Ok(value) => value.get("owned").and_then(|value| value.as_array()).cloned().unwrap_or_default(),
        Err(error) => return json!({"ok": false, "error": error, "status": status_value()}),
    };
    let existing = load_cache();
    let mut games = Vec::new();
    for id_value in owned {
        let product_id = id_value.as_u64().map(|id| id.to_string()).or_else(|| id_value.as_str().map(str::to_string));
        let Some(product_id) = product_id else {
            continue;
        };
        let previous = existing.games.iter().find(|game| game.product_id == product_id).cloned();
        let game = previous.unwrap_or_else(|| GogGame {
            product_id: product_id.clone(),
            title: format!("GOG {}", product_id),
            platform: default_platform(),
            status: "not_installed".to_string(),
            ..Default::default()
        });
        games.push(update_game_from_metadata(game));
    }
    let cache = GogLibraryCache { games, last_sync_at: Some(now_secs()) };
    if let Err(error) = save_cache(&cache) {
        return json!({"ok": false, "error": error, "status": status_value()});
    }
    json!({"ok": true, "games": cache.games, "status": status_value(), "lastSyncAt": cache.last_sync_at})
}

pub fn handle_games() -> Value {
    let mut cache = load_cache();
    cache.games = cache.games.into_iter().map(refresh_installed_state).collect();
    let _ = save_cache(&cache);
    json!({"ok": true, "games": cache.games, "status": status_value(), "lastSyncAt": cache.last_sync_at})
}

fn update_cached_game(product_id: &str, update: impl FnOnce(GogGame) -> GogGame) -> Result<GogGame, String> {
    let mut cache = load_cache();
    let pos = cache.games.iter().position(|game| game.product_id == product_id);
    let base = pos.and_then(|idx| cache.games.get(idx).cloned()).unwrap_or_else(|| GogGame {
        product_id: product_id.to_string(),
        title: format!("GOG {}", product_id),
        platform: default_platform(),
        status: "not_installed".to_string(),
        ..Default::default()
    });
    let updated = update(base);
    if let Some(pos) = pos {
        cache.games[pos] = updated.clone();
    } else {
        cache.games.push(updated.clone());
    }
    save_cache(&cache)?;
    Ok(updated)
}

pub fn handle_install(body: &Value) -> Value {
    let product_id = match product_id_from_body(body) {
        Ok(id) => id,
        Err(error) => return json!({"ok": false, "error": error}),
    };
    let platform = match platform_from_body(body) {
        Ok(platform) => platform,
        Err(error) => return json!({"ok": false, "error": error}),
    };
    let install_root =
        body_string(body, "installPath").map(PathBuf::from).unwrap_or_else(|| default_install_root(&product_id));
    let support = gog_support_product_dir(&product_id);
    if find_game_folder(&install_root, &product_id).is_none() {
        remove_gog_product_state(&product_id);
    }
    if let Err(error) =
        fs::create_dir_all(&install_root).map_err(|error| format!("failed to create install path: {}", error))
    {
        return json!({"ok": false, "error": error});
    }
    let _ = fs::create_dir_all(&support);
    let mut args = vec![
        "download".to_string(),
        product_id.clone(),
        "--platform".to_string(),
        platform.clone(),
        "--path".to_string(),
        install_root.to_string_lossy().to_string(),
        "--support".to_string(),
        support.to_string_lossy().to_string(),
        "--with-dlcs".to_string(),
    ];
    if let Some(language) = body_string(body, "language") {
        args.extend(["--lang".to_string(), language]);
    }
    match spawn_gogdl_logged("download", &product_id, &args) {
        Ok((pid, log)) => {
            let game = update_cached_game(&product_id, |mut game| {
                game.platform = platform;
                game.install_root = Some(install_root.to_string_lossy().to_string());
                game.status = "downloading".to_string();
                game.installed = false;
                game.running = false;
                game.game_folder = None;
                game.primary_exe = None;
                game.primary_task_name = None;
                game.last_install_pid = Some(pid);
                game.last_log_path = Some(log.to_string_lossy().to_string());
                game.last_error = None;
                game
            });
            match game {
                Ok(game) => json!({"ok": true, "pid": pid, "logPath": log.to_string_lossy().to_string(), "game": game}),
                Err(error) => json!({"ok": false, "error": error}),
            }
        },
        Err(error) => json!({"ok": false, "error": error}),
    }
}

fn import_game_folder(product_id: &str, path: &Path) -> Result<GogGame, String> {
    let folder = if path.join(format!("goggame-{}.info", product_id)).is_file() {
        path.to_path_buf()
    } else {
        find_game_folder(path, product_id)
            .ok_or_else(|| format!("goggame-{}.info not found under {}", product_id, path.display()))?
    };
    let output = run_gogdl(&["import".to_string(), folder.to_string_lossy().to_string()])?;
    if !output.status.success() {
        return Err(format!("gogdl import failed: {}", String::from_utf8_lossy(&output.stderr).trim()));
    }
    let imported = output_json(&output).unwrap_or_else(|| json!({}));
    update_cached_game(product_id, |mut game| {
        if let Some(title) = imported.get("title").and_then(|value| value.as_str()) {
            game.title = title.to_string();
        }
        game.primary_task_name = imported
            .get("tasks")
            .and_then(|value| value.as_array())
            .and_then(|tasks| {
                tasks
                    .iter()
                    .find(|task| task.get("isPrimary").and_then(|value| value.as_bool()).unwrap_or(false))
                    .or_else(|| tasks.first())
            })
            .and_then(|task| task.get("name").and_then(|value| value.as_str()).map(str::to_string));
        game.primary_exe = imported
            .get("tasks")
            .and_then(|value| value.as_array())
            .and_then(|tasks| {
                tasks
                    .iter()
                    .find(|task| task.get("isPrimary").and_then(|value| value.as_bool()).unwrap_or(false))
                    .or_else(|| tasks.first())
            })
            .and_then(|task| task.get("path").and_then(|value| value.as_str()).map(str::to_string));
        game.game_folder = Some(folder.to_string_lossy().to_string());
        game.install_root = Some(path.to_string_lossy().to_string());
        game.installed = true;
        game.running = false;
        game.status = "installed".to_string();
        game.last_error = None;
        game
    })
}

pub fn handle_import(body: &Value) -> Value {
    let product_id = match product_id_from_body(body) {
        Ok(id) => id,
        Err(error) => return json!({"ok": false, "error": error}),
    };
    let root = body_string(body, "installPath").map(PathBuf::from).unwrap_or_else(|| default_install_root(&product_id));
    match import_game_folder(&product_id, &root) {
        Ok(game) => json!({"ok": true, "game": game}),
        Err(error) => json!({"ok": false, "error": error}),
    }
}

pub fn handle_progress(body: &Value) -> Value {
    let product_id = match product_id_from_body(body) {
        Ok(id) => id,
        Err(error) => return json!({"ok": false, "error": error}),
    };
    let mut game = load_cache().games.into_iter().find(|game| game.product_id == product_id).unwrap_or_default();
    if game.product_id.is_empty() {
        return json!({"ok": false, "error": "game not found"});
    }
    let log_path = game.last_log_path.clone().map(PathBuf::from);
    let log = log_path.as_deref().map(read_log_tail).unwrap_or_default();
    let percent = parse_progress(&log).unwrap_or(if game.installed { 100.0 } else { 0.0 });
    let exit_code = log_exit_code(&log);
    let active = game.last_install_pid.map(is_pid_alive).unwrap_or(false);
    if game.status == "downloading" && exit_code == Some(0) {
        let root = game.install_root.clone().map(PathBuf::from).unwrap_or_else(|| default_install_root(&product_id));
        match import_game_folder(&product_id, &root) {
            Ok(imported) => {
                game = imported;
            },
            Err(error) => {
                remove_gog_product_state(&product_id);
                let stale_manifest_noop = log.contains("Nothing to do");
                if let Ok(updated) = update_cached_game(&product_id, |mut game| {
                    game.status = if stale_manifest_noop { "not_installed" } else { "install_failed" }.to_string();
                    game.last_error = if stale_manifest_noop {
                        None
                    } else {
                        Some(format!("download finished but import failed: {}", error))
                    };
                    game.last_install_pid = None;
                    game.last_log_path = None;
                    game.primary_exe = None;
                    game.primary_task_name = None;
                    game.game_folder = None;
                    game.installed = false;
                    game
                }) {
                    game = updated;
                }
            },
        }
    } else if game.status == "downloading" && exit_code.is_some() && exit_code != Some(0) {
        remove_gog_product_state(&product_id);
        if let Ok(updated) = update_cached_game(&product_id, |mut game| {
            game.status = "install_failed".to_string();
            game.last_error = Some(format!("gogdl exited with {:?}", exit_code));
            game.last_install_pid = None;
            game.last_log_path = None;
            game.primary_exe = None;
            game.primary_task_name = None;
            game.game_folder = None;
            game.installed = false;
            game
        }) {
            game = updated;
        }
    }
    json!({
        "ok": true,
        "productId": product_id,
        "percent": percent,
        "active": active,
        "exitCode": exit_code,
        "logPath": log_path.map(|path| path.to_string_lossy().to_string()),
        "game": game,
    })
}

pub fn handle_play(body: &Value) -> Value {
    let product_id = match product_id_from_body(body) {
        Ok(id) => id,
        Err(error) => return json!({"ok": false, "error": error}),
    };
    if let Err(error) = initialize_prefix() {
        return json!({"ok": false, "error": error});
    }
    let cache = load_cache();
    let Some(game) =
        cache.games.iter().find(|game| game.product_id == product_id).cloned().map(refresh_installed_state)
    else {
        return json!({"ok": false, "error": "game not found"});
    };
    let Some(folder) = game.game_folder.clone() else {
        return json!({"ok": false, "error": "game is not installed or imported"});
    };
    let args = vec![
        "launch".to_string(),
        folder.clone(),
        product_id.clone(),
        "--platform".to_string(),
        game.platform.clone(),
        "--wine".to_string(),
        wine_binary().to_string_lossy().to_string(),
        "--wine-prefix".to_string(),
        gog_prefix().to_string_lossy().to_string(),
    ];
    match spawn_gogdl_logged("launch", &product_id, &args) {
        Ok((pid, log)) => {
            let prefix = gog_prefix();
            let wine = wine_binary();
            let receipt = gog_launch_receipt_for(&game, &args, pid, &log, &wine, &prefix);
            let receipt_path = persist_gog_launch_receipt(&product_id, &receipt).ok();
            match update_cached_game(&product_id, |mut game| {
                game.last_launch_pid = Some(pid);
                game.last_log_path = Some(log.to_string_lossy().to_string());
                game.last_launch_receipt_path = receipt_path.as_ref().map(|path| path.to_string_lossy().to_string());
                game.running = true;
                game.status = "running".to_string();
                game.last_error = None;
                game
            }) {
                Ok(game) => {
                    json!({
                        "ok": true,
                        "pid": pid,
                        "logPath": log.to_string_lossy().to_string(),
                        "launchReceiptPath": receipt_path.map(|path| path.to_string_lossy().to_string()),
                        "launchReceipt": receipt,
                        "winePrefix": prefix.to_string_lossy().to_string(),
                        "game": game
                    })
                },
                Err(error) => json!({"ok": false, "error": error}),
            }
        },
        Err(error) => json!({"ok": false, "error": error}),
    }
}

fn kill_pid(pid: u32) {
    let _ = Command::new("/bin/kill").arg("-TERM").arg(pid.to_string()).status();
}

fn kill_scoped_processes(game: &GogGame) -> Vec<u32> {
    let prefix = gog_prefix().to_string_lossy().to_string();
    let folder = game.game_folder.clone().unwrap_or_default();
    let output = Command::new("/bin/ps").arg("-axo").arg("pid=,command=").output();
    let mut killed = Vec::new();
    if let Ok(output) = output {
        for line in String::from_utf8_lossy(&output.stdout).lines() {
            let trimmed = line.trim();
            let Some((pid_text, command)) = trimmed.split_once(' ') else {
                continue;
            };
            let Ok(pid) = pid_text.trim().parse::<u32>() else {
                continue;
            };
            if (!folder.is_empty() && command.contains(&folder)) || command.contains(&prefix) {
                kill_pid(pid);
                killed.push(pid);
            }
        }
    }
    let wineserver = wine_root().join("bin").join("wineserver");
    if wineserver.is_file() {
        let _ = Command::new(wineserver).env("WINEPREFIX", gog_prefix()).arg("-k").status();
    }
    killed
}

pub fn handle_stop(body: &Value) -> Value {
    let product_id = match product_id_from_body(body) {
        Ok(id) => id,
        Err(error) => return json!({"ok": false, "error": error}),
    };
    let cache = load_cache();
    let Some(game) = cache.games.iter().find(|game| game.product_id == product_id).cloned() else {
        return json!({"ok": false, "error": "game not found"});
    };
    if let Some(pid) = game.last_launch_pid {
        kill_pid(pid);
    }
    let killed = kill_scoped_processes(&game);
    match update_cached_game(&product_id, |mut game| {
        game.running = false;
        if game.installed {
            game.status = "installed".to_string();
        }
        game.last_launch_pid = None;
        game
    }) {
        Ok(game) => json!({"ok": true, "killedPids": killed, "game": game}),
        Err(error) => json!({"ok": false, "error": error}),
    }
}

pub fn handle_uninstall(body: &Value) -> Value {
    let product_id = match product_id_from_body(body) {
        Ok(id) => id,
        Err(error) => return json!({"ok": false, "error": error}),
    };
    let cache = load_cache();
    let Some(game) = cache.games.iter().find(|game| game.product_id == product_id).cloned() else {
        return json!({"ok": false, "error": "game not found"});
    };
    let _ = kill_scoped_processes(&game);
    let target = uninstall_game_target(&game);
    if let Some(target) = &target {
        if target.exists() {
            if let Err(error) = fs::remove_dir_all(target) {
                return json!({"ok": false, "error": format!("failed to remove {}: {}", target.display(), error)});
            }
        }
    }
    remove_gog_product_state(&product_id);
    match update_cached_game(&product_id, |mut game| {
        game.installed = false;
        game.running = false;
        game.status = "not_installed".to_string();
        game.install_root = None;
        game.game_folder = None;
        game.primary_exe = None;
        game.primary_task_name = None;
        game.last_install_pid = None;
        game.last_launch_pid = None;
        game.last_log_path = None;
        game.last_launch_receipt_path = None;
        game.last_error = None;
        game
    }) {
        Ok(game) => {
            json!({"ok": true, "removedPath": target.map(|path| path.to_string_lossy().to_string()), "game": game})
        },
        Err(error) => json!({"ok": false, "error": error}),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn valid_token_rejects_path_like_values() {
        assert!(valid_token("1876546888"));
        assert!(!valid_token("../bad"));
        assert!(!valid_token("bad/path"));
    }

    #[test]
    fn progress_parser_uses_last_progress_line() {
        let log = "Progress: 12.50 1/2\nother\n[PROGRESS] INFO: = Progress: 100.00 2/2";
        assert_eq!(parse_progress(log), Some(100.0));
    }

    #[test]
    fn exit_code_parser_finds_success() {
        assert_eq!(log_exit_code("abc\ngogdl exited with Some(0)\n"), Some(0));
    }

    fn temp_ms_home(name: &str) -> PathBuf {
        let root = std::env::temp_dir().join(format!("metalsharp-gog-test-{}-{}", name, std::process::id()));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).expect("create temp ms home");
        root
    }

    #[test]
    fn gog_doctor_reports_ready_dedicated_prefix_without_spawning() {
        let ms_home = temp_ms_home("doctor-ready");
        let gogdl = ms_home.join("tools").join("gogdl");
        let auth = ms_home.join("gog_store").join("auth.json");
        let prefix = ms_home.join("bottles").join("gog-prefix").join("prefix").join("drive_c");
        let cache = ms_home.join("gog").join("library.json");
        let receipt = ms_home.join("gog").join("receipts").join("1876546888-launch.json");
        fs::create_dir_all(gogdl.parent().unwrap()).expect("create tools");
        fs::create_dir_all(auth.parent().unwrap()).expect("create auth parent");
        fs::create_dir_all(&prefix).expect("create prefix");
        fs::create_dir_all(cache.parent().unwrap()).expect("create cache parent");
        fs::create_dir_all(receipt.parent().unwrap()).expect("create receipts");
        fs::write(&gogdl, b"#!/bin/sh\necho gogdl\n").expect("write gogdl");
        fs::write(&auth, br#"{"access_token":"token"}"#).expect("write auth");
        fs::write(&cache, br#"{"games":[{"productId":"1876546888","installed":true,"running":false}]}"#)
            .expect("write cache");
        fs::write(&receipt, br#"{"schema":"metalsharp.launch.receipt.v1"}"#).expect("write receipt");

        let report = gog_doctor_for(&ms_home);
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some("metalsharp.gog.diagnostics.v1"));
        assert_eq!(report.get("ok").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(report.pointer("/prefix/mustNotUsePrefixSteam").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(report.pointer("/library/installedGameCount").and_then(|value| value.as_u64()), Some(1));
        assert_eq!(report.pointer("/receipts/count").and_then(|value| value.as_u64()), Some(1));
        let _ = fs::remove_dir_all(&ms_home);
    }

    #[test]
    fn gog_launch_receipt_uses_unified_schema() {
        let game = GogGame {
            product_id: "1876546888".into(),
            title: "Fall of Porcupine Prologue".into(),
            platform: "windows".into(),
            game_folder: Some("/Games/FallOfPorcupine".into()),
            primary_exe: Some("FallOfPorcupine.exe".into()),
            primary_task_name: Some("Play".into()),
            installed: true,
            ..Default::default()
        };
        let args = vec!["launch".into(), "/Games/FallOfPorcupine".into(), "1876546888".into()];
        let receipt = gog_launch_receipt_for(
            &game,
            &args,
            123,
            Path::new("/tmp/gog-launch.log"),
            Path::new("/runtime/wine/bin/metalsharp-wine"),
            Path::new("/prefix/gog-prefix/prefix"),
        );
        assert_eq!(receipt.get("schema").and_then(|value| value.as_str()), Some("metalsharp.launch.receipt.v1"));
        assert_eq!(receipt.get("source").and_then(|value| value.as_str()), Some("gog"));
        assert_eq!(receipt.get("route").and_then(|value| value.as_str()), Some("gogdl_wine"));
        assert_eq!(receipt.get("runtimeContractId").and_then(|value| value.as_str()), Some("gogdl_wine"));
        assert_eq!(receipt.get("pid").and_then(|value| value.as_u64()), Some(123));
        assert_eq!(receipt.get("prefix").and_then(|value| value.as_str()), Some("/prefix/gog-prefix/prefix"));
        assert!(receipt
            .get("envKeys")
            .and_then(|value| value.as_array())
            .unwrap()
            .iter()
            .any(|key| key.as_str() == Some("GOGDL_CONFIG_PATH")));
    }
}
