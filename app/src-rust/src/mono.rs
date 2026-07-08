//! Wine Mono install / upgrade surface.
//!
//! Provides the backend for the GOG "Install Mono" header button and the
//! Settings "Upgrade Mono" action. Both download the official Wine Mono
//! `x86.msi` for the pinned latest release and run it non-quiet
//! (`wine msiexec /i <msi>`, no `/qn`) inside a Wine prefix so the user sees
//! the installer. Once the versioned `windows/mono/wine-mono-<version>/`
//! directory exists in the prefix, the install is considered complete and the
//! caller can hide its button.
//!
//! Detection is prefix-filesystem based (no registry reads) so it works
//! identically for the GOG prefix and the Steam prefix.
//!
//! ## Install flow
//!
//! The MSI install follows the same shape as the Sharp Library's MSI installer
//! (`sharp_library::launch_msi_installer`):
//!
//! 1. Download the MSI asynchronously with progress reported via the status
//!    endpoint so the frontend can show a progress bar instead of blocking the
//!    HTTP handler on a ~100 MB download.
//! 2. Stage (copy) the cached MSI into the target prefix so `msiexec` runs
//!    with its working directory inside the prefix — matching the Sharp
//!    Library's `stage_installer_exe` / `current_dir` pattern.
//! 3. Launch `wine msiexec /i <staged_msi>` non-quiet with a single log handle
//!    (cloned for stdout), exactly matching `launch_msi_installer`.

use serde_json::{json, Value};
use std::fs::{self, OpenOptions};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::OnceLock;

/// Pinned "latest" Wine Mono release. Bumping this (version + msi filename +
/// expected install dir) is the only change needed to ship a new mono.
pub const WINE_MONO_LATEST_VERSION: &str = "11.2.0";
const WINE_MONO_LATEST_MSI_FILENAME: &str = "wine-mono-11.2.0-x86.msi";
const WINE_MONO_RELEASE_TAG: &str = "wine-mono-11.2.0";
const WINE_MONO_INSTALL_DIR_NAME: &str = "wine-mono-11.2.0";

fn wine_mono_msi_url() -> &'static str {
    "https://github.com/wine-mono/wine-mono/releases/download/wine-mono-11.2.0/wine-mono-11.2.0-x86.msi"
}

/// Directory used to cache the downloaded MSI so repeated installs/upgrades do
/// not re-download. Lives under the MetalSharp data folder.
fn mono_cache_dir() -> PathBuf {
    crate::platform::metalsharp_home_dir().join("cache").join("wine-mono")
}

fn mono_cache_msi_path() -> PathBuf {
    mono_cache_dir().join(WINE_MONO_LATEST_MSI_FILENAME)
}

/// In-flight install state so the renderer can poll a single status endpoint
/// without re-deriving it. Mirrors the wineboot/poll patterns elsewhere.
#[derive(Debug, Clone, Default, serde::Serialize)]
struct MonoInstallState {
    running: bool,
    pid: Option<u32>,
    prefix: Option<String>,
    log_path: Option<String>,
    target_version: String,
    last_error: Option<String>,
    /// Background MSI download tracking so the frontend can show progress.
    downloading: bool,
    download_bytes: u64,
    download_total: u64,
    download_error: Option<String>,
}

static INSTALL_STATE: OnceLock<std::sync::Mutex<MonoInstallState>> = OnceLock::new();

fn install_state() -> &'static std::sync::Mutex<MonoInstallState> {
    INSTALL_STATE.get_or_init(|| {
        std::sync::Mutex::new(MonoInstallState {
            target_version: WINE_MONO_LATEST_VERSION.to_string(),
            ..Default::default()
        })
    })
}

fn read_install_state() -> MonoInstallState {
    install_state().lock().map(|s| s.clone()).unwrap_or_default()
}

fn mutate_install_state<F: FnOnce(&mut MonoInstallState)>(f: F) {
    if let Ok(mut state) = install_state().lock() {
        f(&mut state);
    }
}

