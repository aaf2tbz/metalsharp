use serde_json::{json, Value};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

static INSTALLING: AtomicBool = AtomicBool::new(false);

const MAC_RUNTIME_BUNDLE_ASSETS: &[&str] = &[
    "metalsharp_bundle.tar.zst",
    "metalsharp_bundle2.tar.zst",
    "dxmt.tar.zst",
    "gptk.tar.zst",
    "dxvk.tar.zst",
    "mono-x86.tar.zst",
    "mono-arm64.tar.zst",
    "goldberg.tar.zst",
    "eac-toggle.tar.zst",
    "SteamSetup.exe",
    "steamwebhelper.exe",
    "steamwebhelper-wrapper.c",
];

const LINUX_RUNTIME_BUNDLE_ASSETS: &[&str] = &[
    "metalsharp_bundle.tar.zst",
    "metalsharp_bundle2.tar.zst",
    "dxvk.tar.zst",
    "mono-x86.tar.zst",
    "goldberg.tar.zst",
    "eac-toggle.tar.zst",
    "SteamSetup.exe",
    "steamwebhelper.exe",
    "steamwebhelper-wrapper.c",
];

fn progress_path() -> PathBuf {
    dirs::home_dir().unwrap_or_default().join(".metalsharp").join("install_progress.json")
}

fn write_progress(step: usize, total: usize, name: &str, status: &str, log_line: &str, error: Option<&str>) {
    let data = json!({
        "step": step,
        "total": total,
        "current": name,
        "status": status,
        "log": log_line,
        "error": error,
    });
    let path = progress_path();
    let _ = fs::write(&path, serde_json::to_string(&data).unwrap_or_default());
}

pub fn is_installing() -> bool {
    INSTALLING.load(Ordering::SeqCst)
}

pub fn read_progress() -> Value {
    let path = progress_path();
    if path.exists() {
        if let Ok(contents) = fs::read_to_string(&path) {
            if let Ok(v) = serde_json::from_str::<Value>(&contents) {
                return v;
            }
        }
    }
    json!({
        "step": 0,
        "total": 0,
        "current": "",
        "status": "idle",
        "log": "",
        "error": null,
    })
}

pub fn start_install_all() -> Result<Value, Box<dyn std::error::Error>> {
    if INSTALLING.load(Ordering::SeqCst) {
        return Ok(json!({"ok": false, "error": "installation already in progress"}));
    }

    if INSTALLING.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst).is_err() {
        return Ok(json!({"ok": false, "error": "installation already in progress"}));
    }

    std::thread::spawn(|| {
        run_install_all();
        INSTALLING.store(false, Ordering::SeqCst);
    });

    Ok(json!({"ok": true}))
}

fn run_install_all() {
    let home = match dirs::home_dir() {
        Some(h) => h,
        None => {
            write_progress(0, 0, "", "error", "no home directory", Some("no home directory"));
            return;
        },
    };

    let steps: Vec<(&str, Box<dyn Fn(&PathBuf) -> Result<bool, String>>)> =
        if crate::platform::current() == crate::platform::HostPlatform::Linux {
            vec![
                ("Runtime Bundle Downloads", Box::new(ensure_runtime_bundle_assets)),
                ("Runtime Assets", Box::new(install_metalsharp_bundle)),
                ("Host Runtime ABI", Box::new(install_host_runtime)),
                ("DXVK Runtime", Box::new(install_dxvk_fallback)),
                ("Goldberg Steam Emulator", Box::new(install_goldberg)),
                ("Offline EAC Mode", Box::new(install_eac_toggle)),
                ("Pipeline Rules", Box::new(install_mtsp_rules)),
                ("Mono Configs", Box::new(install_mono_configs)),
            ]
        } else {
            vec![
                ("Rosetta 2", Box::new(|_| install_rosetta())),
                ("System Tools", Box::new(|_| install_xcode_cli())),
                ("Runtime Bundle Downloads", Box::new(ensure_runtime_bundle_assets)),
                ("Runtime Assets", Box::new(install_metalsharp_bundle)),
                ("Host Runtime ABI", Box::new(install_host_runtime)),
                ("DXMT Metal Runtime", Box::new(install_dxmt_runtime)),
                ("GPTK D3DMetal Runtime", Box::new(install_gptk_runtime)),
                ("Goldberg Steam Emulator", Box::new(install_goldberg)),
                ("Offline EAC Mode", Box::new(install_eac_toggle)),
                ("Pipeline Rules", Box::new(install_mtsp_rules)),
                ("Mono Configs", Box::new(install_mono_configs)),
                ("Runtime Support", Box::new(|_| install_mono_arm64())),
            ]
        };

    let total = steps.len();

    write_progress(0, total, "Starting...", "starting", "Verifying prerequisites...", None);
    if !check_command("tar") {
        write_progress(
            0,
            total,
            "Prerequisites",
            "error",
            "tar not found — macOS should have this. Is your system intact?",
            Some("tar command not found"),
        );
        return;
    }

    if !check_command("curl") {
        write_progress(
            0,
            total,
            "Prerequisites",
            "error",
            "curl not found — install curl before installing runtime assets.",
            Some("curl command not found"),
        );
        return;
    }

    if crate::platform::current() == crate::platform::HostPlatform::Macos && !check_command("brew") {
        write_progress(
            0,
            total,
            "Homebrew",
            "error",
            "Homebrew is required but not installed. Please install it first from the setup wizard.",
            Some("Homebrew not installed — install from https://brew.sh"),
        );
        return;
    }

    for (i, (name, installer)) in steps.iter().enumerate() {
        let step_num = i + 1;
        write_progress(step_num, total, name, "installing", &format!("Installing {}...", name), None);

        match installer(&home) {
            Ok(false) => {
                write_progress(step_num, total, name, "done", &format!("{} ready", name), None);
            },
            Ok(true) => {
                write_progress(step_num, total, name, "done", &format!("{} installed", name), None);
            },
            Err(e) => {
                write_progress(step_num, total, name, "error", &format!("{} failed: {}", name, e), Some(&e));
                return;
            },
        }

        std::thread::sleep(Duration::from_millis(200));
    }

    write_progress(total, total, "Complete", "complete", "All assets installed!", None);
}

