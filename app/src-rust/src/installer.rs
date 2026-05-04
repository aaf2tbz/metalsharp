use serde_json::{json, Value};
use std::fs;
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

static INSTALLING: AtomicBool = AtomicBool::new(false);

fn progress_path() -> PathBuf {
    dirs::home_dir()
        .unwrap_or_default()
        .join(".metalsharp")
        .join("install_progress.json")
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

    if !INSTALLING.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst).is_ok() {
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
        }
    };

    let steps: Vec<(&str, Box<dyn Fn(&PathBuf) -> Result<bool, String>>)> = vec![
        ("Rosetta 2", Box::new(|_| install_rosetta())),
        ("Xcode CLI Tools", Box::new(|_| install_xcode_cli())),
        ("CrossOver", Box::new(|home| install_crossover(home))),
        ("Game Porting Toolkit", Box::new(|home| install_gptk(home))),
        ("Mono x86 Runtime", Box::new(|home| install_mono_x86(home))),
        ("DXVK 1.10.3", Box::new(|home| install_dxvk(home))),
        ("SteamCMD", Box::new(|home| install_steamcmd(home))),
        ("Mono (arm64)", Box::new(|_| install_mono_arm64())),
        ("Wine Devel", Box::new(|_| install_wine_devel())),
        ("MoltenVK", Box::new(|_| install_moltenvk())),
    ];

    let total = steps.len();

    write_progress(0, total, "Starting...", "starting", "Preparing to install dependencies...", None);

    for (i, (name, installer)) in steps.iter().enumerate() {
        let step_num = i + 1;
        write_progress(step_num, total, name, "installing", &format!("Installing {}...", name), None);

        match installer(&home) {
            Ok(false) => {
                write_progress(step_num, total, name, "done", &format!("{} already installed — skipping", name), None);
            }
            Ok(true) => {
                write_progress(step_num, total, name, "done", &format!("{} installed!", name), None);
            }
            Err(e) => {
                let is_required = i < 7;
                if is_required {
                    write_progress(step_num, total, name, "error", &format!("{} failed: {}", name, e), Some(&e));
                    return;
                } else {
                    write_progress(step_num, total, name, "skipped", &format!("{} skipped: {}", name, e), None);
                }
            }
        }

        std::thread::sleep(Duration::from_millis(200));
    }

    write_progress(total, total, "Complete", "complete", "All dependencies installed!", None);
}