/// Returns the Wine Mono version installed inside a prefix, by scanning
/// `drive_c/windows/mono/wine-mono-<version>/`. Returns `None` when no
/// wine-mono install is present.
pub fn detect_wine_mono_version(prefix: &Path) -> Option<String> {
    let mono_root = prefix.join("drive_c").join("windows").join("mono");
    if !mono_root.is_dir() {
        return None;
    }
    // Pick the highest wine-mono-<version> dir present using a numeric
    // dotted-version comparison (so 11.2.0 beats 9.5.0).
    let mut best: Option<String> = None;
    for entry in fs::read_dir(&mono_root).ok()?.flatten() {
        if !entry.path().is_dir() {
            continue;
        }
        let name = entry.file_name().to_string_lossy().to_string();
        if let Some(version) = name.strip_prefix("wine-mono-") {
            if best.as_deref().map(|b| compare_versions(version, b).is_gt()).unwrap_or(true) {
                best = Some(version.to_string());
            }
        }
    }
    best
}

/// Compare two dotted version strings (e.g. "11.2.0" vs "9.5.0") numerically.
fn compare_versions(a: &str, b: &str) -> std::cmp::Ordering {
    let mut a_it = a.split('.');
    let mut b_it = b.split('.');
    loop {
        match (a_it.next(), b_it.next()) {
            (Some(a_part), Some(b_part)) => {
                let a_num = a_part.parse::<u64>().unwrap_or(0);
                let b_num = b_part.parse::<u64>().unwrap_or(0);
                match a_num.cmp(&b_num) {
                    std::cmp::Ordering::Equal => continue,
                    other => return other,
                }
            },
            (Some(_), None) => return std::cmp::Ordering::Greater,
            (None, Some(_)) => return std::cmp::Ordering::Less,
            (None, None) => return std::cmp::Ordering::Equal,
        }
    }
}

/// True when the prefix has the pinned latest Wine Mono installed.
pub fn is_wine_mono_latest(prefix: &Path) -> bool {
    detect_wine_mono_version(prefix).as_deref() == Some(WINE_MONO_LATEST_VERSION)
}

/// Status payload shared by the GOG header button and the Settings row.
/// `prefix_kind` is "gog" or "steam" so the renderer can label appropriately.
pub fn wine_mono_status(prefix: &Path, prefix_kind: &str) -> Value {
    let installed_version = detect_wine_mono_version(prefix);
    let up_to_date = installed_version.as_deref() == Some(WINE_MONO_LATEST_VERSION);
    let state = read_install_state();
    json!({
        "ok": true,
        "prefixKind": prefix_kind,
        "latestVersion": WINE_MONO_LATEST_VERSION,
        "installedVersion": installed_version,
        "installed": installed_version.is_some(),
        "upToDate": up_to_date,
        "running": state.running,
        "pid": state.pid,
        "logPath": state.log_path,
        "targetVersion": state.target_version,
        "lastError": state.last_error,
        "msiCached": mono_cache_msi_path().is_file(),
        "downloading": state.downloading,
        "downloadBytes": state.download_bytes,
        "downloadTotal": state.download_total,
        "downloadError": state.download_error,
    })
}

// ---------------------------------------------------------------------------
// MSI staging — mirrors sharp_library::stage_installer_exe + current_dir
// ---------------------------------------------------------------------------

