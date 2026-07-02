use serde_json::json;
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};

const CURRENT_VERSION: &str = env!("CARGO_PKG_VERSION");
const PUBLIC_REPO_API: &str = "https://api.github.com/repos/aaf2tbz/metalsharp/releases/latest";
const UPDATE_REPO_API_ENV: &str = "METALSHARP_UPDATE_REPO_API";
const ALLOW_PUBLIC_UPDATES_ENV: &str = "METALSHARP_ALLOW_PUBLIC_UPDATES";

static UPDATING: AtomicBool = AtomicBool::new(false);
static DOWNLOAD_PERCENT: AtomicU32 = AtomicU32::new(0);

fn progress_path() -> PathBuf {
    crate::platform::metalsharp_home_dir().join("update_progress.json")
}

fn write_update_progress(status: &str, percent: u32, message: &str, error: Option<&str>) {
    let data = json!({
        "status": status,
        "percent": percent,
        "message": message,
        "error": error,
    });
    let _ = fs::write(progress_path(), serde_json::to_string(&data).unwrap_or_default());
}

pub fn is_updating() -> bool {
    UPDATING.load(Ordering::SeqCst)
}

pub fn read_update_progress() -> serde_json::Value {
    let path = progress_path();
    if path.exists() {
        if let Ok(contents) = fs::read_to_string(&path) {
            if let Ok(v) = serde_json::from_str::<serde_json::Value>(&contents) {
                return v;
            }
        }
    }
    json!({
        "status": "idle",
        "percent": 0,
        "message": "",
        "error": null,
    })
}

#[derive(serde::Deserialize)]
struct GithubRelease {
    tag_name: String,
    name: Option<String>,
    body: Option<String>,
    assets: Vec<GithubAsset>,
}

#[derive(serde::Deserialize)]
struct GithubAsset {
    name: String,
    browser_download_url: String,
    size: u64,
}

fn find_dmg_asset(assets: &[GithubAsset]) -> Option<&GithubAsset> {
    assets.iter().find(|a| a.name.ends_with("-arm64.dmg") || a.name.ends_with(".dmg"))
}

pub fn check_for_update() -> serde_json::Value {
    let repo_api = match update_repo_api() {
        Some(repo_api) => repo_api,
        None => return updates_disabled_response(),
    };
    let config = ureq::config::Config::builder().user_agent(format!("MetalSharp/{}", CURRENT_VERSION)).build();
    let agent = ureq::Agent::new_with_config(config);

    let mut resp = match agent.get(&repo_api).call() {
        Ok(r) => r,
        Err(e) => {
            return json!({
                "ok": false,
                "error": format!("failed to fetch release: {}", e),
                "current_version": CURRENT_VERSION,
            })
        },
    };

    let release: GithubRelease = match resp.body_mut().read_json() {
        Ok(r) => r,
        Err(e) => {
            return json!({
                "ok": false,
                "error": format!("failed to parse release: {}", e),
                "current_version": CURRENT_VERSION,
            })
        },
    };

    let latest_raw = release.tag_name.trim();
    let latest = clean_version(latest_raw);
    let current = CURRENT_VERSION.to_string();
    let available = semver_gt(&latest, &current);

    let download_url = find_dmg_asset(&release.assets).map(|a| a.browser_download_url.clone()).unwrap_or_default();
    let download_size = find_dmg_asset(&release.assets).map(|a| a.size).unwrap_or(0);

    let release_notes = release.body.unwrap_or_default();
    let release_name = release.name.unwrap_or_else(|| release.tag_name.clone());

    app_log(&format!(
        "Update check: current={} latest_raw='{}' latest_clean='{}' available={}",
        current, latest_raw, latest, available
    ));

    json!({
        "ok": true,
        "current_version": current,
        "latest_version": latest,
        "available": available,
        "download_url": download_url,
        "download_size": download_size,
        "release_notes": release_notes,
        "release_name": release_name,
        "update_repo_api": repo_api,
        "updates_disabled": false,
    })
}