fn install_rosetta() -> Result<bool, String> {
    let plist = PathBuf::from("/Library/Apple/System/Library/LaunchDaemons/com.apple.oahd.plist");
    if plist.exists() {
        return Ok(false);
    }
    let running = Command::new("pgrep")
        .args(["-q", "oahd"])
        .status()
        .map(|s| s.success())
        .unwrap_or(false);
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

fn install_crossover(_home: &PathBuf) -> Result<bool, String> {
    let crossover_wine = PathBuf::from(
        "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-unix/wine"
    );
    if crossover_wine.exists() {
        return Ok(false);
    }

    let bundled = find_bundled_archive("crossover");
    if let Some(archive) = bundled {
        extract_to_applications(&archive, "CrossOver.app")?;
        if crossover_wine.exists() {
            return Ok(true);
        }
    }

    brew_cask_install("crossover")?;

    if !crossover_wine.exists() {
        return Err("CrossOver wine binary not found after installation".into());
    }
    Ok(true)
}

fn install_gptk(_home: &PathBuf) -> Result<bool, String> {
    let gptk_wine = PathBuf::from(
        "/Applications/Game Porting Toolkit.app/Contents/Resources/wine/bin/wine64"
    );
    if gptk_wine.exists() {
        return Ok(false);
    }

    let bundled = find_bundled_archive("gptk");
    if let Some(archive) = bundled {
        extract_to_applications(&archive, "Game Porting Toolkit.app")?;
        if gptk_wine.exists() {
            return Ok(true);
        }
    }

    brew_cask_install("gcenx/wine/game-porting-toolkit")?;

    if !gptk_wine.exists() {
        return Err("GPTK wine binary not found after installation".into());
    }
    Ok(true)
}

fn install_mono_x86(home: &PathBuf) -> Result<bool, String> {
    let mono_x86 = home.join(".metalsharp").join("runtime").join("mono-x86").join("bin").join("mono");
    if mono_x86.exists() {
        return Ok(false);
    }

    let bundled = find_bundled_archive("mono-x86");
    let runtime_dir = home.join(".metalsharp").join("runtime");
    let _ = fs::create_dir_all(&runtime_dir);

    if let Some(archive) = bundled {
        extract_zst(&archive, &runtime_dir, "mono-x86")?;
        if mono_x86.exists() {
            return Ok(true);
        }
    }

    Err("mono x86 not found - no bundled archive available and cannot auto-install".into())
}

fn install_dxvk(home: &PathBuf) -> Result<bool, String> {
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
        if !src.exists() {
            return Err("DXVK archive missing dxvk-1.10.3 directory".into());
        }

        let x32 = src.join("x32");
        let x64 = src.join("x64");
        if x32.exists() {
            let _ = fs::create_dir_all(dxvk_dir.join("x32"));
            for entry in fs::read_dir(&x32).map_err(|e| format!("read x32: {}", e))? {
                let entry = entry.map_err(|e| e.to_string())?;
                let _ = fs::copy(entry.path(), dxvk_dir.join("x32").join(entry.file_name()));
            }
        }
        if x64.exists() {
            let _ = fs::create_dir_all(dxvk_dir.join("x64"));
            for entry in fs::read_dir(&x64).map_err(|e| format!("read x64: {}", e))? {
                let entry = entry.map_err(|e| e.to_string())?;
                let _ = fs::copy(entry.path(), dxvk_dir.join("x64").join(entry.file_name()));
            }
        }

        let _ = fs::remove_dir_all(&tmp);

        if dxvk_dir.join("x32").join("d3d11.dll").exists() {
            return Ok(true);
        }
    }

    let url = "https://github.com/doitsujin/dxvk/releases/download/v1.10.3/dxvk-1.10.3.tar.gz";
    let tar_path = dxvk_dir.join("dxvk.tar.gz");

    let output = Command::new("curl")
        .args(["-sL", "-o"])
        .arg(&tar_path)
        .arg(url)
        .output()
        .map_err(|e| format!("curl failed: {}", e))?;

    if !output.status.success() {
        return Err("failed to download DXVK".into());
    }

    let tar_output = Command::new("tar")
        .args(["-xzf"])
        .arg(&tar_path)
        .arg("-C")
        .arg(&dxvk_dir)
        .output()
        .map_err(|e| format!("tar failed: {}", e))?;

    let _ = fs::remove_file(&tar_path);

    if !tar_output.status.success() {
        return Err("failed to extract DXVK".into());
    }

    if !dxvk_dir.join("dxvk-1.10.3").join("x32").join("d3d11.dll").exists() {
        return Err("DXVK d3d11.dll not found after extraction".into());
    }
    Ok(true)
}

fn install_steamcmd(home: &PathBuf) -> Result<bool, String> {
    let steamcmd_dir = home.join("steamcmd");
    let steamcmd_sh = steamcmd_dir.join("steamcmd.sh");
    if steamcmd_sh.exists() {
        return Ok(false);
    }

    let _ = fs::create_dir_all(&steamcmd_dir);

    let bundled = find_bundled_archive("steamcmd");
    if let Some(archive) = bundled {
        let home = dirs::home_dir().ok_or("Cannot find home directory")?;
        extract_zst(&archive, &home, "steamcmd")?;
        if steamcmd_sh.exists() {
            return Ok(true);
        }
    }

    let tar_path = steamcmd_dir.join("steamcmd.tar.gz");
    let output = Command::new("curl")
        .args(["-sL", "-o"])
        .arg(&tar_path)
        .arg("https://steamcdn-a.akamaihd.net/client/installer/steamcmd_osx.tar.gz")
        .output()
        .map_err(|e| format!("curl failed: {}", e))?;

    if !output.status.success() {
        return Err("failed to download steamcmd".into());
    }

    let tar_output = Command::new("tar")
        .args(["-xzf"])
        .arg(&tar_path)
        .arg("-C")
        .arg(&steamcmd_dir)
        .output();

    let _ = fs::remove_file(&tar_path);

    match tar_output {
        Ok(t) if t.status.success() => Ok(true),
        _ => Err("failed to extract steamcmd".into()),
    }
}