/// Copy the cached MSI into a staging directory inside the prefix so the
/// installer runs with its working directory co-located with the prefix.
/// Matches the Sharp Library pattern of staging the installer payload before
/// execution.
fn stage_msi_into_prefix(cached_msi: &Path, prefix: &Path) -> Result<PathBuf, String> {
    let staging_dir = prefix.join(".ms-mono-install");
    fs::create_dir_all(&staging_dir).map_err(|e| format!("create mono staging dir: {}", e))?;
    let file_name = cached_msi.file_name().ok_or_else(|| "msi cache filename missing".to_string())?;
    let staged = staging_dir.join(file_name);

    // Only re-copy when the cached MSI is newer or the staged copy is absent.
    let needs_copy = !staged.is_file()
        || cached_msi.metadata().ok().and_then(|m| m.modified().ok())
            > staged.metadata().ok().and_then(|m| m.modified().ok());
    if needs_copy {
        fs::copy(cached_msi, &staged).map_err(|e| format!("stage mono msi: {}", e))?;
    }
    Ok(staged)
}

// ---------------------------------------------------------------------------
// Async download — non-blocking, progress reported through MonoInstallState
// ---------------------------------------------------------------------------

/// Spawn a background thread to download the MSI. Updates `MonoInstallState`
/// with progress as bytes arrive. Idempotent: does nothing if a download is
/// already in flight.
fn spawn_msi_download() {
    {
        let state = read_install_state();
        if state.downloading {
            return;
        }
    }
    mutate_install_state(|s| {
        s.downloading = true;
        s.download_bytes = 0;
        s.download_total = 0;
        s.download_error = None;
    });

    let cache_dir = mono_cache_dir();
    let cache_path = mono_cache_msi_path();
    let url = wine_mono_msi_url().to_string();

    std::thread::spawn(move || {
        let result = download_msi_to_cache(&url, &cache_dir, &cache_path);
        match result {
            Ok(_) => mutate_install_state(|s| {
                s.downloading = false;
            }),
            Err(e) => mutate_install_state(|s| {
                s.downloading = false;
                s.download_error = Some(e);
            }),
        }
    });
}

/// Synchronous download worker (runs on the background thread). Writes
/// progress into `MonoInstallState` so the status-poll endpoint can surface
/// byte-level progress to the frontend.
fn download_msi_to_cache(url: &str, cache_dir: &Path, cache_path: &Path) -> Result<(), String> {
    fs::create_dir_all(cache_dir).map_err(|e| format!("create wine-mono cache dir: {}", e))?;

    let config = ureq::config::Config::builder()
        .user_agent(format!("MetalSharp/wine-mono-{}", WINE_MONO_LATEST_VERSION))
        .build();
    let agent = ureq::Agent::new_with_config(config);
    let response = agent.get(url).call().map_err(|e| format!("wine-mono download failed: {}", e))?;

    let total: u64 = response
        .headers()
        .get("content-length")
        .and_then(|v| v.to_str().ok())
        .and_then(|v| v.parse::<u64>().ok())
        .unwrap_or(0);
    mutate_install_state(|s| {
        s.download_total = total;
    });

    let tmp = cache_path.with_extension("msi.part");
    let mut file = fs::File::create(&tmp).map_err(|e| format!("create wine-mono msi: {}", e))?;
    let mut reader = response.into_body().into_reader();
    let mut buf = [0u8; 65536];
    let mut downloaded: u64 = 0;
    loop {
        let n = reader.read(&mut buf).map_err(|e| format!("wine-mono read: {}", e))?;
        if n == 0 {
            break;
        }
        file.write_all(&buf[..n]).map_err(|e| format!("wine-mono write: {}", e))?;
        downloaded += n as u64;
        mutate_install_state(|s| {
            s.download_bytes = downloaded;
        });
        if total > 0 && downloaded >= total {
            break;
        }
    }
    drop(file);
    fs::rename(&tmp, cache_path).map_err(|e| format!("finalize wine-mono msi: {}", e))?;
    Ok(())
}

// ---------------------------------------------------------------------------
// MSI launch — matches sharp_library::launch_msi_installer shape exactly
// ---------------------------------------------------------------------------