fn update_repo_api() -> Option<String> {
    let custom_repo = std::env::var(UPDATE_REPO_API_ENV).ok();
    let allow_public = std::env::var(ALLOW_PUBLIC_UPDATES_ENV).map(|value| value == "1").unwrap_or(false);
    update_repo_api_from_values(custom_repo.as_deref(), allow_public)
}

fn update_repo_api_from_values(custom_repo: Option<&str>, allow_public: bool) -> Option<String> {
    if let Some(value) = custom_repo {
        let trimmed = value.trim();
        if !trimmed.is_empty() {
            return Some(trimmed.to_string());
        }
    }
    allow_public.then(|| PUBLIC_REPO_API.to_string())
}

fn updates_disabled_response() -> serde_json::Value {
    json!({
        "ok": true,
        "current_version": CURRENT_VERSION,
        "latest_version": CURRENT_VERSION,
        "available": false,
        "download_url": "",
        "download_size": 0,
        "release_notes": "Public MetalSharp updates are disabled for the private MetalSharp Wine 2.0 fork. Set METALSHARP_UPDATE_REPO_API or METALSHARP_ALLOW_PUBLIC_UPDATES=1 to opt in.",
        "release_name": "MetalSharp Wine 2.0 private fork",
        "updates_disabled": true,
    })
}

fn clean_version(tag: &str) -> String {
    let v = tag.trim().trim_start_matches('v');
    let parts: Vec<&str> = v.split('.').collect();
    parts
        .iter()
        .map(|p| p.chars().take_while(|c| c.is_ascii_digit()).collect::<String>())
        .filter(|p| !p.is_empty())
        .collect::<Vec<_>>()
        .join(".")
}

fn semver_gt(a: &str, b: &str) -> bool {
    compare_versions(a, b).is_gt()
}

pub fn start_update() -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    if update_repo_api().is_none() {
        return Ok(json!({
            "ok": false,
            "error": "updates are disabled for this private Wine 2.0 fork; set METALSHARP_UPDATE_REPO_API or METALSHARP_ALLOW_PUBLIC_UPDATES=1 to opt in",
            "updates_disabled": true,
        }));
    }

    if UPDATING.load(Ordering::SeqCst) {
        return Ok(json!({"ok": false, "error": "update already in progress"}));
    }

    if UPDATING.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst).is_err() {
        return Ok(json!({"ok": false, "error": "update already in progress"}));
    }

    DOWNLOAD_PERCENT.store(0, Ordering::SeqCst);
    write_update_progress("starting", 0, "Checking for updates...", None);

    std::thread::spawn(|| {
        run_download();
        UPDATING.store(false, Ordering::SeqCst);
    });

    Ok(json!({"ok": true}))
}

fn run_download() {
    write_update_progress("checking", 5, "Fetching latest release info...", None);

    let update_info = check_for_update();
    let download_url = match update_info.get("download_url").and_then(|u| u.as_str()) {
        Some(url) if !url.is_empty() => url.to_string(),
        _ => {
            write_update_progress("error", 0, "No DMG download URL found", Some("no_download_url"));
            return;
        },
    };

    let latest_version = update_info.get("latest_version").and_then(|v| v.as_str()).unwrap_or("unknown").to_string();
    let expected_size = update_info.get("download_size").and_then(|v| v.as_u64()).unwrap_or(0);

    let home = match dirs::home_dir() {
        Some(h) => h,
        None => {
            write_update_progress("error", 0, "no home directory", Some("no_home"));
            return;
        },
    };

    let cache_dir = crate::platform::metalsharp_home_dir_for(&home).join("cache").join("updates");
    let _ = fs::create_dir_all(&cache_dir);
    let dmg_path = cache_dir.join(format!("MetalSharp-{}.dmg", latest_version));

    if cached_dmg_ready(&dmg_path, expected_size) {
        app_log(&format!("Using cached DMG: {}", dmg_path.display()));
    } else {
        let _ = fs::remove_file(&dmg_path);
        write_update_progress("downloading", 10, &format!("Downloading v{}...", latest_version), None);
        if let Err(e) = download_with_progress(&download_url, &dmg_path) {
            write_update_progress("error", 0, &format!("Download failed: {}", e), Some(&e.to_string()));
            let _ = fs::remove_file(&dmg_path);
            return;
        }
        if !cached_dmg_ready(&dmg_path, expected_size) {
            write_update_progress(
                "error",
                0,
                "Downloaded DMG size did not match the latest release asset",
                Some("dmg_size_mismatch"),
            );
            let _ = fs::remove_file(&dmg_path);
            return;
        }
    }

    write_update_progress("downloaded", 80, &format!("Download complete — ready to install v{}", latest_version), None);
}

