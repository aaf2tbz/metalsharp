use serde_json::{json, Value};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

static INSTALLING: AtomicBool = AtomicBool::new(false);

fn mac_cmd(name: &str) -> Command {
    let path = match name {
        "curl" => "/usr/bin/curl",
        "tar" => "/usr/bin/tar",
        "which" => "/usr/bin/which",
        "shasum" => "/usr/bin/shasum",
        "pgrep" => "/usr/bin/pgrep",
        "softwareupdate" => "/usr/sbin/softwareupdate",
        "pkill" => "/usr/bin/pkill",
        "clang" => "/usr/bin/clang",
        _ => name,
    };
    Command::new(path)
}

pub const DXMT_BUNDLED_RUNTIME_VERSION: &str = concat!(env!("CARGO_PKG_VERSION"), "-d3d12-sdk-phase17-pr129");
const DXMT_RUNTIME_MANIFEST: &str = "metalsharp-dxmt-runtime.json";
const DXMT_RUNTIME_SCHEMA: &str = "metalsharp.dxmt-runtime.v1";
const RUNTIME_BUNDLE: &str = "metalsharp-runtime";
const GRAPHICS_DLL_BUNDLE: &str = "metalsharp-graphics-dll";
const ASSETS_BUNDLE: &str = "metalsharp-assets";
const SCRIPTS_TOOLS_BUNDLE: &str = "metalsharp-scripts-tools";
const STEAM_BUNDLE: &str = "metalsharp-steam";
const METALSHARP_NTDLL_HOOK_DLL: &str = "metalsharp_ntdll_hook.dll";
const DXMT_REQUIRED_PE: &[&str] = &[
    "d3d10core.dll",
    "d3d11.dll",
    "d3d12.dll",
    "dxgi.dll",
    "dxgi_dxmt.dll",
    "winemetal.dll",
    "nvapi64.dll",
    "nvngx.dll",
];
const RUNTIME_REQUIRED_ARCHIVE_FILES: &[&str] = &[
    "runtime/wine/bin/metalsharp-wine",
    "runtime/metalsharp-backend",
    "runtime/host/manifest.json",
    "runtime/host/HostRuntimeABI.h",
    "runtime/host/libmetalsharp_host_runtime.dylib",
    "runtime/wine/lib/metalsharp/x86_64-windows/metalsharp_ntdll_hook.dll",
];
const GRAPHICS_REQUIRED_ARCHIVE_FILES: &[&str] = &[
    "Graphics/dll/dxmt/x86_64-unix/winemetal.so",
    "Graphics/dll/dxmt/x86_64-windows/d3d10core.dll",
    "Graphics/dll/dxmt/x86_64-windows/d3d11.dll",
    "Graphics/dll/dxmt/x86_64-windows/d3d12.dll",
    "Graphics/dll/dxmt/x86_64-windows/dxgi.dll",
    "Graphics/dll/dxmt/x86_64-windows/dxgi_dxmt.dll",
    "Graphics/dll/dxmt/x86_64-windows/nvapi64.dll",
    "Graphics/dll/dxmt/x86_64-windows/nvngx.dll",
    "Graphics/dll/dxmt/x86_64-windows/winemetal.dll",
];
const ASSETS_REQUIRED_ARCHIVE_FILES: &[&str] = &[
    "assets/eac-toggle/x86_64-windows/_winhttp.dll",
    "assets/fna-kickstart/kick.bin.osx",
    "assets/fna-kickstart/FNA.dll",
    "assets/fna-kickstart/mscorlib.dll",
    "assets/fna-kickstart/osx/libmonosgen-2.0.1.dylib",
    "assets/fna-kickstart/osx/libSDL2-2.0.0.dylib",
    "assets/fna-kickstart/osx/libFNA3D.0.dylib",
    "assets/fna-kickstart/osx/libFAudio.0.dylib",
    "assets/fna-kickstart/osx/libMonoPosixHelper.dylib",
    "assets/fnalibs/libFNA3D.0.dylib",
    "assets/fnalibs/libSDL2-2.0.0.dylib",
    "assets/fnalibs/libFAudio.0.dylib",
    "assets/fnalibs/libtheorafile.dylib",
    "assets/goldberg/x64/steam_api64.dll",
    "assets/goldberg/x86/steam_api.dll",
    "assets/gptk/external/D3DMetal.framework/Versions/A/D3DMetal",
    "assets/gptk/external/D3DMetal.framework/Versions/A/Resources/libmetalirconverter.dylib",
    "assets/gptk/x86_64-windows/d3d10.dll",
    "assets/gptk/x86_64-windows/d3d11.dll",
    "assets/gptk/x86_64-windows/d3d12.dll",
    "assets/gptk/x86_64-windows/dxgi.dll",
    "assets/gptk/x86_64-windows/nvapi64.dll",
    "assets/gptk/x86_64-windows/nvngx-on-metalfx.dll",
    "assets/mono-arm64/bin/mono-sgen",
    "assets/shims/libsteam_api.dylib",
];
const SCRIPTS_TOOLS_REQUIRED_ARCHIVE_FILES: &[&str] =
    &["scripts/tools/configs/mtsp-rules.toml", "scripts/tools/updater/update.sh"];
const STEAM_REQUIRED_ARCHIVE_FILES: &[&str] =
    &["steam/SteamSetup.exe", "steam/steamwebhelper.exe", "steam/steamwebhelper-wrapper.c"];

const MAC_RUNTIME_BUNDLE_ASSETS: &[&str] = &[
    "metalsharp-runtime.tar.zst",
    "metalsharp-graphics-dll.tar.zst",
    "metalsharp-assets.tar.zst",
    "metalsharp-scripts-tools.tar.zst",
    "metalsharp-steam.tar.zst",
];