fn runtime_bundle_assets_for_host() -> &'static [&'static str] {
    if crate::platform::current() == crate::platform::HostPlatform::Linux {
        LINUX_RUNTIME_BUNDLE_ASSETS
    } else {
        MAC_RUNTIME_BUNDLE_ASSETS
    }
}

fn ensure_runtime_bundle_assets(_home: &PathBuf) -> Result<bool, String> {
    let mut downloaded = false;
    let mut missing = Vec::new();

    for asset in runtime_bundle_assets_for_host() {
        let had_local = bundled_file_exists(asset);
        match find_bundled_file(asset) {
            Some(path) if file_nonempty(&path) => {
                downloaded |= !had_local;
            },
            _ => missing.push(*asset),
        }
    }

    if missing.is_empty() {
        Ok(downloaded)
    } else {
        Err(format!("Missing required runtime bundle asset(s): {}", missing.join(", ")))
    }
}

fn bundled_file_exists(name: &str) -> bool {
    if let Some(resources) = crate::platform::app_resources_dir() {
        if file_nonempty(&resources.join(format!("bundles/{}", name))) {
            return true;
        }
    }

    if file_nonempty(&PathBuf::from(format!("app/bundles/{}", name))) {
        return true;
    }

    dirs::home_dir()
        .map(|home| file_nonempty(&home.join(".metalsharp").join("cache").join("bundles").join(name)))
        .unwrap_or(false)
}

fn install_rosetta() -> Result<bool, String> {
    let plist = PathBuf::from("/Library/Apple/System/Library/LaunchDaemons/com.apple.oahd.plist");
    if plist.exists() {
        return Ok(false);
    }
    let running = Command::new("pgrep").args(["-q", "oahd"]).status().map(|s| s.success()).unwrap_or(false);
    if running {
        return Ok(false);
    }

    let output = Command::new("softwareupdate")
        .args(["--install-rosetta", "--agree-to-license"])
        .output()
        .map_err(|e| format!("failed to run softwareupdate: {}", e))?;

    if output.status.success() || String::from_utf8_lossy(&output.stderr).contains("already installed") {
        Ok(true)
    } else {
        Err(format!("rosetta install failed: {}", String::from_utf8_lossy(&output.stderr)))
    }
}

fn install_xcode_cli() -> Result<bool, String> {
    if check_command("clang") {
        return Ok(false);
    }

    let output = Command::new("/usr/bin/xcode-select")
        .args(["--install"])
        .output()
        .map_err(|e| format!("failed to run xcode-select: {}", e))?;

    let stderr = String::from_utf8_lossy(&output.stderr);
    if stderr.contains("already installed") || stderr.contains("command line tools are already installed") {
        return Ok(false);
    }

    for _ in 0..120 {
        std::thread::sleep(Duration::from_secs(5));
        let check = check_command("clang");
        if check {
            return Ok(true);
        }
    }

    Err("timed out waiting for Xcode CLI tools installation (you may need to complete it manually)".into())
}

fn install_metalsharp_bundle(home: &PathBuf) -> Result<bool, String> {
    let runtime_dir = home.join(".metalsharp").join("runtime");
    let _ = fs::create_dir_all(&runtime_dir);

    let ms_wine = metalsharp_wine_binary(home);
    let already_installed = ms_wine.exists()
        && home.join(".metalsharp").join("runtime").join("mono-x86").join("bin").join("mono").exists()
        && home.join(".metalsharp").join("runtime").join("dxvk-1.10.3").join("x32").join("d3d11.dll").exists();
    if already_installed {
        return Ok(false);
    }

    let bundle = find_bundled_file("metalsharp_bundle.tar.zst");
    if let Some(archive) = bundle {
        let tmp_extract = std::env::temp_dir().join("metalsharp-bundle-extract");
        let _ = fs::remove_dir_all(&tmp_extract);
        let _ = fs::create_dir_all(&tmp_extract);
        extract_zst(&archive, &tmp_extract, "bundle")?;

        let wine_dir = runtime_dir.join("wine");
        let _ = fs::create_dir_all(&wine_dir);

        let extracted_wine115 = tmp_extract.join("wine-11.5");
        let source = if extracted_wine115.exists() { extracted_wine115 } else { tmp_extract.join("wine") };

        if source.exists() {
            if let Ok(entries) = fs::read_dir(&source) {
                for entry in entries.flatten() {
                    let src_path = entry.path();
                    let file_name = entry.file_name();
                    let dst = wine_dir.join(&file_name);
                    if src_path.is_dir() {
                        let _ = fs::create_dir_all(&dst);
                        let _ = copy_dir_recursive(&src_path, &dst);
                    } else {
                        let _ = fs::copy(&src_path, &dst);
                    }
                }
            }
        }
        let _ = fs::remove_dir_all(&tmp_extract);

        let bundle2 = find_bundled_file("metalsharp_bundle2.tar.zst");
        if let Some(archive2) = bundle2 {
            let _ = extract_zst(&archive2, &runtime_dir, "bundle2");
        }
        let ms_wine = metalsharp_wine_binary(home);
        if ms_wine.exists() {
            let wine_check = Command::new(&ms_wine).arg("--version").output();
            match wine_check {
                Ok(o) if o.status.success() => {
                    let _ = install_mono_x86_fallback(home);
                    let _ = install_dxvk_fallback(home);
                    return Ok(true);
                },
                Ok(o) => {
                    return Err(format!(
                        "Wine binary exists but --version failed: {}",
                        String::from_utf8_lossy(&o.stderr)
                    ))
                },
                Err(e) => return Err(format!("Wine binary exists but cannot execute: {}", e)),
            }
        }
    }

    let fallback_wine = find_bundled_archive("wine");
    if let Some(archive) = fallback_wine {
        let wine_dir = runtime_dir.join("wine");
        let _ = fs::create_dir_all(&wine_dir);
        extract_zst(&archive, &wine_dir, "wine")?;
        let ms_wine = metalsharp_wine_binary(home);
        if ms_wine.exists() {
            let _ = install_mono_x86_fallback(home);
            let _ = install_dxvk_fallback(home);
            return Ok(true);
        }
    }

    if crate::platform::current() == crate::platform::HostPlatform::Linux {
        return install_linux_system_wine_runtime(home);
    }

    Err("MetalSharp runtime not found — no bundled metalsharp_bundle.tar.zst available".into())
}