fn cached_dmg_ready(path: &PathBuf, expected_size: u64) -> bool {
    let Ok(meta) = fs::metadata(path) else {
        return false;
    };
    if !meta.is_file() || meta.len() == 0 {
        return false;
    }
    expected_size == 0 || meta.len() == expected_size
}

fn download_with_progress(url: &str, dest: &PathBuf) -> Result<(), String> {
    let config = ureq::config::Config::builder().user_agent(format!("MetalSharp/{}", CURRENT_VERSION)).build();
    let agent = ureq::Agent::new_with_config(config);

    let resp = agent.get(url).call().map_err(|e| format!("HTTP request failed: {}", e))?;

    let total_size: u64 = resp
        .headers()
        .get("content-length")
        .and_then(|v| v.to_str().ok())
        .and_then(|v| v.parse::<u64>().ok())
        .unwrap_or(0);

    let tmp_path = dest.with_extension("dmg.tmp");
    let mut file = fs::File::create(&tmp_path).map_err(|e| format!("create file: {}", e))?;

    let mut reader = resp.into_body().into_reader();
    let mut buf = [0u8; 65536];
    let mut downloaded: u64 = 0;
    let mut last_percent: u32 = 10;

    loop {
        let n = reader.read(&mut buf).map_err(|e| format!("read error: {}", e))?;
        if n == 0 {
            break;
        }
        use std::io::Write;
        file.write_all(&buf[..n]).map_err(|e| format!("write error: {}", e))?;
        downloaded += n as u64;

        if total_size > 0 {
            let pct = 10 + ((downloaded as f64 / total_size as f64) * 70.0) as u32;
            if pct > last_percent {
                last_percent = pct;
                DOWNLOAD_PERCENT.store(pct, Ordering::SeqCst);
                write_update_progress("downloading", pct, &format!("Downloading... {}%", pct), None);
            }
        }
    }

    drop(file);
    fs::rename(&tmp_path, dest).map_err(|e| format!("rename: {}", e))?;

    Ok(())
}

pub fn cleanup_downloaded_dmgs() -> serde_json::Value {
    let home = match dirs::home_dir() {
        Some(h) => h,
        None => return json!({"ok": false, "error": "no home dir"}),
    };

    let cache_dir = crate::platform::metalsharp_home_dir_for(&home).join("cache").join("updates");
    if !cache_dir.exists() {
        return json!({"ok": true, "removed": 0, "bytes_freed": 0});
    }

    let mut removed = 0u32;
    let mut bytes_freed: u64 = 0;

    if let Ok(entries) = fs::read_dir(&cache_dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().map(|e| e == "dmg").unwrap_or(false) {
                let size = fs::metadata(&path).map(|m| m.len()).unwrap_or(0);
                if fs::remove_file(&path).is_ok() {
                    bytes_freed += size;
                    removed += 1;
                }
            }
        }
    }

    if removed > 0 {
        app_log(&format!("Cleaned up {} downloaded DMG(s), freed {} bytes", removed, bytes_freed));
    }

    json!({"ok": true, "removed": removed, "bytes_freed": bytes_freed})
}

