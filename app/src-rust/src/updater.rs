use serde_json::json;
use std::fs;
use std::io::Read;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};

const CURRENT_VERSION: &str = env!("CARGO_PKG_VERSION");
const REPO_API: &str = "https://api.github.com/repos/aaf2tbz/metalsharp/releases/latest";

static UPDATING: AtomicBool = AtomicBool::new(false);
static DOWNLOAD_PERCENT: AtomicU32 = AtomicU32::new(0);

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
    let config = ureq::config::Config::builder().user_agent(format!("MetalSharp/{}", CURRENT_VERSION)).build();
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

    let latest_raw = release.tag_name.trim();
    let latest = clean_version(latest_raw);
    let current = CURRENT_VERSION.to_string();
    let available = semver_gt(&latest, &current);

    let download_url = find_dmg_asset(&release.assets).map(|a| a.browser_download_url.clone()).unwrap_or_default();

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
        "release_notes": release_notes,
        "release_name": release_name,
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
    let parse = |v: &str| -> Vec<u32> {
        v.split('.')
            .filter_map(|p| {
                let clean: String = p.chars().take_while(|c| c.is_ascii_digit()).collect();
                clean.parse::<u32>().ok()
            })
            .collect()
    };
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

    if !dmg_path.exists() {
        write_update_progress("downloading", 10, &format!("Downloading v{}...", latest_version), None);
        if let Err(e) = download_with_progress(&download_url, &dmg_path) {
            write_update_progress("error", 0, &format!("Download failed: {}", e), Some(&e.to_string()));
            let _ = fs::remove_file(&dmg_path);
            return;
        }
    } else {
        app_log(&format!("Using cached DMG: {}", dmg_path.display()));
    }

    write_update_progress("downloaded", 80, &format!("Download complete — ready to install v{}", latest_version), None);
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

    let cache_dir = home.join(".metalsharp").join("cache").join("updates");
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

pub fn get_downloaded_dmg_path() -> Option<String> {
    let update_info = check_for_update();
    let latest_version = update_info.get("latest_version").and_then(|v| v.as_str())?;
    let home = dirs::home_dir()?;
    let dmg_path =
        home.join(".metalsharp").join("cache").join("updates").join(format!("MetalSharp-{}.dmg", latest_version));

    if dmg_path.exists() {
        Some(dmg_path.to_string_lossy().to_string())
    } else {
        None
    }
}

fn app_log(msg: &str) {
    let home = dirs::home_dir().unwrap_or_default();
    let log_dir = home.join(".metalsharp").join("logs");
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
}