fn metalsharp_wine_binary(home: &Path) -> PathBuf {
    crate::platform::runtime_wine_binary(&home.join(".metalsharp").join("runtime").join("wine"))
}

fn install_host_runtime(home: &PathBuf) -> Result<bool, String> {
    let dest = home.join(".metalsharp").join("runtime").join("host");
    if host_runtime_ready(&dest) {
        return Ok(false);
    }

    let source = find_packaged_host_runtime()
        .ok_or_else(|| "MetalSharp host runtime not found — packaged runtime/host assets are missing".to_string())?;
    let _ = fs::remove_dir_all(&dest);
    fs::create_dir_all(&dest).map_err(|e| format!("create host runtime dir: {}", e))?;
    copy_dir_recursive(&source, &dest)?;

    if host_runtime_ready(&dest) {
        Ok(true)
    } else {
        Err("MetalSharp host runtime copied but required ABI files are missing".into())
    }
}

fn host_runtime_ready(dir: &Path) -> bool {
    file_nonempty(&dir.join("manifest.json"))
        && file_nonempty(&dir.join("HostRuntimeABI.h"))
        && (file_nonempty(&dir.join("libmetalsharp_host_runtime.dylib"))
            || file_nonempty(&dir.join("libmetalsharp_host_runtime.so"))
            || file_nonempty(&dir.join("metalsharp_host_runtime.dll")))
}

fn file_nonempty(path: &Path) -> bool {
    path.metadata().map(|meta| meta.is_file() && meta.len() > 0).unwrap_or(false)
}

fn find_packaged_host_runtime() -> Option<PathBuf> {
    if let Some(resources) = crate::platform::app_resources_dir() {
        let dir = resources.join("runtime").join("host");
        if host_runtime_ready(&dir) {
            return Some(dir);
        }
    }

    let dev = PathBuf::from("app/native/host");
    if host_runtime_ready(&dev) {
        return Some(dev);
    }

    if let Ok(exe) = std::env::current_exe() {
        let dev = exe.parent()?.parent()?.parent()?.parent()?.join("native").join("host");
        if host_runtime_ready(&dev) {
            return Some(dev);
        }
    }

    None
}

fn install_linux_system_wine_runtime(home: &PathBuf) -> Result<bool, String> {
    let wine = find_system_command("wine").ok_or_else(|| {
        "Linux Wine runtime not found — install wine or bundle metalsharp_linux_runtime.tar.zst".to_string()
    })?;

    let bin_dir = home.join(".metalsharp").join("runtime").join("wine").join("bin");
    fs::create_dir_all(&bin_dir).map_err(|e| format!("create wine runtime bin dir: {}", e))?;

    for wrapper_name in &["wine", "metalsharp-wine"] {
        let wrapper = bin_dir.join(wrapper_name);
        let contents = format!("#!/bin/sh\nexec '{}' \"$@\"\n", wine.to_string_lossy());
        fs::write(&wrapper, contents).map_err(|e| format!("write {}: {}", wrapper.display(), e))?;
        make_executable(&wrapper);
    }

    Ok(true)
}

fn install_mono_x86_fallback(home: &PathBuf) -> Result<bool, String> {
    let mono_x86 = home.join(".metalsharp").join("runtime").join("mono-x86").join("bin").join("mono");
    if mono_x86.exists() {
        return Ok(false);
    }
    let bundled = find_bundled_archive("mono-x86");
    let runtime_dir = home.join(".metalsharp").join("runtime");
    if let Some(archive) = bundled {
        extract_zst(&archive, &runtime_dir, "mono-x86")?;
        if mono_x86.exists() {
            return Ok(true);
        }
    }
    Err("mono x86 fallback not found".into())
}

fn install_dxvk_fallback(home: &PathBuf) -> Result<bool, String> {
    let dxvk_dir = home.join(".metalsharp").join("runtime").join("dxvk-1.10.3");
    if dxvk_dir.join("x32").join("d3d11.dll").exists() {
        return Ok(false);
    }
    let _ = fs::create_dir_all(&dxvk_dir);
    let bundled = find_bundled_archive("dxvk");
    if let Some(archive) = bundled {
        let tmp = std::env::temp_dir().join("metalsharp-dxvk-extract");
        let _ = fs::remove_dir_all(&tmp);
        let _ = fs::create_dir_all(&tmp);
        extract_zst(&archive, &tmp, "dxvk")?;
        let src = tmp.join("dxvk-1.10.3");
        if src.exists() {
            for subdir in &["x32", "x64"] {
                let s = src.join(subdir);
                if s.exists() {
                    let _ = fs::create_dir_all(dxvk_dir.join(subdir));
                    for entry in fs::read_dir(&s).map_err(|e| format!("read {}: {}", subdir, e))? {
                        let entry = entry.map_err(|e| e.to_string())?;
                        let _ = fs::copy(entry.path(), dxvk_dir.join(subdir).join(entry.file_name()));
                    }
                }
            }
        }
        let _ = fs::remove_dir_all(&tmp);
        if dxvk_dir.join("x32").join("d3d11.dll").exists() {
            return Ok(true);
        }
    }
    Err("DXVK fallback not found".into())
}