fn progress_path() -> PathBuf {
    crate::platform::metalsharp_home_dir().join("install_progress.json")
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
    if crate::platform::current() != crate::platform::HostPlatform::Macos {
        write_progress(
            0,
            0,
            "Unsupported Platform",
            "error",
            "MetalSharp runtime installation is Apple Silicon macOS-only.",
            Some("unsupported_platform"),
        );
        return;
    }

    let home = match dirs::home_dir() {
        Some(h) => h,
        None => {
            write_progress(0, 0, "", "error", "no home directory", Some("no home directory"));
            return;
        },
    };

    let steps: Vec<(&str, Box<dyn Fn(&PathBuf) -> Result<bool, String>>)> = vec![
        ("Rosetta 2", Box::new(|_| install_rosetta())),
        ("System Tools", Box::new(|_| install_xcode_cli())),
        ("Extract Tools (zstd)", Box::new(|_| ensure_zstd())),
        ("Runtime Bundle Downloads", Box::new(ensure_runtime_bundle_assets)),
        ("Runtime Assets", Box::new(install_metalsharp_bundle)),
        ("Host Runtime ABI", Box::new(install_host_runtime)),
        ("Support Assets", Box::new(install_split_assets_bundle)),
        ("Scripts and Tools", Box::new(install_scripts_tools_bundle)),
        ("DXMT Metal Runtime", Box::new(install_dxmt_runtime)),
        ("GPTK D3DMetal Runtime", Box::new(install_gptk_runtime)),
        ("Goldberg Steam Emulator", Box::new(install_goldberg)),
        ("Steam Bridge Shim", Box::new(install_steam_bridge)),
        ("Offline EAC Mode", Box::new(install_eac_toggle)),
        ("Pipeline Rules", Box::new(install_mtsp_rules)),
        ("Mono Configs", Box::new(install_mono_configs)),
        ("Runtime Support", Box::new(|_| install_mono_arm64())),
        ("FNA Shim Precompile", Box::new(|_| crate::mtsp::launcher::precompile_all_fna_shims().map(|_| true))),
    ];

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

    if !check_command("brew") {
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
    MAC_RUNTIME_BUNDLE_ASSETS
}

fn ensure_runtime_bundle_assets(_home: &PathBuf) -> Result<bool, String> {
    let mut downloaded = false;
    let mut missing = Vec::new();

    for asset in runtime_bundle_assets_for_host() {
        let had_local = bundled_file_valid_exists(asset);
        if had_local {
            continue;
        }

        write_progress(3, 14, "Runtime Bundle Downloads", "downloading", &format!("Downloading {}...", asset), None);
        match download_bundled_file(asset) {
            Some(path) if file_nonempty(&path) && bundled_artifact_valid(asset, &path) => {
                downloaded = true;
                write_progress(3, 14, "Runtime Bundle Downloads", "done", &format!("Downloaded {}", asset), None);
            },
            _ => {
                missing.push(*asset);
                write_progress(
                    3,
                    14,
                    "Runtime Bundle Downloads",
                    "error",
                    &format!("Failed to download {}", asset),
                    Some(&format!("Missing bundle: {}", asset)),
                );
            },
        }
    }

    if missing.is_empty() {
        Ok(downloaded)
    } else {
        Err(format!(
            "Missing required runtime bundle asset(s) that could not be downloaded: {}. Please check your internet connection and try again.",
            missing.join(", ")
        ))
    }
}

fn bundled_file_valid_exists(name: &str) -> bool {
    if let Some(resources) = crate::platform::app_resources_dir() {
        let file = resources.join(format!("bundles/{}", name));
        if bundled_artifact_valid(name, &file) {
            return true;
        }
    }

    let dev = PathBuf::from(format!("app/bundles/{}", name));
    if bundled_artifact_valid(name, &dev) {
        return true;
    }

    dirs::home_dir()
        .map(|home| {
            bundled_artifact_valid(
                name,
                &crate::platform::metalsharp_home_dir_for(&home).join("cache").join("bundles").join(name),
            )
        })
        .unwrap_or(false)
}

fn install_rosetta() -> Result<bool, String> {
    let plist = PathBuf::from("/Library/Apple/System/Library/LaunchDaemons/com.apple.oahd.plist");
    if plist.exists() {
        return Ok(false);
    }
    let running = mac_cmd("pgrep").args(["-q", "oahd"]).status().map(|s| s.success()).unwrap_or(false);
    if running {
        return Ok(false);
    }

    let output = mac_cmd("softwareupdate")
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
    if xcode_cli_functional() {
        return Ok(false);
    }

    let output = Command::new("/usr/bin/xcode-select")
        .args(["--install"])
        .output()
        .map_err(|e| format!("failed to run xcode-select: {}", e))?;

    let stderr = String::from_utf8_lossy(&output.stderr);
    if (stderr.contains("already installed") || stderr.contains("command line tools are already installed"))
        && xcode_cli_functional()
    {
        return Ok(false);
    }

    for _ in 0..120 {
        std::thread::sleep(Duration::from_secs(5));
        if xcode_cli_functional() {
            return Ok(true);
        }
    }

    install_xcode_cli_softwareupdate()?;

    if xcode_cli_functional() {
        Ok(true)
    } else {
        Err("Xcode CLI tools installation failed — install manually with: xcode-select --install".into())
    }
}

fn xcode_cli_functional() -> bool {
    let clang = match find_system_command("clang") {
        Some(p) => p,
        None => return false,
    };
    Command::new(&clang)
        .args(["-x", "c", "-c", "-o", "/dev/null", "-"])
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

fn install_xcode_cli_softwareupdate() -> Result<(), String> {
    let list_output = Command::new("/usr/sbin/softwareupdate")
        .args(["--list"])
        .output()
        .map_err(|e| format!("softwareupdate --list failed: {}", e))?;

    let list = String::from_utf8_lossy(&list_output.stdout);
    let label = list
        .lines()
        .find(|line| line.to_lowercase().contains("command line tools") || line.to_lowercase().contains("xcode"))
        .and_then(|line| {
            line.split_whitespace()
                .find(|word| word.starts_with('*') || word.contains("CLTools") || word.contains("Xcode"))
                .map(|w| w.trim_start_matches('*').trim_start_matches('"').trim_end_matches('"').to_string())
                .or_else(|| {
                    let parts: Vec<&str> = line.splitn(2, ',').collect();
                    parts.first().map(|s| {
                        s.trim().trim_start_matches('*').trim_start_matches('"').trim_end_matches('"').to_string()
                    })
                })
        });

    let install_target = match label {
        Some(l) => l,
        None => "*Command Line Tools*".to_string(),
    };

    let install_output = Command::new("/usr/sbin/softwareupdate")
        .args(["--install", &install_target])
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        .output()
        .map_err(|e| format!("softwareupdate --install failed: {}", e))?;

    let combined = format!(
        "{}{}",
        String::from_utf8_lossy(&install_output.stdout),
        String::from_utf8_lossy(&install_output.stderr)
    );
    if !install_output.status.success() && !combined.contains("No updates") && !combined.contains("already installed") {
        return Err(format!("softwareupdate install failed: {}", combined.lines().last().unwrap_or("unknown error")));
    }

    Ok(())
}

fn install_metalsharp_bundle(home: &PathBuf) -> Result<bool, String> {
    let runtime_dir = crate::platform::metalsharp_home_dir_for(&home).join("runtime");
    let _ = fs::create_dir_all(&runtime_dir);

    let bundle = find_bundled_archive(RUNTIME_BUNDLE);
    let ms_wine = metalsharp_wine_binary(home);
    let host_dir = runtime_dir.join("host");
    let backend = runtime_dir.join("metalsharp-backend");
    if ms_wine.exists()
        && host_runtime_ready(&host_dir)
        && file_nonempty(&backend)
        && metalsharp_runtime_lib_ready(&runtime_dir.join("wine"))
        && bundle.as_ref().is_some_and(|archive| split_bundle_current(home, RUNTIME_BUNDLE, archive))
    {
        return Ok(false);
    }

    if let Some(archive) = bundle {
        let tmp_extract = std::env::temp_dir().join("metalsharp-bundle-extract");
        let _ = fs::remove_dir_all(&tmp_extract);
        let _ = fs::create_dir_all(&tmp_extract);
        extract_zst(&archive, &tmp_extract, RUNTIME_BUNDLE)?;

        let wine_dir = runtime_dir.join("wine");
        let source = tmp_extract.join("runtime").join("wine");
        if source.exists() {
            let _ = fs::remove_dir_all(&wine_dir);
            copy_dir_recursive(&source, &wine_dir)?;
        }

        let source_host = tmp_extract.join("runtime").join("host");
        if source_host.exists() {
            let _ = fs::remove_dir_all(&host_dir);
            copy_dir_recursive(&source_host, &host_dir)?;
        }

        let source_backend = tmp_extract.join("runtime").join("metalsharp-backend");
        if source_backend.exists() {
            fs::copy(&source_backend, &backend).map_err(|e| format!("copy runtime backend: {}", e))?;
            make_executable(&backend);
        }
        let _ = fs::remove_dir_all(&tmp_extract);

        let ms_wine = metalsharp_wine_binary(home);
        if ms_wine.exists() {
            if !host_runtime_ready(&host_dir) {
                return Err("MetalSharp runtime bundle installed but host runtime ABI assets are missing".into());
            }
            if !file_nonempty(&backend) {
                return Err("MetalSharp runtime bundle installed but backend executable is missing".into());
            }
            if !metalsharp_runtime_lib_ready(&runtime_dir.join("wine")) {
                return Err("MetalSharp runtime bundle installed but MetalSharp hook DLL is missing".into());
            }

            let wine_check = Command::new(&ms_wine).arg("--version").output();
            match wine_check {
                Ok(o) if o.status.success() => {
                    fix_moltenvk_icd_paths(&runtime_dir.join("wine"));
                    mark_split_bundle_installed(home, RUNTIME_BUNDLE, &archive);
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

    Err("MetalSharp runtime not found — no bundled metalsharp-runtime.tar.zst available".into())
}

fn metalsharp_wine_binary(home: &Path) -> PathBuf {
    crate::platform::runtime_wine_binary(&crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine"))
}

fn install_host_runtime(home: &PathBuf) -> Result<bool, String> {
    let dest = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("host");
    if host_runtime_ready(&dest) {
        return Ok(false);
    }

    let Some(source) = find_packaged_host_runtime() else {
        return install_host_runtime_from_runtime_bundle(&dest);
    };

    install_host_runtime_from_dir(&source, &dest)
}

fn install_host_runtime_from_dir(source: &Path, dest: &Path) -> Result<bool, String> {
    let _ = fs::remove_dir_all(dest);
    fs::create_dir_all(dest).map_err(|e| format!("create host runtime dir: {}", e))?;
    copy_dir_recursive(source, dest)?;

    if host_runtime_ready(dest) {
        Ok(true)
    } else {
        Err("MetalSharp host runtime copied but required ABI files are missing".into())
    }
}

fn install_host_runtime_from_runtime_bundle(dest: &Path) -> Result<bool, String> {
    let archive = find_bundled_archive(RUNTIME_BUNDLE)
        .ok_or_else(|| "MetalSharp host runtime not found — packaged runtime/host assets are missing".to_string())?;
    let tmp_extract = std::env::temp_dir().join("metalsharp-host-runtime-extract");
    let _ = fs::remove_dir_all(&tmp_extract);
    let _ = fs::create_dir_all(&tmp_extract);
    extract_zst(&archive, &tmp_extract, RUNTIME_BUNDLE)?;

    let source = tmp_extract.join("runtime").join("host");
    if !host_runtime_ready(&source) {
        let _ = fs::remove_dir_all(&tmp_extract);
        return Err("MetalSharp host runtime not found in bundled metalsharp-runtime.tar.zst".into());
    }

    let result = install_host_runtime_from_dir(&source, dest);
    let _ = fs::remove_dir_all(&tmp_extract);
    result
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

pub(crate) fn metalsharp_runtime_lib_ready(wine_dir: &Path) -> bool {
    file_nonempty(&wine_dir.join("lib").join("metalsharp").join("x86_64-windows").join(METALSHARP_NTDLL_HOOK_DLL))
}

pub fn moltenvk_ready(wine_dir: &Path) -> bool {
    wine_dir.join("lib").join("wine").join("x86_64-unix").join("libMoltenVK.dylib").is_file()
}

fn fix_moltenvk_icd_paths(wine_dir: &Path) {
    let actual_lib = wine_dir.join("lib").join("wine").join("x86_64-unix").join("libMoltenVK.dylib");
    if !actual_lib.exists() {
        eprintln!("moltenvk: libMoltenVK.dylib not found in runtime — skipping ICD fix");
        return;
    }

    let icd_dir = wine_dir.join("etc").join("vulkan").join("icd.d");
    if !icd_dir.is_dir() {
        eprintln!("moltenvk: ICD directory not found — skipping");
        return;
    }

    let correct_path = format!("{}", actual_lib.to_string_lossy());

    for entry in std::fs::read_dir(&icd_dir).unwrap_or_else(|_| panic!("read_dir")).flatten() {
        let path = entry.path();
        let name = entry.file_name().to_string_lossy().to_string();
        if !name.starts_with("MoltenVK") || !name.ends_with(".json") {
            continue;
        }
        let Ok(data) = fs::read_to_string(&path) else { continue };
        let Ok(mut v) = serde_json::from_str::<serde_json::Value>(&data) else { continue };
        if let Some(icd) = v.get_mut("ICD") {
            if let Some(lib_path) = icd.get_mut("library_path") {
                let current = lib_path.as_str().unwrap_or("");
                if current != correct_path {
                    eprintln!("moltenvk: fixing {} ICD path {} -> {}", name, current, correct_path);
                    *lib_path = serde_json::Value::String(correct_path.clone());
                    if let Err(e) = fs::write(&path, serde_json::to_string_pretty(&v).unwrap_or_default()) {
                        eprintln!("moltenvk: failed to write {}: {}", path.display(), e);
                    }
                }
            }
        }
    }
}

fn split_bundle_marker_dir(home: &Path) -> PathBuf {
    crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("bundle-state")
}

fn split_bundle_marker_path(home: &Path, bundle: &str) -> PathBuf {
    split_bundle_marker_dir(home).join(format!("{}.sha256", bundle))
}

fn split_bundle_current(home: &Path, bundle: &str, archive: &Path) -> bool {
    let Some(hash) = archive_sha256(archive) else {
        return false;
    };
    fs::read_to_string(split_bundle_marker_path(home, bundle)).map(|existing| existing.trim() == hash).unwrap_or(false)
}

fn mark_split_bundle_installed(home: &Path, bundle: &str, archive: &Path) {
    let Some(hash) = archive_sha256(archive) else {
        return;
    };
    let marker_dir = split_bundle_marker_dir(home);
    if fs::create_dir_all(&marker_dir).is_ok() {
        let _ = fs::write(marker_dir.join(format!("{}.sha256", bundle)), hash);
    }
}

fn archive_sha256(path: &Path) -> Option<String> {
    for (cmd, args) in [("/usr/bin/shasum", vec!["-a", "256"]), ("sha256sum", Vec::new())] {
        let Ok(output) = Command::new(cmd).args(args).arg(path).output() else {
            continue;
        };
        if !output.status.success() {
            continue;
        }
        let stdout = String::from_utf8_lossy(&output.stdout);
        let hash = stdout.split_whitespace().next()?.to_string();
        if hash.len() == 64 && hash.chars().all(|c| c.is_ascii_hexdigit()) {
            return Some(hash);
        }
    }
    None
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

fn install_split_assets_bundle(home: &PathBuf) -> Result<bool, String> {
    let archive = find_bundled_archive(ASSETS_BUNDLE)
        .ok_or_else(|| "Support assets not found — metalsharp-assets.tar.zst is missing".to_string())?;
    if split_bundle_current(home, ASSETS_BUNDLE, &archive) {
        return Ok(false);
    }
    let tmp = std::env::temp_dir().join("metalsharp-assets-extract");
    let _ = fs::remove_dir_all(&tmp);
    let _ = fs::create_dir_all(&tmp);
    extract_zst(&archive, &tmp, ASSETS_BUNDLE)?;
    let assets = tmp.join("assets");
    let runtime_dir = crate::platform::metalsharp_home_dir_for(&home).join("runtime");

    let mut changed = false;
    for (src_name, dst_path) in [
        ("mono-x86", runtime_dir.join("mono-x86")),
        ("mono-arm64", runtime_dir.join("mono-arm64")),
        ("dxvk-1.10.3", runtime_dir.join("dxvk-1.10.3")),
        ("goldberg", runtime_dir.join("goldberg")),
        ("eac-toggle", runtime_dir.join("eac-toggle")),
        ("shims", runtime_dir.join("shims")),
        ("fnalibs", runtime_dir.join("fnalibs")),
        ("fna-kickstart", runtime_dir.join("fna-kickstart")),
    ] {
        let src = assets.join(src_name);
        if src.exists() {
            let _ = fs::remove_dir_all(&dst_path);
            copy_dir_recursive(&src, &dst_path)?;
            changed = true;
        }
    }

    let gptk = assets.join("gptk");
    if gptk.exists() {
        let gptk_dir = runtime_dir.join("wine").join("lib").join("gptk");
        let ext_dir = runtime_dir.join("wine").join("lib").join("external");
        for subdir in ["x86_64-windows", "x86_64-unix"] {
            let src = gptk.join(subdir);
            if src.exists() {
                let dst = gptk_dir.join(subdir);
                let _ = fs::remove_dir_all(&dst);
                copy_dir_recursive(&src, &dst)?;
                changed = true;
            }
        }
        let external = gptk.join("external");
        if external.exists() {
            copy_dir_recursive(&external, &ext_dir)?;
            changed = true;
        }
    }

    let wine_etc = assets.join("wine").join("etc");
    if wine_etc.exists() {
        copy_dir_recursive(&wine_etc, &runtime_dir.join("wine").join("etc"))?;
        changed = true;
    }

    let shader_cache = assets.join("shader-cache");
    if shader_cache.exists() {
        copy_dir_recursive(&shader_cache, &crate::platform::metalsharp_home_dir_for(&home).join("shader-cache"))?;
        changed = true;
    }

    let _ = fs::remove_dir_all(&tmp);
    if changed {
        mark_split_bundle_installed(home, ASSETS_BUNDLE, &archive);
    }
    Ok(changed)
}

fn install_scripts_tools_bundle(home: &PathBuf) -> Result<bool, String> {
    let archive = find_bundled_archive(SCRIPTS_TOOLS_BUNDLE)
        .ok_or_else(|| "Scripts/tools bundle not found — metalsharp-scripts-tools.tar.zst is missing".to_string())?;
    if split_bundle_current(home, SCRIPTS_TOOLS_BUNDLE, &archive) {
        return Ok(false);
    }
    let dest = crate::platform::metalsharp_home_dir_for(&home).join("scripts").join("tools");
    let tmp = std::env::temp_dir().join("metalsharp-scripts-tools-extract");
    let _ = fs::remove_dir_all(&tmp);
    let _ = fs::create_dir_all(&tmp);
    extract_zst(&archive, &tmp, SCRIPTS_TOOLS_BUNDLE)?;
    let src = tmp.join("scripts").join("tools");
    let _ = fs::remove_dir_all(&dest);
    copy_dir_recursive(&src, &dest)?;
    let _ = fs::remove_dir_all(&tmp);
    mark_split_bundle_installed(home, SCRIPTS_TOOLS_BUNDLE, &archive);
    Ok(true)
}

fn install_mono_x86_fallback(home: &PathBuf) -> Result<bool, String> {
    let mono_x86 =
        crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("mono-x86").join("bin").join("mono");
    if mono_x86.exists() {
        return Ok(false);
    }
    let bundled = find_bundled_archive("mono-x86");
    let runtime_dir = crate::platform::metalsharp_home_dir_for(&home).join("runtime");
    if let Some(archive) = bundled {
        extract_zst(&archive, &runtime_dir, "mono-x86")?;
        if mono_x86.exists() {
            return Ok(true);
        }
    }
    Err("mono x86 fallback not found".into())
}

fn install_dxvk_fallback(home: &PathBuf) -> Result<bool, String> {
    let dxvk_dir = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("dxvk-1.10.3");
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

    let wine_dir = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
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
    let dxmt_dir = dxmt_runtime_dir_for_home(home);

    let bundled = find_bundled_archive(GRAPHICS_DLL_BUNDLE);
    if dxmt_runtime_current_for_dir(&dxmt_dir)
        && bundled.as_ref().is_some_and(|archive| split_bundle_current(home, GRAPHICS_DLL_BUNDLE, archive))
    {
        return Ok(false);
    }

    let _ = fs::create_dir_all(dxmt_dir.join("x86_64-unix"));
    let _ = fs::create_dir_all(dxmt_dir.join("x86_64-windows"));

    if let Some(archive) = bundled {
        let tmp = std::env::temp_dir().join("metalsharp-dxmt-extract");
        let _ = fs::remove_dir_all(&tmp);
        let _ = fs::create_dir_all(&tmp);
        extract_zst(&archive, &tmp, GRAPHICS_DLL_BUNDLE)?;

        let src_root = tmp.join("Graphics").join("dll").join("dxmt");
        let src_x64_unix = src_root.join("x86_64-unix");
        let src_x64_windows = src_root.join("x86_64-windows");

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

        ensure_dxmt_runtime_compat_files(&dxmt_dir)?;
        write_dxmt_runtime_manifest(&dxmt_dir, "bundled:metalsharp-graphics-dll.tar.zst")?;
        mark_split_bundle_installed(home, GRAPHICS_DLL_BUNDLE, &archive);
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
            ensure_dxmt_runtime_compat_files(&dxmt_dir)?;
            write_dxmt_runtime_manifest(&dxmt_dir, "fallback:~/metalsharp/runtime/dxmt")?;
        }
    }

    if dxmt_runtime_current_for_dir(&dxmt_dir) {
        Ok(true)
    } else {
        Err(format!(
            "DXMT Metal runtime {} not installed — bundle metalsharp-graphics-dll.tar.zst or place files in ~/.metalsharp/runtime/dxmt/",
            DXMT_BUNDLED_RUNTIME_VERSION
        ))
    }
}

fn dxmt_runtime_dir_for_home(home: &Path) -> PathBuf {
    crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine").join("lib").join("dxmt")
}

pub fn dxmt_runtime_current_for_home(home: &Path) -> bool {
    dxmt_runtime_current_for_dir(&dxmt_runtime_dir_for_home(home))
}

pub fn dxmt_runtime_current_for_ms_dir(ms_dir: &Path) -> bool {
    dxmt_runtime_current_for_dir(&ms_dir.join("runtime").join("wine").join("lib").join("dxmt"))
}

pub fn dxmt_runtime_status() -> Value {
    let home = dirs::home_dir().unwrap_or_default();
    let dxmt_dir = dxmt_runtime_dir_for_home(&home);
    let installed_version = dxmt_runtime_installed_version(&dxmt_dir);
    let files_ready = dxmt_runtime_ready(&dxmt_dir);
    let current = files_ready && installed_version.as_deref() == Some(DXMT_BUNDLED_RUNTIME_VERSION);

    json!({
        "current": current,
        "filesReady": files_ready,
        "installedVersion": installed_version,
        "requiredVersion": DXMT_BUNDLED_RUNTIME_VERSION,
        "manifestPath": dxmt_dir.join(DXMT_RUNTIME_MANIFEST).to_string_lossy(),
    })
}

fn dxmt_runtime_current_for_dir(dxmt_dir: &Path) -> bool {
    dxmt_runtime_ready(dxmt_dir)
        && dxmt_runtime_installed_version(dxmt_dir).as_deref() == Some(DXMT_BUNDLED_RUNTIME_VERSION)
}

fn dxmt_runtime_installed_version(dxmt_dir: &Path) -> Option<String> {
    let manifest = fs::read_to_string(dxmt_dir.join(DXMT_RUNTIME_MANIFEST)).ok()?;
    let value: Value = serde_json::from_str(&manifest).ok()?;
    let schema = value.get("schema").and_then(|v| v.as_str())?;
    if schema != DXMT_RUNTIME_SCHEMA {
        return None;
    }
    value.get("version").and_then(|v| v.as_str()).map(str::to_string)
}

fn write_dxmt_runtime_manifest(dxmt_dir: &Path, source: &str) -> Result<(), String> {
    let installed_at =
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or_default();
    let manifest = json!({
        "schema": DXMT_RUNTIME_SCHEMA,
        "version": DXMT_BUNDLED_RUNTIME_VERSION,
        "source": source,
        "installedAtUnix": installed_at,
        "requiredFiles": {
            "x86_64-unix": ["winemetal.so"],
            "x86_64-windows": DXMT_REQUIRED_PE,
        },
    });
    fs::write(dxmt_dir.join(DXMT_RUNTIME_MANIFEST), serde_json::to_string_pretty(&manifest).unwrap_or_default())
        .map_err(|e| format!("write DXMT runtime manifest: {}", e))
}

fn ensure_dxmt_runtime_compat_files(dxmt_dir: &Path) -> Result<(), String> {
    let pe_dir = dxmt_dir.join("x86_64-windows");
    let dxgi = pe_dir.join("dxgi.dll");
    let dxgi_dxmt = pe_dir.join("dxgi_dxmt.dll");

    if !file_nonempty(&dxgi_dxmt) && file_nonempty(&dxgi) {
        fs::copy(&dxgi, &dxgi_dxmt).map_err(|e| {
            format!("copy legacy DXMT dxgi.dll to dxgi_dxmt.dll: {} -> {}: {}", dxgi.display(), dxgi_dxmt.display(), e)
        })?;
    }

    Ok(())
}

fn dxmt_runtime_ready(dxmt_dir: &Path) -> bool {
    let pe_dir = dxmt_dir.join("x86_64-windows");
    file_nonempty(&dxmt_dir.join("x86_64-unix").join("winemetal.so"))
        && DXMT_REQUIRED_PE.iter().all(|dll| file_nonempty(&pe_dir.join(dll)))
}

fn install_gptk_runtime(home: &PathBuf) -> Result<bool, String> {
    let gptk_dir =
        crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine").join("lib").join("gptk");
    let ext_dir =
        crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine").join("lib").join("external");
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
    let required_pe = ["d3d10.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "nvapi64.dll", "nvngx-on-metalfx.dll"];
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
    let goldberg_dir = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("goldberg");
    let x86_dll = goldberg_dir.join("x86").join("steam_api.dll");
    let x64_dll = goldberg_dir.join("x64").join("steam_api64.dll");
    let sc64_dll = goldberg_dir.join("steamclient").join("steamclient64.dll");

    if x86_dll.exists() && x64_dll.exists() && sc64_dll.exists() {
        return Ok(false);
    }

    let _ = fs::create_dir_all(goldberg_dir.join("x86"));
    let _ = fs::create_dir_all(goldberg_dir.join("x64"));
    let _ = fs::create_dir_all(goldberg_dir.join("steamclient"));

    let bundled = find_bundled_archive("goldberg");
    if let Some(archive) = bundled {
        let tmp = std::env::temp_dir().join("metalsharp-goldberg-extract");
        let _ = fs::remove_dir_all(&tmp);
        let _ = fs::create_dir_all(&tmp);
        extract_zst(&archive, &tmp, "goldberg")?;

        let src_x86 = tmp.join("x86");
        let src_x64 = tmp.join("x64");
        let src_sc = tmp.join("steamclient");

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
        if src_sc.exists() {
            for entry in fs::read_dir(&src_sc).map_err(|e| format!("read steamclient: {}", e))? {
                let entry = entry.map_err(|e| e.to_string())?;
                let _ = fs::copy(entry.path(), goldberg_dir.join("steamclient").join(entry.file_name()));
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

fn install_steam_bridge(home: &PathBuf) -> Result<bool, String> {
    let bridge_dir = crate::platform::metalsharp_home_dir_for(home).join("runtime").join("steam-bridge");
    let shim_dst = bridge_dir.join("libsteam_api.dylib");

    let wine_dir = crate::platform::metalsharp_home_dir_for(home).join("runtime").join("wine");
    fix_moltenvk_icd_paths(&wine_dir);
    if !moltenvk_ready(&wine_dir) {
        eprintln!("steam-bridge: warning — MoltenVK not found in Wine runtime, CEF webhelper may not render");
    }

    if shim_dst.exists() {
        return Ok(false);
    }

    let _ = fs::create_dir_all(&bridge_dir);

    let shims_dylib =
        crate::platform::metalsharp_home_dir_for(home).join("runtime").join("shims").join("libsteam_api.dylib");
    if shims_dylib.exists() {
        fs::copy(&shims_dylib, &shim_dst).map_err(|e| format!("copy steam bridge shim: {}", e))?;
    }

    if shim_dst.exists() {
        Ok(true)
    } else {
        Ok(false)
    }
}

fn install_eac_toggle(home: &PathBuf) -> Result<bool, String> {
    let eac_dir = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("eac-toggle");
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
    let dest = crate::platform::metalsharp_home_dir_for(&home).join("configs").join("mtsp-rules.toml");
    let mut candidates = vec![
        PathBuf::from("configs/mtsp-rules.toml"),
        crate::platform::metalsharp_home_dir_for(&home)
            .join("scripts")
            .join("tools")
            .join("configs")
            .join("mtsp-rules.toml"),
        home.join("metalsharp").join("configs").join("mtsp-rules.toml"),
        home.join("repos").join("metalsharp").join("configs").join("mtsp-rules.toml"),
    ];

    if let Ok(exe) = std::env::current_exe() {
        if let Some(mut dir) = exe.parent() {
            for _ in 0..8 {
                candidates.push(dir.join("configs").join("mtsp-rules.toml"));
                candidates.push(dir.join("scripts").join("tools").join("configs").join("mtsp-rules.toml"));
                match dir.parent() {
                    Some(p) => dir = p,
                    None => break,
                }
            }
        }
    }

    install_mtsp_rules_from_candidates(&dest, &candidates)
}

fn install_mtsp_rules_from_candidates(dest: &Path, candidates: &[PathBuf]) -> Result<bool, String> {
    for src in candidates {
        if src.exists() {
            if let Ok(contents) = fs::read_to_string(src) {
                if let Ok(existing) = fs::read_to_string(dest) {
                    if existing == contents {
                        return Ok(false);
                    }
                    let backup = dest.with_extension("toml.bak");
                    let _ = fs::write(&backup, existing);
                }
                fs::create_dir_all(dest.parent().unwrap()).map_err(|e| format!("create MTSP config dir: {}", e))?;
                fs::write(dest, &contents).map_err(|e| format!("write mtsp-rules.toml: {}", e))?;
                if fs::read_to_string(dest).ok().as_deref() == Some(contents.as_str()) {
                    return Ok(true);
                }
                return Err("mtsp-rules.toml was written but could not be verified".into());
            }
        }
    }

    Ok(false)
}

fn install_mono_configs(home: &PathBuf) -> Result<bool, String> {
    let configs_dir = crate::platform::metalsharp_home_dir_for(&home).join("configs");
    let _ = fs::create_dir_all(&configs_dir);

    let config_files =
        ["terraria-mono.config", "celeste-x86-mono.config", "stardew-mono.config", "generic-fna-mono.config"];
    let mut any_installed = false;

    for name in &config_files {
        let dest = configs_dir.join(name);
        if dest.exists() {
            continue;
        }

        let mut candidates = vec![
            PathBuf::from(format!("configs/{}", name)),
            crate::platform::metalsharp_home_dir_for(&home).join("configs").join(name),
            home.join("repos").join("metalsharp").join("configs").join(name),
        ];

        if let Ok(exe) = std::env::current_exe() {
            if let Some(mut dir) = exe.parent() {
                for _ in 0..8 {
                    candidates.push(dir.join("configs").join(name));
                    candidates.push(dir.join("scripts").join("tools").join("configs").join(name));
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

    let installer = crate::platform::metalsharp_home_dir_for(&home).join("SteamSetup.exe");

    let _ = fs::remove_file(&installer);

    let output = mac_cmd("curl")
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

    let prefix = crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam");
    let _ = fs::create_dir_all(&prefix);

    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
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
            let steam_dir = crate::platform::metalsharp_home_dir_for(&home)
                .join("prefix-steam")
                .join("drive_c")
                .join("Program Files (x86)")
                .join("Steam");
            crate::steam::deploy_steamwebhelper_wrapper(&steam_dir);
            return Ok(true);
        }
    }

    if steam_exe.exists() {
        let steam_dir = crate::platform::metalsharp_home_dir_for(&home)
            .join("prefix-steam")
            .join("drive_c")
            .join("Program Files (x86)")
            .join("Steam");
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
        "brew" => vec![PathBuf::from("/opt/homebrew/bin/brew"), PathBuf::from("/usr/local/bin/brew")],
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

pub fn ensure_zstd() -> Result<bool, String> {
    if check_command("unzstd") {
        return Ok(false);
    }
    brew_install("zstd")
}

fn install_mono_arm64() -> Result<bool, String> {
    if check_command("mono") {
        return Ok(false);
    }

    let home = dirs::home_dir().ok_or("Cannot find home directory")?;
    let mono_arm64 =
        crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("mono-arm64").join("bin").join("mono");
    if mono_arm64.exists() {
        return Ok(false);
    }

    let bundled = find_bundled_archive("mono-arm64");
    let runtime_dir = crate::platform::metalsharp_home_dir_for(&home).join("runtime");
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
    let output = mac_cmd("which").arg("brew").output().ok();
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

    if let Some(found) =
        candidates.into_iter().find(|c| c.as_ref().is_some_and(|path| bundled_artifact_valid(name, path))).flatten()
    {
        return Some(found);
    }

    download_from_github_release(&format!("{}.tar.zst", name))
}

fn download_bundled_file(name: &str) -> Option<PathBuf> {
    let cache_dir = crate::platform::metalsharp_home_dir().join("cache").join("bundles");
    let _ = fs::create_dir_all(&cache_dir);
    let cached = cache_dir.join(name);
    let tmp = cache_dir.join(format!("{}.download", name));

    if file_nonempty(&cached) && bundled_artifact_valid(name, &cached) {
        return Some(cached);
    }

    let url = format!("https://github.com/aaf2tbz/metalsharp/releases/download/bundles/{}", name);

    let _ = fs::remove_file(&tmp);

    for retry in 0..3 {
        let output = mac_cmd("curl")
            .args([
                "--fail",
                "--location",
                "--silent",
                "--show-error",
                "--retry",
                "2",
                "--connect-timeout",
                "30",
                "--max-time",
                "600",
                "-o",
            ])
            .arg(&tmp)
            .arg(&url)
            .output();

        match output {
            Ok(o) if o.status.success() && file_nonempty(&tmp) => {
                if bundled_artifact_valid(name, &tmp) {
                    if fs::rename(&tmp, &cached).or_else(|_| fs::copy(&tmp, &cached).map(|_| ())).is_ok() {
                        let _ = fs::remove_file(&tmp);
                        return Some(cached);
                    }
                    let _ = fs::remove_file(&tmp);
                    return None;
                } else {
                    let _ = fs::remove_file(&tmp);
                    if retry < 2 {
                        std::thread::sleep(std::time::Duration::from_secs(1));
                    }
                }
            },
            Ok(_) => {
                let _ = fs::remove_file(&tmp);
                if retry < 2 {
                    std::thread::sleep(std::time::Duration::from_secs(2));
                }
            },
            Err(_) => {
                let _ = fs::remove_file(&tmp);
                if retry < 2 {
                    std::thread::sleep(std::time::Duration::from_secs(2));
                }
            },
        }
    }

    None
}

fn find_bundled_file(name: &str) -> Option<PathBuf> {
    if let Some(resources) = crate::platform::app_resources_dir() {
        let file = resources.join(format!("bundles/{}", name));
        if file.exists() && bundled_artifact_valid(name, &file) {
            return Some(file);
        }
    }

    let dev = PathBuf::from(format!("app/bundles/{}", name));
    if dev.exists() && bundled_artifact_valid(name, &dev) {
        return Some(dev);
    }

    download_bundled_file(name)
}

fn download_from_github_release(filename: &str) -> Option<PathBuf> {
    download_bundled_file(filename)
}

fn bundled_artifact_valid(name: &str, path: &Path) -> bool {
    if !file_nonempty(path) {
        return false;
    }

    if name == RUNTIME_BUNDLE || name == "metalsharp-runtime.tar.zst" {
        return archive_required_files_valid(path, RUNTIME_REQUIRED_ARCHIVE_FILES);
    }

    if name == GRAPHICS_DLL_BUNDLE || name == "metalsharp-graphics-dll.tar.zst" {
        return archive_required_files_valid(path, GRAPHICS_REQUIRED_ARCHIVE_FILES);
    }

    if name == ASSETS_BUNDLE || name == "metalsharp-assets.tar.zst" {
        return archive_required_files_valid(path, ASSETS_REQUIRED_ARCHIVE_FILES);
    }

    if name == SCRIPTS_TOOLS_BUNDLE || name == "metalsharp-scripts-tools.tar.zst" {
        return archive_required_files_valid(path, SCRIPTS_TOOLS_REQUIRED_ARCHIVE_FILES);
    }

    if name == STEAM_BUNDLE || name == "metalsharp-steam.tar.zst" {
        return archive_required_files_valid(path, STEAM_REQUIRED_ARCHIVE_FILES);
    }

    true
}

fn archive_required_files_valid(path: &Path, required_files: &[&str]) -> bool {
    let tmp = std::env::temp_dir().join(format!(
        "metalsharp-archive-validate-{}-{}",
        std::process::id(),
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0)
    ));
    let _ = fs::remove_dir_all(&tmp);
    if fs::create_dir_all(&tmp).is_err() {
        return false;
    }

    let file = match fs::File::open(path) {
        Ok(f) => f,
        Err(_) => {
            let _ = fs::remove_dir_all(&tmp);
            return false;
        },
    };
    let mut decoder = match zstd::Decoder::new(file) {
        Ok(d) => d,
        Err(_) => {
            let _ = fs::remove_dir_all(&tmp);
            return false;
        },
    };

    let mut tar_cmd = match mac_cmd("tar")
        .args(["-xf", "-"])
        .arg("-C")
        .arg(&tmp)
        .args(required_files)
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
    {
        Ok(c) => c,
        Err(_) => {
            let _ = fs::remove_dir_all(&tmp);
            return false;
        },
    };

    if let Some(mut stdin) = tar_cmd.stdin.take() {
        let _ = std::io::copy(&mut decoder, &mut stdin);
    }

    let ready = tar_cmd.wait().map(|s| s.success()).unwrap_or(false)
        && required_files.iter().all(|required| file_nonempty(&tmp.join(required)));
    let _ = fs::remove_dir_all(&tmp);
    ready
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
        let file_type =
            fs::symlink_metadata(&src_path).map_err(|e| format!("metadata {}: {}", src_path.display(), e))?.file_type();
        if file_type.is_symlink() {
            copy_symlink_or_target(&src_path, &dst_path)?;
        } else if file_type.is_dir() {
            copy_dir_recursive(&src_path, &dst_path)?;
        } else {
            fs::copy(&src_path, &dst_path)
                .map_err(|e| format!("copy {} to {}: {}", src_path.display(), dst_path.display(), e))?;
        }
    }
    Ok(())
}

#[cfg(unix)]
fn copy_symlink_or_target(src: &Path, dst: &Path) -> Result<(), String> {
    let target = fs::read_link(src).map_err(|e| format!("read symlink {}: {}", src.display(), e))?;
    let _ = fs::remove_file(dst);
    std::os::unix::fs::symlink(&target, dst)
        .map_err(|e| format!("copy symlink {} to {}: {}", src.display(), dst.display(), e))
}

#[cfg(not(unix))]
fn copy_symlink_or_target(src: &Path, dst: &Path) -> Result<(), String> {
    fs::copy(src, dst).map(|_| ()).map_err(|e| format!("copy {} to {}: {}", src.display(), dst.display(), e))
}

fn extract_zst(archive: &PathBuf, dest: &PathBuf, name: &str) -> Result<(), String> {
    let _ = fs::create_dir_all(dest);

    let file = fs::File::open(archive).map_err(|e| format!("cannot open archive: {}", e))?;

    let mut decoder = zstd::Decoder::new(file).map_err(|e| format!("zstd decode error: {}", e))?;

    let mut tar_cmd = mac_cmd("tar")
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
        let bin = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine").join("bin");
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
        for dll in ["d3d10.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "nvapi64.dll", "nvngx-on-metalfx.dll"] {
            fs::write(pe_dir.join(dll), b"dll").expect("write GPTK DLL");
        }

        assert!(!gptk_runtime_ready(&gptk_dir, &framework));

        fs::write(framework.join("Versions").join("A").join("D3DMetal"), b"framework").expect("write framework binary");
        fs::write(resources.join("libD3DMetalHelper.dylib"), b"dylib").expect("write framework resource dylib");

        assert!(gptk_runtime_ready(&gptk_dir, &framework));
        fs::remove_file(pe_dir.join("nvngx-on-metalfx.dll")).expect("remove MetalFX NVNGX bridge");
        assert!(!gptk_runtime_ready(&gptk_dir, &framework));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn runtime_bundle_preflight_knows_beta7_assets() {
        let mac_assets = MAC_RUNTIME_BUNDLE_ASSETS;
        for expected in [
            "metalsharp-runtime.tar.zst",
            "metalsharp-graphics-dll.tar.zst",
            "metalsharp-assets.tar.zst",
            "metalsharp-scripts-tools.tar.zst",
            "metalsharp-steam.tar.zst",
        ] {
            assert!(mac_assets.contains(&expected), "missing mac bundle asset {}", expected);
        }
    }

    #[test]
    fn graphics_bundle_layout_matches_release_manifest() {
        let manifest = include_str!("../../../tools/bundles/asset-manifest.tsv");
        let graphics_row = manifest
            .lines()
            .find(|line| line.starts_with("metalsharp-graphics-dll.tar.zst\t"))
            .expect("metalsharp-graphics-dll.tar.zst release manifest row");
        let fields: Vec<&str> = graphics_row.split('\t').collect();

        assert_eq!(fields.get(1).copied(), Some("Graphics/dll"));
    }

    #[test]
    fn bundle_validation_rejects_stale_known_archives() {
        let home = test_home("stale-known-bundle");
        fs::create_dir_all(&home).expect("create test dir");
        let stale = home.join("metalsharp-runtime.tar.zst");
        fs::write(&stale, b"old runtime bundle").expect("write stale archive");

        assert!(!bundled_artifact_valid("metalsharp-runtime", &stale));
        assert!(!bundled_artifact_valid("metalsharp-runtime.tar.zst", &stale));
        assert!(!bundled_artifact_valid("metalsharp-graphics-dll", &stale));
        assert!(!bundled_artifact_valid("metalsharp-graphics-dll.tar.zst", &stale));
        assert!(!bundled_artifact_valid("metalsharp-assets", &stale));
        assert!(!bundled_artifact_valid("metalsharp-assets.tar.zst", &stale));
        assert!(!bundled_artifact_valid("metalsharp-scripts-tools", &stale));
        assert!(!bundled_artifact_valid("metalsharp-scripts-tools.tar.zst", &stale));
        assert!(!bundled_artifact_valid("metalsharp-steam", &stale));
        assert!(!bundled_artifact_valid("metalsharp-steam.tar.zst", &stale));
        assert!(bundled_artifact_valid("unmanaged-test-asset.bin", &stale));

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn dxmt_readiness_requires_current_bundled_manifest() {
        let home = test_home("dxmt-current-manifest");
        let dxmt_dir = dxmt_runtime_dir_for_home(&home);
        write_dxmt_runtime_files(&dxmt_dir);

        assert!(dxmt_runtime_ready(&dxmt_dir));
        assert!(!dxmt_runtime_current_for_dir(&dxmt_dir));

        write_dxmt_runtime_manifest(&dxmt_dir, "test").expect("write current DXMT manifest");
        assert!(dxmt_runtime_current_for_dir(&dxmt_dir));

        fs::write(dxmt_dir.join(DXMT_RUNTIME_MANIFEST), br#"{"schema":"metalsharp.dxmt-runtime.v1","version":"old"}"#)
            .expect("write stale manifest");
        assert!(!dxmt_runtime_current_for_dir(&dxmt_dir));

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn dxmt_install_normalizes_legacy_bundle_dxgi_bridge() {
        let home = test_home("dxmt-legacy-dxgi-bridge");
        let dxmt_dir = dxmt_runtime_dir_for_home(&home);
        let unix_dir = dxmt_dir.join("x86_64-unix");
        let pe_dir = dxmt_dir.join("x86_64-windows");
        fs::create_dir_all(&unix_dir).expect("create DXMT unix dir");
        fs::create_dir_all(&pe_dir).expect("create DXMT PE dir");
        fs::write(unix_dir.join("winemetal.so"), b"so").expect("write winemetal");
        for dll in DXMT_REQUIRED_PE.iter().copied().filter(|dll| *dll != "dxgi_dxmt.dll") {
            fs::write(pe_dir.join(dll), dll.as_bytes()).expect("write DXMT DLL");
        }

        assert!(!dxmt_runtime_ready(&dxmt_dir));

        ensure_dxmt_runtime_compat_files(&dxmt_dir).expect("normalize legacy DXMT bundle");

        assert!(dxmt_runtime_ready(&dxmt_dir));
        assert_eq!(
            fs::read(pe_dir.join("dxgi_dxmt.dll")).expect("read dxgi_dxmt"),
            fs::read(pe_dir.join("dxgi.dll")).expect("read dxgi")
        );
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn install_mtsp_rules_refreshes_stale_installed_copy() {
        let repo = test_home("mtsp-rules-source");
        let home = test_home("mtsp-rules-home");
        let source_dir = repo.join("configs");
        let dest_dir = crate::platform::metalsharp_home_dir_for(&home).join("configs");
        let dest = dest_dir.join("mtsp-rules.toml");
        fs::create_dir_all(&source_dir).expect("create source config dir");
        fs::create_dir_all(&dest_dir).expect("create dest config dir");
        fs::write(source_dir.join("mtsp-rules.toml"), "# new rules\n[overrides]\n").expect("write source rules");
        fs::write(&dest, "# stale rules\n").expect("write stale rules");

        let result = install_mtsp_rules_from_candidates(&dest, &[source_dir.join("mtsp-rules.toml")]);

        assert_eq!(result, Ok(true));
        assert_eq!(fs::read_to_string(&dest).expect("read rules"), "# new rules\n[overrides]\n");
        assert_eq!(fs::read_to_string(dest_dir.join("mtsp-rules.toml.bak")).expect("read backup"), "# stale rules\n");
        let _ = fs::remove_dir_all(repo);
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn install_mtsp_rules_uses_installed_scripts_tools_bundle_copy() {
        let home = test_home("mtsp-rules-installed-tools");
        let source_dir = crate::platform::metalsharp_home_dir_for(&home).join("scripts").join("tools").join("configs");
        let dest_dir = crate::platform::metalsharp_home_dir_for(&home).join("configs");
        fs::create_dir_all(&source_dir).expect("create installed scripts tools config dir");
        fs::write(source_dir.join("mtsp-rules.toml"), "# installed rules\n[profiles]\n").expect("write source rules");

        let result = install_mtsp_rules(&home);

        assert_eq!(result, Ok(true));
        assert_eq!(
            fs::read_to_string(dest_dir.join("mtsp-rules.toml")).expect("read rules"),
            "# installed rules\n[profiles]\n"
        );
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn install_mtsp_rules_allows_missing_optional_rules() {
        let home = test_home("mtsp-rules-missing-home");
        let dest = crate::platform::metalsharp_home_dir_for(&home).join("configs").join("mtsp-rules.toml");

        let result = install_mtsp_rules_from_candidates(&dest, &[]);

        assert_eq!(result, Ok(false));
        assert!(!dest.exists());
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

    #[test]
    fn bundled_file_valid_exists_rejects_empty_files() {
        let home = test_home("empty-file-validation");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let bundles_dir = ms_dir.join("cache").join("bundles");
        fs::create_dir_all(&bundles_dir).expect("create bundles dir");

        let empty_file = bundles_dir.join("metalsharp-runtime.tar.zst");
        fs::write(&empty_file, b"").expect("create empty file");

        assert!(!bundled_file_valid_exists("metalsharp-runtime.tar.zst"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn bundled_file_valid_exists_rejects_nonexistent_files() {
        assert!(!bundled_file_valid_exists("metalsharp-runtime.tar.zst"));
    }

    #[test]
    fn bundled_file_valid_exists_rejects_invalid_archives() {
        let home = test_home("invalid-archive-validation");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let bundles_dir = ms_dir.join("cache").join("bundles");
        fs::create_dir_all(&bundles_dir).expect("create bundles dir");

        let invalid_file = bundles_dir.join("metalsharp-runtime.tar.zst");
        fs::write(&invalid_file, b"not a valid zst archive").expect("create invalid archive");

        assert!(!bundled_file_valid_exists("metalsharp-runtime.tar.zst"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn bundled_artifact_valid_accepts_non_bundle_files() {
        let home = test_home("non-bundle-file");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let bundles_dir = ms_dir.join("cache").join("bundles");
        fs::create_dir_all(&bundles_dir).expect("create bundles dir");

        let non_bundle = bundles_dir.join("other-file.bin");
        fs::write(&non_bundle, b"some content").expect("create non-bundle file");

        assert!(bundled_artifact_valid("other-file.bin", &non_bundle));
        let _ = fs::remove_dir_all(home);
    }

    fn write_dxmt_runtime_files(dxmt_dir: &Path) {
        let unix_dir = dxmt_dir.join("x86_64-unix");
        let pe_dir = dxmt_dir.join("x86_64-windows");
        fs::create_dir_all(&unix_dir).expect("create DXMT unix dir");
        fs::create_dir_all(&pe_dir).expect("create DXMT PE dir");
        fs::write(unix_dir.join("winemetal.so"), b"so").expect("write winemetal");
        for dll in DXMT_REQUIRED_PE {
            fs::write(pe_dir.join(dll), b"dll").expect("write DXMT DLL");
        }
    }
}