/// Launch the Wine Mono MSI installer non-quiet in `prefix`.
///
/// The MSI is staged into a `.ms-mono-install` directory inside the prefix
/// and `wine msiexec /i <staged>` is launched with CWD set to the staging
/// directory — matching the Sharp Library's `launch_msi_installer` pattern.
///
/// `wine msiexec /i <msi>` (no `/qn`, no `/passive`) so the user sees the
/// installer GUI and completes it interactively. The spawned wine process is
/// reaped in a background thread; the renderer polls
/// `/wine-mono/status?prefix=<kind>` and treats `upToDate == true` as
/// completion (the button disappears).
pub fn install_wine_mono_latest(prefix: &Path, prefix_kind: &str) -> Result<u32, String> {
    // 1. Resolve wine binary
    let home = dirs::home_dir().ok_or_else(|| "no home dir".to_string())?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err(format!("MetalSharp Wine not found: {}", wine.display()));
    }

    // 2. Ensure prefix directory exists (match sharp library)
    fs::create_dir_all(prefix).map_err(|e| format!("create prefix dir: {}", e))?;
    if !prefix.join("drive_c").is_dir() {
        return Err(format!("prefix not initialized (no drive_c): {}", prefix.display()));
    }

    // 3. Verify the cached MSI is present
    let cached_msi = mono_cache_msi_path();
    if !cached_msi.is_file() || cached_msi.metadata().map(|m| m.len() < 1_000_000).unwrap_or(true) {
        return Err("Wine Mono MSI not downloaded yet".to_string());
    }

    // 4. Stage MSI into the prefix (match sharp stage_installer_exe + current_dir)
    let staged_msi = stage_msi_into_prefix(&cached_msi, prefix)?;

    // 5. Log setup — single open, clone handle for stdout (match sharp)
    let log_dir = crate::platform::metalsharp_home_dir_for(&home).join("logs");
    fs::create_dir_all(&log_dir).map_err(|e| format!("create log dir: {}", e))?;
    let log_path = log_dir.join(format!("wine-mono-install-{}.log", prefix_kind));

    let mut log = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&log_path)
        .map_err(|e| format!("open wine-mono log: {}", e))?;
    let _ = writeln!(log, "installer_kind=msi");
    let _ = writeln!(log, "prefix={}", prefix.display());
    let _ = writeln!(log, "msi={}", staged_msi.display());
    let _ = writeln!(log, "mono_version={}", WINE_MONO_LATEST_VERSION);
    let _ = writeln!(log, "--- wine output ---");
    let stdout = log.try_clone().map_err(|e| format!("clone log handle: {}", e))?;

    // 6. Build command (match sharp library exactly)
    let mut cmd = Command::new(&wine);
    cmd.arg("msiexec")
        .arg("/i")
        .arg(&staged_msi)
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(log));
    // Set CWD to the staging directory so msiexec resolves temp/cab files
    // relative to the prefix — matches sharp library pattern.
    if let Some(parent) = staged_msi.parent() {
        cmd.current_dir(parent);
    }
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);

    // 7. Spawn
    let mut child = cmd.spawn().map_err(|e| format!("spawn wine msiexec: {}", e))?;
    let pid = child.id();

    mutate_install_state(|s| {
        s.running = true;
        s.pid = Some(pid);
        s.prefix = Some(prefix.to_string_lossy().to_string());
        s.log_path = Some(log_path.to_string_lossy().to_string());
        s.target_version = WINE_MONO_LATEST_VERSION.to_string();
        s.last_error = None;
    });

    // Reap the installer process so it does not become a zombie. The renderer
    // discovers completion via `upToDate` in the status poll, not via process
    // exit, because the user may cancel the MSI GUI.
    let prefix_for_thread: PathBuf = prefix.to_path_buf();
    std::thread::spawn(move || {
        let _ = child.wait();
        let still_latest = detect_wine_mono_version(&prefix_for_thread).as_deref() == Some(WINE_MONO_LATEST_VERSION);
        mutate_install_state(|s| {
            s.running = false;
            s.pid = None;
            if !still_latest {
                s.last_error = Some(format!(
                    "Wine Mono installer exited but {} was not detected in {}",
                    WINE_MONO_LATEST_VERSION,
                    prefix_for_thread.display()
                ));
            }
        });
    });

    Ok(pid)
}