fn install_metalsharp_wine(home: &PathBuf) -> Result<bool, String> {
    let ms_wine = metalsharp_wine_binary(home);
    if ms_wine.exists() {
        return Ok(false);
    }

    let wine_dir = home.join(".metalsharp").join("runtime").join("wine");
    let _ = fs::create_dir_all(&wine_dir);

    let bundled = find_bundled_archive("wine");
    if let Some(archive) = bundled {
        extract_zst(&archive, &wine_dir, "wine")?;
        let ms_wine = metalsharp_wine_binary(home);
        if ms_wine.exists() {
            return Ok(true);
        }
    }

    Err("MetalSharp Wine not found — no bundled wine.tar.zst available".into())
}

fn install_dxmt_runtime(home: &PathBuf) -> Result<bool, String> {
    let dxmt_dir = home.join(".metalsharp").join("runtime").join("wine").join("lib").join("dxmt");

    if dxmt_runtime_ready(&dxmt_dir) {
        return Ok(false);
    }

    let _ = fs::create_dir_all(dxmt_dir.join("x86_64-unix"));
    let _ = fs::create_dir_all(dxmt_dir.join("x86_64-windows"));

    let bundled = find_bundled_archive("dxmt");
    if let Some(archive) = bundled {
        let tmp = std::env::temp_dir().join("metalsharp-dxmt-extract");
        let _ = fs::remove_dir_all(&tmp);
        let _ = fs::create_dir_all(&tmp);
        extract_zst(&archive, &tmp, "dxmt")?;

        let src_x64_unix = tmp.join("x86_64-unix");
        let src_x64_windows = tmp.join("x86_64-windows");

        if src_x64_unix.exists() {
            for entry in fs::read_dir(&src_x64_unix).map_err(|e| format!("read x86_64-unix: {}", e))? {
                let entry = entry.map_err(|e| e.to_string())?;
                let _ = fs::copy(entry.path(), dxmt_dir.join("x86_64-unix").join(entry.file_name()));
            }
        }
        if src_x64_windows.exists() {
            for entry in fs::read_dir(&src_x64_windows).map_err(|e| format!("read x86_64-windows: {}", e))? {
                let entry = entry.map_err(|e| e.to_string())?;
                let _ = fs::copy(entry.path(), dxmt_dir.join("x86_64-windows").join(entry.file_name()));
            }
        }

        let _ = fs::remove_dir_all(&tmp);
    } else {
        let dxmt_src = home.join("metalsharp").join("runtime").join("dxmt");
        if dxmt_src.join("x86_64-windows").join("d3d11.dll").exists() {
            for subdir in &["x86_64-unix", "x86_64-windows"] {
                let src = dxmt_src.join(subdir);
                let dst = dxmt_dir.join(subdir);
                if src.exists() {
                    let _ = fs::create_dir_all(&dst);
                    if let Ok(entries) = fs::read_dir(&src) {
                        for entry in entries.flatten() {
                            let _ = fs::copy(entry.path(), dst.join(entry.file_name()));
                        }
                    }
                }
            }
        }
    }

    if dxmt_runtime_ready(&dxmt_dir) {
        Ok(true)
    } else {
        Err("DXMT Metal runtime not found — bundle dxmt.tar.zst or place files in ~/metalsharp/runtime/dxmt/".into())
    }
}