pub fn get_downloaded_dmg() -> Option<(String, String)> {
    let update_info = check_for_update();
    let latest_version = update_info.get("latest_version").and_then(|v| v.as_str())?;
    let home = dirs::home_dir()?;
    let cache_dir = crate::platform::metalsharp_home_dir_for(&home).join("cache").join("updates");
    let dmg_path = cache_dir.join(format!("MetalSharp-{}.dmg", latest_version));

    let expected_size = update_info.get("download_size").and_then(|v| v.as_u64()).unwrap_or(0);

    if cached_dmg_ready(&dmg_path, expected_size) {
        Some((dmg_path.to_string_lossy().to_string(), latest_version.to_string()))
    } else {
        newest_cached_update_dmg(&cache_dir, CURRENT_VERSION)
            .map(|(version, path)| (path.to_string_lossy().to_string(), version))
    }
}

pub fn get_downloaded_dmg_path() -> Option<String> {
    get_downloaded_dmg().map(|(path, _version)| path)
}

fn newest_cached_update_dmg(cache_dir: &Path, current_version: &str) -> Option<(String, PathBuf)> {
    let mut candidates = Vec::new();
    for entry in fs::read_dir(cache_dir).ok()?.flatten() {
        let path = entry.path();
        if !cached_dmg_ready(&path, 0) {
            continue;
        }
        let Some(version) = dmg_filename_version(&path) else {
            continue;
        };
        if semver_gt(&version, current_version) {
            candidates.push((version, path));
        }
    }

    candidates.sort_by(|(left, _), (right, _)| compare_versions(left, right));
    candidates.pop()
}

fn dmg_filename_version(path: &Path) -> Option<String> {
    let name = path.file_name()?.to_str()?;
    let raw = name.strip_prefix("MetalSharp-")?.strip_suffix(".dmg")?;
    let raw = raw.strip_suffix("-arm64").unwrap_or(raw);
    let version = clean_version(raw);
    (!version.is_empty()).then_some(version)
}

fn compare_versions(left: &str, right: &str) -> std::cmp::Ordering {
    let left = parse_version_parts(left);
    let right = parse_version_parts(right);
    for i in 0..std::cmp::max(left.len(), right.len()) {
        let l = left.get(i).unwrap_or(&0);
        let r = right.get(i).unwrap_or(&0);
        match l.cmp(r) {
            std::cmp::Ordering::Equal => {},
            ordering => return ordering,
        }
    }
    std::cmp::Ordering::Equal
}

fn parse_version_parts(value: &str) -> Vec<u32> {
    value
        .split('.')
        .filter_map(|p| {
            let clean: String = p.chars().take_while(|c| c.is_ascii_digit()).collect();
            clean.parse::<u32>().ok()
        })
        .collect()
}

fn app_log(msg: &str) {
    let home = dirs::home_dir().unwrap_or_default();
    let log_dir = crate::platform::metalsharp_home_dir_for(&home).join("logs");
    let _ = fs::create_dir_all(&log_dir);
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
    let secs = now.as_secs();
    let h = (secs / 3600) % 24;
    let m = (secs / 60) % 60;
    let s = secs % 60;
    let line = format!("[{:02}:{:02}:{:02}] {}\n", h, m, s, msg);
    let (year, month, day) = unix_days_to_ymd(secs / 86400);
    let log_path = log_dir.join(format!("{:04}-{:02}-{:02}.log", year, month, day));
    let _ = fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(&log_path)
        .and_then(|mut f| std::io::Write::write_all(&mut f, line.as_bytes()));
}