// ---------------------------------------------------------------------------
// Public handlers
// ---------------------------------------------------------------------------

/// Path resolution for the two supported prefixes.
pub fn prefix_for_kind(prefix_kind: &str) -> Result<PathBuf, String> {
    match prefix_kind {
        "gog" => Ok(crate::bottles::bottle_dir("gog-prefix").join("prefix")),
        "steam" => Ok(crate::platform::metalsharp_home_dir().join("prefix-steam")),
        other => Err(format!("unknown prefix kind: {}", other)),
    }
}

pub fn handle_status(prefix_kind: &str) -> Value {
    match prefix_for_kind(prefix_kind) {
        Ok(prefix) => wine_mono_status(&prefix, prefix_kind),
        Err(error) => json!({"ok": false, "error": error}),
    }
}

/// Install handler with two-phase flow:
///
/// 1. **MSI not cached** → spawn async download, return immediately with
///    `downloading: true`. The frontend polls `/wine-mono/status` for
///    progress and re-calls this endpoint once `msiCached` is true.
/// 2. **MSI cached** → stage into prefix and launch the installer.
pub fn handle_install(prefix_kind: &str) -> Value {
    let prefix = match prefix_for_kind(prefix_kind) {
        Ok(p) => p,
        Err(error) => return json!({"ok": false, "error": error}),
    };

    // If the latest is already installed, treat as a no-op success so a
    // redundant button press does not re-launch the installer.
    if is_wine_mono_latest(&prefix) {
        return json!({
            "ok": true,
            "alreadyInstalled": true,
            "status": wine_mono_status(&prefix, prefix_kind),
        });
    }

    // If a download is already in progress, just return current status.
    {
        let state = read_install_state();
        if state.downloading {
            return json!({
                "ok": true,
                "downloading": true,
                "status": wine_mono_status(&prefix, prefix_kind),
            });
        }
    }

    // Phase 1: MSI not cached yet — start async download.
    if !mono_cache_msi_path().is_file() {
        spawn_msi_download();
        return json!({
            "ok": true,
            "downloading": true,
            "status": wine_mono_status(&prefix, prefix_kind),
        });
    }

    // Phase 2: MSI cached — launch the installer.
    match install_wine_mono_latest(&prefix, prefix_kind) {
        Ok(pid) => json!({
            "ok": true,
            "pid": pid,
            "status": wine_mono_status(&prefix, prefix_kind),
        }),
        Err(error) => {
            mutate_install_state(|s| {
                s.running = false;
                s.last_error = Some(error.clone());
            });
            json!({
                "ok": false,
                "error": error,
                "status": wine_mono_status(&prefix, prefix_kind),
            })
        },
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn latest_version_constants_are_consistent() {
        assert!(WINE_MONO_LATEST_VERSION.contains('.'));
        assert!(WINE_MONO_LATEST_MSI_FILENAME.contains(WINE_MONO_LATEST_VERSION));
        assert!(WINE_MONO_INSTALL_DIR_NAME.contains(WINE_MONO_LATEST_VERSION));
        assert!(wine_mono_msi_url().contains(WINE_MONO_RELEASE_TAG));
        assert!(wine_mono_msi_url().ends_with(WINE_MONO_LATEST_MSI_FILENAME));
    }

    #[test]
    fn compare_versions_is_numeric_not_lexicographic() {
        assert!(compare_versions("11.2.0", "9.5.0").is_gt());
        assert!(compare_versions("9.5.0", "11.2.0").is_lt());
        assert!(compare_versions("11.2.0", "11.2.0").is_eq());
        assert!(compare_versions("11.2.1", "11.2.0").is_gt());
        assert!(compare_versions("11.3.0", "11.2.9").is_gt());
        assert_eq!(compare_versions("2.0", "2.0.0"), std::cmp::Ordering::Less);
    }

    #[test]
    fn detect_returns_none_when_mono_dir_absent() {
        let tmp = std::env::temp_dir().join(format!(
            "ms-mono-none-{}-{}",
            std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()
        ));
        assert_eq!(detect_wine_mono_version(&tmp), None);
        assert!(!is_wine_mono_latest(&tmp));
        let _ = std::fs::remove_dir_all(&tmp);
    }

    #[test]
    fn detect_picks_versioned_mono_dir_and_flags_latest() {
        let tmp = std::env::temp_dir().join(format!(
            "ms-mono-detect-{}-{}",
            std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()
        ));
        let mono = tmp.join("drive_c/windows/mono");
        std::fs::create_dir_all(mono.join("wine-mono-9.5.0")).unwrap();
        std::fs::create_dir_all(mono.join("wine-mono-11.2.0")).unwrap();
        std::fs::create_dir_all(mono.join("not-a-mono")).unwrap();

        assert_eq!(detect_wine_mono_version(&tmp), Some("11.2.0".to_string()));
        assert!(is_wine_mono_latest(&tmp));

        let status = wine_mono_status(&tmp, "gog");
        assert_eq!(status.get("installedVersion").and_then(|v| v.as_str()), Some("11.2.0"));
        assert_eq!(status.get("upToDate").and_then(|v| v.as_bool()), Some(true));
        assert_eq!(status.get("prefixKind").and_then(|v| v.as_str()), Some("gog"));
        let _ = std::fs::remove_dir_all(&tmp);
    }

    #[test]
    fn status_reports_outdated_when_older_version_installed() {
        let tmp = std::env::temp_dir().join(format!(
            "ms-mono-outdated-{}-{}",
            std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()
        ));
        let mono = tmp.join("drive_c/windows/mono");
        std::fs::create_dir_all(mono.join("wine-mono-8.1.0")).unwrap();
        let status = wine_mono_status(&tmp, "steam");
        assert_eq!(status.get("installedVersion").and_then(|v| v.as_str()), Some("8.1.0"));
        assert_eq!(status.get("upToDate").and_then(|v| v.as_bool()), Some(false));
        assert_eq!(status.get("installed").and_then(|v| v.as_bool()), Some(true));
        let _ = std::fs::remove_dir_all(&tmp);
    }

    #[test]
    fn prefix_for_kind_resolves_known_prefixes() {
        assert!(prefix_for_kind("gog").is_ok());
        assert!(prefix_for_kind("steam").is_ok());
        assert!(prefix_for_kind("unknown").is_err());
    }

    #[test]
    fn handle_install_rejects_unknown_prefix_without_spawning() {
        let result = handle_install("nope");
        assert_eq!(result.get("ok").and_then(|v| v.as_bool()), Some(false));
        assert!(result.get("error").is_some());
    }

    #[test]
    fn wine_mono_status_includes_download_fields() {
        let tmp = std::env::temp_dir().join(format!(
            "ms-mono-dl-fields-{}-{}",
            std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()
        ));
        // Reset install state so the test starts clean.
        mutate_install_state(|s| {
            s.downloading = true;
            s.download_bytes = 42;
            s.download_total = 100;
        });
        let status = wine_mono_status(&tmp, "steam");
        assert_eq!(status.get("downloading").and_then(|v| v.as_bool()), Some(true));
        assert_eq!(status.get("downloadBytes").and_then(|v| v.as_u64()), Some(42));
        assert_eq!(status.get("downloadTotal").and_then(|v| v.as_u64()), Some(100));
        mutate_install_state(|s| {
            s.downloading = false;
            s.download_bytes = 0;
            s.download_total = 0;
        });
        let _ = std::fs::remove_dir_all(&tmp);
    }
}