fn dxmt_runtime_ready(dxmt_dir: &Path) -> bool {
    let pe_dir = dxmt_dir.join("x86_64-windows");
    let required_pe =
        ["d3d10core.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "winemetal.dll", "nvapi64.dll", "nvngx.dll"];
    file_nonempty(&dxmt_dir.join("x86_64-unix").join("winemetal.so"))
        && required_pe.iter().all(|dll| file_nonempty(&pe_dir.join(dll)))
}

fn install_gptk_runtime(home: &PathBuf) -> Result<bool, String> {
    let gptk_dir = home.join(".metalsharp").join("runtime").join("wine").join("lib").join("gptk");
    let ext_dir = home.join(".metalsharp").join("runtime").join("wine").join("lib").join("external");
    let framework = ext_dir.join("D3DMetal.framework");

    if gptk_runtime_ready(&gptk_dir, &framework) {
        return Ok(false);
    }

    fs::create_dir_all(gptk_dir.join("x86_64-windows")).map_err(|e| format!("create GPTK PE dir: {}", e))?;
    fs::create_dir_all(gptk_dir.join("x86_64-unix")).map_err(|e| format!("create GPTK Unix dir: {}", e))?;
    fs::create_dir_all(&ext_dir).map_err(|e| format!("create GPTK external dir: {}", e))?;

    let bundled = find_bundled_archive("gptk");
    if let Some(archive) = bundled {
        let tmp = std::env::temp_dir().join("metalsharp-gptk-extract");
        let _ = fs::remove_dir_all(&tmp);
        let _ = fs::create_dir_all(&tmp);
        extract_zst(&archive, &tmp, "gptk")?;

        let src_x64_windows = tmp.join("x86_64-windows");
        let src_x64_unix = tmp.join("x86_64-unix");
        let src_external = tmp.join("external");

        if src_x64_windows.exists() {
            for entry in fs::read_dir(&src_x64_windows).map_err(|e| format!("read x86_64-windows: {}", e))? {
                let entry = entry.map_err(|e| e.to_string())?;
                fs::copy(entry.path(), gptk_dir.join("x86_64-windows").join(entry.file_name()))
                    .map_err(|e| format!("copy GPTK Windows DLL {}: {}", entry.file_name().to_string_lossy(), e))?;
            }
        }
        if src_x64_unix.exists() {
            for entry in fs::read_dir(&src_x64_unix).map_err(|e| format!("read x86_64-unix: {}", e))? {
                let entry = entry.map_err(|e| e.to_string())?;
                fs::copy(entry.path(), gptk_dir.join("x86_64-unix").join(entry.file_name()))
                    .map_err(|e| format!("copy GPTK Unix library {}: {}", entry.file_name().to_string_lossy(), e))?;
            }
        }
        if src_external.exists() {
            for entry in fs::read_dir(&src_external).map_err(|e| format!("read external: {}", e))? {
                let entry = entry.map_err(|e| e.to_string())?;
                let dst = ext_dir.join(entry.file_name());
                if entry.path().is_dir() {
                    fs::create_dir_all(&dst)
                        .map_err(|e| format!("create GPTK external dir {}: {}", dst.display(), e))?;
                    copy_dir_recursive(&entry.path(), &dst)?;
                } else {
                    fs::copy(entry.path(), &dst)
                        .map_err(|e| format!("copy GPTK external file {}: {}", dst.display(), e))?;
                }
            }
        }

        let _ = fs::remove_dir_all(&tmp);
    }

    if gptk_runtime_ready(&gptk_dir, &framework) {
        Ok(true)
    } else {
        Err("GPTK D3DMetal runtime incomplete — bundle gptk.tar.zst with required PE DLLs and D3DMetal.framework contents".into())
    }
}

fn gptk_runtime_ready(gptk_dir: &Path, framework: &Path) -> bool {
    let pe_dir = gptk_dir.join("x86_64-windows");
    let required_pe = ["d3d10.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "nvapi64.dll", "nvngx.dll", "atidxx64.dll"];
    required_pe.iter().all(|dll| file_nonempty(&pe_dir.join(dll)))
        && file_nonempty(&framework.join("Versions").join("A").join("D3DMetal"))
        && framework_has_resource_dylib(framework)
}

fn framework_has_resource_dylib(framework: &Path) -> bool {
    for resources_dir in [framework.join("Resources"), framework.join("Versions").join("A").join("Resources")] {
        if let Ok(entries) = fs::read_dir(resources_dir) {
            if entries.flatten().any(|entry| {
                entry.path().extension().and_then(|ext| ext.to_str()) == Some("dylib") && file_nonempty(&entry.path())
            }) {
                return true;
            }
        }
    }
    false
}

fn install_goldberg(home: &PathBuf) -> Result<bool, String> {
    let goldberg_dir = home.join(".metalsharp").join("runtime").join("goldberg");
    let x86_dll = goldberg_dir.join("x86").join("steam_api.dll");
    let x64_dll = goldberg_dir.join("x64").join("steam_api64.dll");

    if x86_dll.exists() && x64_dll.exists() {
        return Ok(false);
    }

    let _ = fs::create_dir_all(goldberg_dir.join("x86"));
    let _ = fs::create_dir_all(goldberg_dir.join("x64"));

    let bundled = find_bundled_archive("goldberg");
    if let Some(archive) = bundled {
        let tmp = std::env::temp_dir().join("metalsharp-goldberg-extract");
        let _ = fs::remove_dir_all(&tmp);
        let _ = fs::create_dir_all(&tmp);
        extract_zst(&archive, &tmp, "goldberg")?;

        let src_x86 = tmp.join("x86");
        let src_x64 = tmp.join("x64");

        if src_x86.exists() {
            for entry in fs::read_dir(&src_x86).map_err(|e| format!("read x86: {}", e))? {
                let entry = entry.map_err(|e| e.to_string())?;
                let _ = fs::copy(entry.path(), goldberg_dir.join("x86").join(entry.file_name()));
            }
        }
        if src_x64.exists() {
            for entry in fs::read_dir(&src_x64).map_err(|e| format!("read x64: {}", e))? {
                let entry = entry.map_err(|e| e.to_string())?;
                let _ = fs::copy(entry.path(), goldberg_dir.join("x64").join(entry.file_name()));
            }
        }

        let _ = fs::remove_dir_all(&tmp);
    }

    if goldberg_dir.join("x86").join("steam_api.dll").exists()
        && goldberg_dir.join("x64").join("steam_api64.dll").exists()
    {
        Ok(true)
    } else {
        Err("Goldberg Steam emulator not found — goldberg.tar.zst missing from bundles".into())
    }
}

fn install_eac_toggle(home: &PathBuf) -> Result<bool, String> {
    let eac_dir = home.join(".metalsharp").join("runtime").join("eac-toggle");
    let dll = eac_dir.join("x86_64-windows").join("_winhttp.dll");

    if dll.exists() {
        return Ok(false);
    }

    let _ = fs::create_dir_all(eac_dir.join("x86_64-windows"));

    let bundled = find_bundled_archive("eac-toggle");
    if let Some(archive) = bundled {
        let tmp = std::env::temp_dir().join("metalsharp-eac-toggle-extract");
        let _ = fs::remove_dir_all(&tmp);
        let _ = fs::create_dir_all(&tmp);
        extract_zst(&archive, &tmp, "eac-toggle")?;

        let src_windows = tmp.join("x86_64-windows");
        if src_windows.exists() {
            for entry in fs::read_dir(&src_windows).map_err(|e| format!("read x86_64-windows: {}", e))? {
                let entry = entry.map_err(|e| e.to_string())?;
                let _ = fs::copy(entry.path(), eac_dir.join("x86_64-windows").join(entry.file_name()));
            }
        }

        let _ = fs::remove_dir_all(&tmp);
    }

    if eac_dir.join("x86_64-windows").join("_winhttp.dll").exists() {
        Ok(true)
    } else {
        Ok(false)
    }
}

fn install_mtsp_rules(home: &PathBuf) -> Result<bool, String> {
    let dest = home.join(".metalsharp").join("configs").join("mtsp-rules.toml");
    let mut candidates = vec![
        PathBuf::from("configs/mtsp-rules.toml"),
        home.join("metalsharp").join("configs").join("mtsp-rules.toml"),
        home.join("repos").join("metalsharp").join("configs").join("mtsp-rules.toml"),
    ];

    if let Ok(exe) = std::env::current_exe() {
        if let Some(mut dir) = exe.parent() {
            for _ in 0..8 {
                candidates.push(dir.join("configs").join("mtsp-rules.toml"));
                match dir.parent() {
                    Some(p) => dir = p,
                    None => break,
                }
            }
        }
    }

    for src in &candidates {
        if src.exists() {
            if let Ok(contents) = fs::read_to_string(src) {
                if let Ok(existing) = fs::read_to_string(&dest) {
                    if existing == contents {
                        return Ok(false);
                    }
                    let backup = dest.with_extension("toml.bak");
                    let _ = fs::write(&backup, existing);
                }
                fs::create_dir_all(dest.parent().unwrap()).map_err(|e| format!("create MTSP config dir: {}", e))?;
                fs::write(&dest, &contents).map_err(|e| format!("write mtsp-rules.toml: {}", e))?;
                if fs::read_to_string(&dest).ok().as_deref() == Some(contents.as_str()) {
                    return Ok(true);
                }
                return Err("mtsp-rules.toml was written but could not be verified".into());
            }
        }
    }

    Err("mtsp-rules.toml not found — pipeline auto-detection will use PE analysis fallback".into())
}

fn install_mono_configs(home: &PathBuf) -> Result<bool, String> {
    let configs_dir = home.join(".metalsharp").join("configs");
    let _ = fs::create_dir_all(&configs_dir);

    let config_files = ["terraria-mono.config", "celeste-x86-mono.config"];
    let mut any_installed = false;

    for name in &config_files {
        let dest = configs_dir.join(name);
        if dest.exists() {
            continue;
        }

        let mut candidates = vec![
            PathBuf::from(format!("configs/{}", name)),
            home.join(".metalsharp").join("configs").join(name),
            home.join("repos").join("metalsharp").join("configs").join(name),
        ];

        if let Ok(exe) = std::env::current_exe() {
            if let Some(mut dir) = exe.parent() {
                for _ in 0..8 {
                    candidates.push(dir.join("configs").join(name));
                    match dir.parent() {
                        Some(p) => dir = p,
                        None => break,
                    }
                }
            }
        }

        for src in &candidates {
            if src.exists() {
                if let Ok(contents) = fs::read_to_string(src) {
                    let _ = fs::write(&dest, &contents);
                    if dest.exists() {
                        any_installed = true;
                        break;
                    }
                }
            }
        }
    }

    Ok(any_installed)
}

fn install_windows_steam(home: &PathBuf) -> Result<bool, String> {
    let steam_exe = home
        .join(".metalsharp")
        .join("prefix-steam")
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("Steam.exe");
    if steam_exe.exists() {
        return Ok(false);
    }

    let ms_wine = metalsharp_wine_binary(home);
    if !ms_wine.exists() {
        return Err("MetalSharp Wine not found — cannot install Steam".into());
    }

    let installer = home.join(".metalsharp").join("SteamSetup.exe");

    let _ = fs::remove_file(&installer);

    let output = Command::new("curl")
        .args(["-sL", "-o"])
        .arg(&installer)
        .arg("https://steamcdn-a.akamaihd.net/client/installer/SteamSetup.exe")
        .output()
        .map_err(|e| format!("curl failed: {}", e))?;
    if !output.status.success() {
        let bundled = find_bundled_file("SteamSetup.exe");
        if let Some(bundled) = bundled {
            let _ = fs::copy(&bundled, &installer);
        }
        if !installer.exists() {
            return Err("failed to download SteamSetup.exe and no bundled fallback".into());
        }
    }

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let _ = fs::create_dir_all(&prefix);

    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let mut wineboot_cmd = Command::new(&ms_wine);
    wineboot_cmd
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .arg("wineboot")
        .arg("--init")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    crate::platform::set_runtime_library_env(&mut wineboot_cmd, &ms_root);
    let _ = wineboot_cmd.status();

    let mut install_cmd = Command::new(&ms_wine);
    install_cmd
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .arg(&installer)
        .args(["/S"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    crate::platform::set_runtime_library_env(&mut install_cmd, &ms_root);
    let _ = install_cmd.spawn().map_err(|e| format!("Steam install spawn failed: {}", e))?;

    for _ in 0..90 {
        std::thread::sleep(Duration::from_secs(2));
        if steam_exe.exists() {
            let steam_dir =
                home.join(".metalsharp").join("prefix-steam").join("drive_c").join("Program Files (x86)").join("Steam");
            crate::steam::deploy_steamwebhelper_wrapper(&steam_dir);
            return Ok(true);
        }
    }

    if steam_exe.exists() {
        let steam_dir =
            home.join(".metalsharp").join("prefix-steam").join("drive_c").join("Program Files (x86)").join("Steam");
        crate::steam::deploy_steamwebhelper_wrapper(&steam_dir);
        Ok(true)
    } else {
        Err("Steam.exe not found after installation — may need manual install".into())
    }
}

fn check_command(cmd: &str) -> bool {
    find_system_command(cmd).is_some()
}

fn find_system_command(cmd: &str) -> Option<PathBuf> {
    let candidates = match cmd {
        "which" => vec![PathBuf::from("/usr/bin/which")],
        "mono" => vec![
            PathBuf::from("/opt/homebrew/bin/mono"),
            PathBuf::from("/usr/local/bin/mono"),
            PathBuf::from("/usr/bin/mono"),
        ],
        "wine" => vec![PathBuf::from("/usr/bin/wine"), PathBuf::from("/usr/local/bin/wine")],
        _ => vec![PathBuf::from(cmd)],
    };
    for c in &candidates {
        if c.exists() {
            return Some(c.clone());
        }
    }
    Command::new("/usr/bin/which")
        .arg(cmd)
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
        .then(|| {
            let output = Command::new("/usr/bin/which").arg(cmd).output().ok()?;
            let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
            (!path.is_empty()).then_some(PathBuf::from(path))
        })
        .flatten()
}

fn make_executable(path: &PathBuf) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if let Ok(metadata) = fs::metadata(path) {
            let mut permissions = metadata.permissions();
            permissions.set_mode(0o755);
            let _ = fs::set_permissions(path, permissions);
        }
    }
}

fn install_mono_arm64() -> Result<bool, String> {
    if check_command("mono") {
        return Ok(false);
    }

    let home = dirs::home_dir().ok_or("Cannot find home directory")?;
    let mono_arm64 = home.join(".metalsharp").join("runtime").join("mono-arm64").join("bin").join("mono");
    if mono_arm64.exists() {
        return Ok(false);
    }

    let bundled = find_bundled_archive("mono-arm64");
    let runtime_dir = home.join(".metalsharp").join("runtime");
    let _ = fs::create_dir_all(&runtime_dir);

    if let Some(archive) = bundled {
        extract_zst(&archive, &runtime_dir, "mono-arm64")?;
        if mono_arm64.exists() {
            return Ok(true);
        }
    }

    brew_install("mono")
}

fn install_moltenvk() -> Result<bool, String> {
    let icd = PathBuf::from("/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json");
    if icd.exists() {
        return Ok(false);
    }

    let bundled = find_bundled_archive("moltenvk");
    if let Some(archive) = bundled {
        let cellar = PathBuf::from("/opt/homebrew/Cellar/molten-vk");
        let _ = fs::create_dir_all(&cellar);
        extract_zst(&archive, &cellar, "moltenvk")?;
        if icd.exists() {
            return Ok(true);
        }
    }

    brew_install("molten-vk")
}

fn find_brew() -> Result<PathBuf, String> {
    let candidates = [PathBuf::from("/opt/homebrew/bin/brew"), PathBuf::from("/usr/local/bin/brew")];
    for c in &candidates {
        if c.exists() {
            return Ok(c.clone());
        }
    }
    let output = Command::new("which").arg("brew").output().ok();
    if let Some(o) = output {
        if o.status.success() {
            let path = String::from_utf8_lossy(&o.stdout).trim().to_string();
            if !path.is_empty() {
                return Ok(PathBuf::from(path));
            }
        }
    }
    Err("Homebrew not found — install it first".into())
}

fn brew_install(package: &str) -> Result<bool, String> {
    let brew = find_brew()?;
    let output = Command::new(&brew).args(["install", package]).output().map_err(|e| format!("brew failed: {}", e))?;

    let combined = format!("{}{}", String::from_utf8_lossy(&output.stdout), String::from_utf8_lossy(&output.stderr));

    if output.status.success() || combined.contains("already installed") {
        Ok(true)
    } else {
        Err(combined.lines().last().unwrap_or("brew install failed").into())
    }
}

fn find_bundled_archive(name: &str) -> Option<PathBuf> {
    let candidates = [find_in_resources(name), find_in_dev_path(name)];

    if let Some(found) = candidates.into_iter().find(|c| c.is_some()).flatten() {
        return Some(found);
    }

    download_from_github_release(&format!("{}.tar.zst", name))
}

fn find_bundled_file(name: &str) -> Option<PathBuf> {
    if let Some(resources) = crate::platform::app_resources_dir() {
        let file = resources.join(format!("bundles/{}", name));
        if file.exists() {
            return Some(file);
        }
    }

    let dev = PathBuf::from(format!("app/bundles/{}", name));
    if dev.exists() {
        return Some(dev);
    }

    download_from_github_release(name)
}

fn download_from_github_release(filename: &str) -> Option<PathBuf> {
    let cache_dir = dirs::home_dir()?.join(".metalsharp").join("cache").join("bundles");
    let _ = fs::create_dir_all(&cache_dir);
    let cached = cache_dir.join(filename);
    let tmp = cache_dir.join(format!("{}.download", filename));

    if file_nonempty(&cached) {
        return Some(cached);
    }

    let url = format!("https://github.com/aaf2tbz/metalsharp/releases/download/bundles/{}", filename);

    let _ = fs::remove_file(&tmp);
    let output = Command::new("curl")
        .args(["--fail", "--location", "--silent", "--show-error", "--retry", "3", "--connect-timeout", "20", "-o"])
        .arg(&tmp)
        .arg(&url)
        .output()
        .ok()?;

    if output.status.success()
        && file_nonempty(&tmp)
        && fs::rename(&tmp, &cached).or_else(|_| fs::copy(&tmp, &cached).map(|_| ())).is_ok()
        && file_nonempty(&cached)
    {
        let _ = fs::remove_file(&tmp);
        return Some(cached);
    }

    let _ = fs::remove_file(&tmp);
    let _ = fs::remove_file(&cached);
    None
}

fn find_in_resources(name: &str) -> Option<PathBuf> {
    if let Some(resources) = crate::platform::app_resources_dir() {
        let archive = resources.join(format!("bundles/{}.tar.zst", name));
        if archive.exists() {
            return Some(archive);
        }
    }
    None
}

fn find_in_dev_path(name: &str) -> Option<PathBuf> {
    let archive = PathBuf::from(format!("app/bundles/{}.tar.zst", name));
    if archive.exists() {
        return Some(archive);
    }

    if let Ok(exe) = std::env::current_exe() {
        let dev = exe.parent()?.parent()?.parent()?.parent()?.join("bundles");
        let archive = dev.join(format!("{}.tar.zst", name));
        if archive.exists() {
            return Some(archive);
        }
    }
    None
}

fn copy_dir_recursive(src: &Path, dst: &Path) -> Result<(), String> {
    fs::create_dir_all(dst).map_err(|e| format!("create {}: {}", dst.display(), e))?;
    for entry in fs::read_dir(src).map_err(|e| format!("read {}: {}", src.display(), e))? {
        let entry = entry.map_err(|e| format!("read entry in {}: {}", src.display(), e))?;
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());
        if src_path.is_dir() {
            copy_dir_recursive(&src_path, &dst_path)?;
        } else {
            fs::copy(&src_path, &dst_path)
                .map_err(|e| format!("copy {} to {}: {}", src_path.display(), dst_path.display(), e))?;
        }
    }
    Ok(())
}

fn extract_zst(archive: &PathBuf, dest: &PathBuf, name: &str) -> Result<(), String> {
    let _ = fs::create_dir_all(dest);

    let file = fs::File::open(archive).map_err(|e| format!("cannot open archive: {}", e))?;

    let mut decoder = zstd::Decoder::new(file).map_err(|e| format!("zstd decode error: {}", e))?;

    let mut tar_cmd = Command::new("tar")
        .args(["-xf", "-"])
        .arg("-C")
        .arg(dest)
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::piped())
        .spawn()
        .map_err(|e| format!("tar spawn failed: {}", e))?;

    if let Some(mut stdin) = tar_cmd.stdin.take() {
        std::io::copy(&mut decoder, &mut stdin).map_err(|e| format!("zstd decompression failed: {}", e))?;
        drop(stdin);
    }

    let status = tar_cmd.wait().map_err(|e| format!("tar wait failed: {}", e))?;

    if !status.success() {
        return Err(format!("tar extraction failed for {}", name));
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn metalsharp_wine_binary_accepts_renamed_runtime_binary() {
        let home = test_home("renamed-runtime-binary");
        let bin = home.join(".metalsharp").join("runtime").join("wine").join("bin");
        fs::create_dir_all(&bin).expect("create runtime bin");
        fs::write(bin.join("metalsharp-wine"), b"#!/bin/sh\n").expect("write renamed wine");

        assert_eq!(metalsharp_wine_binary(&home), bin.join("metalsharp-wine"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn gptk_runtime_readiness_requires_framework_contents() {
        let home = test_home("gptk-readiness");
        let gptk_dir = home.join("gptk");
        let pe_dir = gptk_dir.join("x86_64-windows");
        let framework = home.join("external").join("D3DMetal.framework");
        let resources = framework.join("Versions").join("A").join("Resources");
        fs::create_dir_all(&pe_dir).expect("create GPTK PE dir");
        fs::create_dir_all(&resources).expect("create framework resources");
        for dll in ["d3d10.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "nvapi64.dll", "nvngx.dll", "atidxx64.dll"] {
            fs::write(pe_dir.join(dll), b"dll").expect("write GPTK DLL");
        }

        assert!(!gptk_runtime_ready(&gptk_dir, &framework));

        fs::write(framework.join("Versions").join("A").join("D3DMetal"), b"framework").expect("write framework binary");
        fs::write(resources.join("libD3DMetalHelper.dylib"), b"dylib").expect("write framework resource dylib");

        assert!(gptk_runtime_ready(&gptk_dir, &framework));
        fs::remove_file(pe_dir.join("nvngx.dll")).expect("remove nvngx");
        assert!(!gptk_runtime_ready(&gptk_dir, &framework));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn runtime_bundle_preflight_knows_beta7_assets() {
        let mac_assets = MAC_RUNTIME_BUNDLE_ASSETS;
        for expected in [
            "metalsharp_bundle.tar.zst",
            "metalsharp_bundle2.tar.zst",
            "dxmt.tar.zst",
            "gptk.tar.zst",
            "mono-x86.tar.zst",
            "mono-arm64.tar.zst",
            "goldberg.tar.zst",
            "eac-toggle.tar.zst",
            "SteamSetup.exe",
            "steamwebhelper.exe",
        ] {
            assert!(mac_assets.contains(&expected), "missing mac bundle asset {}", expected);
        }

        let linux_assets = LINUX_RUNTIME_BUNDLE_ASSETS;
        for expected in [
            "metalsharp_bundle.tar.zst",
            "metalsharp_bundle2.tar.zst",
            "dxvk.tar.zst",
            "mono-x86.tar.zst",
            "goldberg.tar.zst",
            "eac-toggle.tar.zst",
            "SteamSetup.exe",
            "steamwebhelper.exe",
        ] {
            assert!(linux_assets.contains(&expected), "missing linux bundle asset {}", expected);
        }
    }

    #[test]
    fn install_mtsp_rules_refreshes_stale_installed_copy() {
        let cwd = std::env::current_dir().expect("current dir");
        let repo = test_home("mtsp-rules-source");
        let home = test_home("mtsp-rules-home");
        let source_dir = repo.join("configs");
        let dest_dir = home.join(".metalsharp").join("configs");
        fs::create_dir_all(&source_dir).expect("create source config dir");
        fs::create_dir_all(&dest_dir).expect("create dest config dir");
        fs::write(source_dir.join("mtsp-rules.toml"), "# new rules\n[overrides]\n").expect("write source rules");
        fs::write(dest_dir.join("mtsp-rules.toml"), "# stale rules\n").expect("write stale rules");

        std::env::set_current_dir(&repo).expect("enter source repo");
        let result = install_mtsp_rules(&home);
        std::env::set_current_dir(cwd).expect("restore cwd");

        assert_eq!(result, Ok(true));
        assert_eq!(
            fs::read_to_string(dest_dir.join("mtsp-rules.toml")).expect("read rules"),
            "# new rules\n[overrides]\n"
        );
        assert_eq!(fs::read_to_string(dest_dir.join("mtsp-rules.toml.bak")).expect("read backup"), "# stale rules\n");
        let _ = fs::remove_dir_all(repo);
        let _ = fs::remove_dir_all(home);
    }

    fn test_home(name: &str) -> PathBuf {
        std::env::temp_dir().join(format!(
            "metalsharp-installer-{}-{}-{}",
            name,
            std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).expect("system time").as_nanos()
        ))
    }
}
