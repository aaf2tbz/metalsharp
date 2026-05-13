use serde_json::json;
use std::fs;
use std::io::Read;
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};

const CURRENT_VERSION: &str = env!("CARGO_PKG_VERSION");
const REPO_API: &str = "https://api.github.com/repos/aaf2tbz/metalsharp/releases/latest";
const REPO_OWNER: &str = "aaf2tbz";
const REPO_NAME: &str = "metalsharp";

static UPDATING: AtomicBool = AtomicBool::new(false);
static DOWNLOAD_PERCENT: AtomicU32 = AtomicU32::new(0);
static UPDATE_STATUS: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());

fn progress_path() -> PathBuf {
    dirs::home_dir().unwrap_or_default().join(".metalsharp").join("update_progress.json")
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
    let config = ureq::config::Config::builder().user_agent(&format!("MetalSharp/{}", CURRENT_VERSION)).build();
    let agent = ureq::Agent::new_with_config(config);

    let mut resp = match agent.get(REPO_API).call() {
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

    let latest = release.tag_name.trim_start_matches('v').to_string();
    let current = CURRENT_VERSION.to_string();
    let available = semver_gt(&latest, &current);

    let download_url = find_dmg_asset(&release.assets).map(|a| a.browser_download_url.clone()).unwrap_or_default();

    let release_notes = release.body.unwrap_or_default();
    let release_name = release.name.unwrap_or_else(|| release.tag_name.clone());

    json!({
        "ok": true,
        "current_version": current,
        "latest_version": latest,
        "available": available,
        "download_url": download_url,
        "release_notes": release_notes,
        "release_name": release_name,
    })
}

fn semver_gt(a: &str, b: &str) -> bool {
    let parse = |v: &str| -> Vec<u32> { v.split('.').filter_map(|p| p.parse::<u32>().ok()).collect() };
    let av = parse(a);
    let bv = parse(b);
    for i in 0..std::cmp::max(av.len(), bv.len()) {
        let x = av.get(i).unwrap_or(&0);
        let y = bv.get(i).unwrap_or(&0);
        if x > y {
            return true;
        }
        if x < y {
            return false;
        }
    }
    false
}

pub fn start_update() -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    if UPDATING.load(Ordering::SeqCst) {
        return Ok(json!({"ok": false, "error": "update already in progress"}));
    }

    if UPDATING.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst).is_err() {
        return Ok(json!({"ok": false, "error": "update already in progress"}));
    }

    DOWNLOAD_PERCENT.store(0, Ordering::SeqCst);
    write_update_progress("starting", 0, "Checking for updates...", None);

    std::thread::spawn(|| {
        run_update();
        UPDATING.store(false, Ordering::SeqCst);
    });

    Ok(json!({"ok": true}))
}

fn run_update() {
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

    let home = match dirs::home_dir() {
        Some(h) => h,
        None => {
            write_update_progress("error", 0, "no home directory", Some("no_home"));
            return;
        },
    };

    let cache_dir = home.join(".metalsharp").join("cache").join("updates");
    let _ = fs::create_dir_all(&cache_dir);
    let dmg_path = cache_dir.join(format!("MetalSharp-{}.dmg", latest_version));

    write_update_progress("downloading", 10, &format!("Downloading v{}...", latest_version), None);

    if !dmg_path.exists() {
        if let Err(e) = download_with_progress(&download_url, &dmg_path) {
            write_update_progress("error", 0, &format!("Download failed: {}", e), Some(&e.to_string()));
            let _ = fs::remove_file(&dmg_path);
            return;
        }
    } else {
        write_update_progress("downloading", 80, "Using cached DMG...", None);
    }

    write_update_progress("stopping_backend", 85, "Stopping backend...", None);
    stop_backend();

    write_update_progress("mounting", 87, "Mounting DMG...", None);
    let mount_point = match mount_dmg(&dmg_path) {
        Some(m) => m,
        None => {
            write_update_progress("error", 0, "Failed to mount DMG", Some("mount_failed"));
            return;
        },
    };

    write_update_progress("installing", 90, "Installing new version...", None);

    let app_source = find_app_in_mount(&mount_point);
    match app_source {
        Some(src) => {
            if let Err(e) = install_app(&src) {
                let _ = detach_dmg(&mount_point);
                write_update_progress("error", 0, &format!("Install failed: {}", e), Some(&e.to_string()));
                return;
            }
        },
        None => {
            let _ = detach_dmg(&mount_point);
            write_update_progress("error", 0, "MetalSharp.app not found in DMG", Some("app_not_found"));
            return;
        },
    }

    write_update_progress("installing", 95, "Unmounting installer...", None);
    let _ = detach_dmg(&mount_point);

    write_update_progress("relaunching", 98, "Relaunching MetalSharp...", None);

    std::thread::sleep(std::time::Duration::from_secs(2));
    relaunch_app();

    write_update_progress("complete", 100, &format!("Updated to v{}!", latest_version), None);
}

fn download_with_progress(url: &str, dest: &PathBuf) -> Result<(), String> {
    let config = ureq::config::Config::builder().user_agent(&format!("MetalSharp/{}", CURRENT_VERSION)).build();
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

fn stop_backend() {
    let _ = Command::new("pkill")
        .args(["-f", "metalsharp-backend"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();
    std::thread::sleep(std::time::Duration::from_millis(500));
}

fn mount_dmg(dmg_path: &PathBuf) -> Option<String> {
    let output = Command::new("hdiutil").args(["attach", "-nobrowse", "-quiet"]).arg(dmg_path).output().ok()?;

    if !output.status.success() {
        return None;
    }

    let stdout = String::from_utf8_lossy(&output.stdout);
    for line in stdout.lines().rev() {
        let parts: Vec<&str> = line.split_whitespace().collect();
        if let Some(mount) = parts.last() {
            if mount.starts_with("/Volumes/") {
                return Some(mount.to_string());
            }
        }
    }
    None
}

fn detach_dmg(mount_point: &str) -> bool {
    Command::new("hdiutil")
        .args(["detach", mount_point, "-quiet"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

fn find_app_in_mount(mount_point: &str) -> Option<PathBuf> {
    let mount = PathBuf::from(mount_point);
    if let Ok(entries) = fs::read_dir(&mount) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            if name.ends_with(".app") && name.to_lowercase().contains("metalsharp") {
                return Some(entry.path());
            }
        }
    }
    None
}

fn install_app(app_source: &PathBuf) -> Result<(), String> {
    let target = PathBuf::from("/Applications/MetalSharp.app");

    if target.exists() {
        let _ = Command::new("rm")
            .args(["-rf"])
            .arg(&target)
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
    }

    let output = Command::new("cp")
        .args(["-R"])
        .arg(app_source)
        .arg(&target)
        .output()
        .map_err(|e| format!("cp failed: {}", e))?;

    if !output.status.success() {
        return Err(format!("copy failed: {}", String::from_utf8_lossy(&output.stderr)));
    }

    Ok(())
}

fn relaunch_app() {
    let app_path = "/Applications/MetalSharp.app";
    let _ = Command::new("open")
        .arg(app_path)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn();

    let _ = Command::new("pkill")
        .args(["-f", "MetalSharp"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn();
}

use std::io::Write;