fn install_windows_steam(home: &PathBuf) -> Result<bool, String> {
    let steam_exe = home.join(".metalsharp")
        .join("prefix-steam-cx")
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("Steam.exe");
    if steam_exe.exists() {
        return Ok(false);
    }

    let crossover_wine = PathBuf::from(
        "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-unix/wine"
    );
    if !crossover_wine.exists() {
        return Err("CrossOver Wine not found — cannot install Steam".into());
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

    let prefix = home.join(".metalsharp").join("prefix-steam-cx");
    let _ = fs::create_dir_all(&prefix);

    let cx_root = PathBuf::from("/Applications/CrossOver.app/Contents/SharedSupport/CrossOver");

    let _ = Command::new(&crossover_wine)
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("CX_ROOT", cx_root.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .arg("wineboot")
        .arg("--init")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    let _ = Command::new(&crossover_wine)
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("CX_ROOT", cx_root.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .arg(&installer)
        .args(["/S"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .map_err(|e| format!("Steam install spawn failed: {}", e))?;

    for _ in 0..90 {
        std::thread::sleep(Duration::from_secs(2));
        if steam_exe.exists() {
            return Ok(true);
        }
    }

    if steam_exe.exists() {
        Ok(true)
    } else {
        Err("Steam.exe not found after installation — may need manual install".into())
    }
}

fn check_command(cmd: &str) -> bool {
    let candidates = match cmd {
        "which" => vec![PathBuf::from("/usr/bin/which")],
        "mono" => vec![PathBuf::from("/opt/homebrew/bin/mono"), PathBuf::from("/usr/local/bin/mono")],
        _ => vec![PathBuf::from(cmd)],
    };
    for c in &candidates {
        if c.exists() {
            return true;
        }
    }
    Command::new("/usr/bin/which")
        .arg(cmd)
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
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

fn install_wine_devel() -> Result<bool, String> {
    let wine = PathBuf::from("/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine");
    if wine.exists() {
        return Ok(false);
    }

    let bundled = find_bundled_archive("wine");
    if let Some(archive) = bundled {
        extract_to_applications(&archive, "Wine Devel.app")?;
        if wine.exists() {
            return Ok(true);
        }
    }

    brew_cask_install("wine@devel")
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
    let candidates = [
        PathBuf::from("/opt/homebrew/bin/brew"),
        PathBuf::from("/usr/local/bin/brew"),
    ];
    for c in &candidates {
        if c.exists() {
            return Ok(c.clone());
        }
    }
    let output = Command::new("which")
        .arg("brew")
        .output()
        .ok();
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
    let output = Command::new(&brew)
        .args(["install", package])
        .output()
        .map_err(|e| format!("brew failed: {}", e))?;

    let combined = format!(
        "{}{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    if output.status.success() || combined.contains("already installed") {
        Ok(true)
    } else {
        Err(combined.lines().last().unwrap_or("brew install failed").into())
    }
}

fn brew_cask_install(package: &str) -> Result<bool, String> {
    let brew = find_brew()?;

    let install_output = Command::new(&brew)
        .args(["install", "--cask", package])
        .output()
        .map_err(|e| format!("brew cask failed: {}", e))?;

    let install_combined = format!(
        "{}{}",
        String::from_utf8_lossy(&install_output.stdout),
        String::from_utf8_lossy(&install_output.stderr)
    );

    if install_output.status.success() && !install_combined.contains("already installed") {
        return Ok(true);
    }

    let reinstall_output = Command::new(&brew)
        .args(["reinstall", "--cask", package])
        .output()
        .map_err(|e| format!("brew cask reinstall failed: {}", e))?;

    let reinstall_combined = format!(
        "{}{}",
        String::from_utf8_lossy(&reinstall_output.stdout),
        String::from_utf8_lossy(&reinstall_output.stderr)
    );

    if reinstall_output.status.success() || reinstall_combined.contains("already installed") || reinstall_combined.contains("It seems there is already an app") {
        Ok(true)
    } else {
        let combined = format!("{}\n{}", install_combined, reinstall_combined);
        Err(combined.lines().last().unwrap_or("brew cask install failed").into())
    }
}

fn find_bundled_archive(name: &str) -> Option<PathBuf> {
    let candidates = [
        find_in_resources(name),
        find_in_dev_path(name),
    ];

    candidates.into_iter().find(|c| c.is_some()).flatten()
}

fn find_bundled_file(name: &str) -> Option<PathBuf> {
    if let Ok(exe) = std::env::current_exe() {
        let resources = exe.parent()?.parent()?.join("Resources");
        let file = resources.join(format!("bundles/{}", name));
        if file.exists() {
            return Some(file);
        }
    }

    let dev = PathBuf::from(format!("app/bundles/{}", name));
    if dev.exists() {
        return Some(dev);
    }

    None
}

fn find_in_resources(name: &str) -> Option<PathBuf> {
    if let Ok(exe) = std::env::current_exe() {
        let resources = exe.parent()?.parent()?.join("Resources");
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

fn extract_to_applications(archive: &PathBuf, app_name: &str) -> Result<(), String> {
    let tmp_dir = std::env::temp_dir().join("metalsharp_extract");
    let _ = fs::remove_dir_all(&tmp_dir);
    let _ = fs::create_dir_all(&tmp_dir);

    extract_zst(archive, &tmp_dir, app_name)?;

    let extracted = tmp_dir.join(app_name);
    if !extracted.exists() {
        return Err(format!("{} not found in extracted archive", app_name));
    }

    let app_path = format!("/Applications/{}", app_name);
    if PathBuf::from(&app_path).exists() {
        let rm_script = format!("rm -rf '{}'", app_path);
        let admin_result = Command::new("osascript")
            .args(["-e", &format!("do shell script \"{}\" with administrator privileges", rm_script)])
            .output()
            .map_err(|e| format!("admin auth failed: {}", e))?;
        if !admin_result.status.success() {
            let stderr = String::from_utf8_lossy(&admin_result.stderr);
            if stderr.contains("User canceled") {
                return Err("authentication cancelled".into());
            }
            return Err(format!("failed to remove existing {}: {}", app_name, stderr));
        }
    }

    let cp_script = format!("cp -R '{}' '/Applications/'", extracted.to_string_lossy());
    let admin_result = Command::new("osascript")
        .args(["-e", &format!("do shell script \"{}\" with administrator privileges", cp_script)])
        .output()
        .map_err(|e| format!("admin auth failed: {}", e))?;

    if !admin_result.status.success() {
        let stderr = String::from_utf8_lossy(&admin_result.stderr);
        if stderr.contains("User canceled") {
            return Err("authentication cancelled".into());
        }
        return Err(format!("failed to install {}: {}", app_name, stderr));
    }

    let _ = fs::remove_dir_all(&tmp_dir);
    Ok(())
}

fn extract_zst(archive: &PathBuf, dest: &PathBuf, name: &str) -> Result<(), String> {
    let _ = fs::create_dir_all(dest);

    let file = fs::File::open(archive)
        .map_err(|e| format!("cannot open archive: {}", e))?;

    let mut decoder = zstd::Decoder::new(file)
        .map_err(|e| format!("zstd decode error: {}", e))?;

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
        std::io::copy(&mut decoder, &mut stdin)
            .map_err(|e| format!("zstd decompression failed: {}", e))?;
        drop(stdin);
    }

    let status = tar_cmd.wait()
        .map_err(|e| format!("tar wait failed: {}", e))?;

    if !status.success() {
        return Err(format!("tar extraction failed for {}", name));
    }

    Ok(())
}