fn unix_days_to_ymd(days_since_epoch: u64) -> (i64, u32, u32) {
    let z = days_since_epoch as i64 + 719_468;
    let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1_460 + doe / 36_524 - doe / 146_096) / 365;
    let mut year = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = mp + if mp < 10 { 3 } else { -9 };
    if month <= 2 {
        year += 1;
    }
    (year, month as u32, day as u32)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unix_days_to_ymd_handles_epoch() {
        assert_eq!(unix_days_to_ymd(0), (1970, 1, 1));
    }

    #[test]
    fn unix_days_to_ymd_handles_current_dates_without_underflow() {
        assert_eq!(unix_days_to_ymd(20_592), (2026, 5, 19));
    }

    #[test]
    fn private_fork_updates_are_disabled_without_explicit_opt_in() {
        assert_eq!(update_repo_api_from_values(None, false), None);
        let response = updates_disabled_response();
        assert_eq!(response.get("ok").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(response.get("available").and_then(|value| value.as_bool()), Some(false));
        assert_eq!(response.get("updates_disabled").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(response.get("download_url").and_then(|value| value.as_str()), Some(""));
    }

    #[test]
    fn updater_allows_custom_or_explicit_public_release_sources() {
        assert_eq!(
            update_repo_api_from_values(
                Some(" https://api.github.com/repos/aaf2tbz/MetalSharp-Wine-2.0/releases/latest "),
                false
            )
            .as_deref(),
            Some("https://api.github.com/repos/aaf2tbz/MetalSharp-Wine-2.0/releases/latest")
        );
        assert_eq!(update_repo_api_from_values(Some("   "), true).as_deref(), Some(PUBLIC_REPO_API));
        assert_eq!(update_repo_api_from_values(None, true).as_deref(), Some(PUBLIC_REPO_API));
    }

    #[test]
    fn cached_dmg_requires_nonempty_expected_size_match() {
        let home = test_home("cached-dmg-size");
        fs::create_dir_all(&home).expect("create test dir");
        let dmg = home.join("MetalSharp-0.1.0.dmg");

        assert!(!cached_dmg_ready(&dmg, 4));

        fs::write(&dmg, b"test").expect("write dmg");

        assert!(cached_dmg_ready(&dmg, 0));
        assert!(cached_dmg_ready(&dmg, 4));
        assert!(!cached_dmg_ready(&dmg, 5));

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn cached_update_selection_requires_newer_versioned_dmg() {
        let home = test_home("cached-dmg-version");
        fs::create_dir_all(&home).expect("create test dir");
        fs::write(home.join("MetalSharp-0.37.0.dmg"), b"old").expect("write old dmg");
        fs::write(home.join("MetalSharp-0.37.1.dmg"), b"same").expect("write same dmg");
        fs::write(home.join("MetalSharp-0.37.2.dmg"), b"new").expect("write new dmg");
        fs::write(home.join("MetalSharp-0.37.3-arm64.dmg"), b"newer").expect("write newer dmg");
        fs::write(home.join("MetalSharp-not-a-version.dmg"), b"junk").expect("write junk dmg");

        let (version, selected) = newest_cached_update_dmg(&home, "0.37.1").expect("newer dmg selected");

        assert_eq!(version, "0.37.3");
        assert_eq!(selected.file_name().and_then(|name| name.to_str()), Some("MetalSharp-0.37.3-arm64.dmg"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn cached_update_selection_rejects_old_or_current_dmgs() {
        let home = test_home("cached-dmg-no-newer");
        fs::create_dir_all(&home).expect("create test dir");
        fs::write(home.join("MetalSharp-0.36.9.dmg"), b"old").expect("write old dmg");
        fs::write(home.join("MetalSharp-0.37.1.dmg"), b"same").expect("write same dmg");

        assert!(newest_cached_update_dmg(&home, "0.37.1").is_none());
        let _ = fs::remove_dir_all(home);
    }

    fn test_home(name: &str) -> PathBuf {
        std::env::temp_dir().join(format!(
            "metalsharp-updater-{}-{}-{}",
            name,
            std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).expect("system time").as_nanos()
        ))
    }
}
