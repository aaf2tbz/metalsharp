use serde_json::json;
use serde_json::Value;
use std::path::PathBuf;
use std::process::Command;

pub fn launch(exe_path: &str, game_type: &str) -> Result<u32, Box<dyn std::error::Error>> {
    match game_type {
        "xna_fna" => launch_via_fna_mono(exe_path),
        _ => launch_native(exe_path),
    }
}

fn launch_native(exe_path: &str) -> Result<u32, Box<dyn std::error::Error>> {
    let metalsharp_bin = find_metalsharp_native()?;

    let mut args: Vec<String> = Vec::new();
    args.push(exe_path.into());
    args.push("--fullscreen".into());

    let child = Command::new(&metalsharp_bin).args(&args).spawn()?;

    Ok(child.id())
}

fn launch_via_fna_mono(exe_path: &str) -> Result<u32, Box<dyn std::error::Error>> {
    let mono = find_mono()?;
    let exe = PathBuf::from(exe_path);
    let game_dir = exe.parent().ok_or("no parent dir for exe")?;

    let child = Command::new(&mono)
        .current_dir(game_dir)
        .env("DYLD_LIBRARY_PATH", ".")
        .env("METAL_DEVICE_WRAPPER_TYPE", "0")
        .arg(&exe)
        .spawn()?;

    Ok(child.id())
}

fn find_mono() -> Result<String, Box<dyn std::error::Error>> {
    let candidates = vec![
        PathBuf::from("/opt/homebrew/bin/mono"),
        PathBuf::from("/usr/local/bin/mono"),
    ];

    for c in candidates {
        if c.exists() {
            return Ok(c.to_string_lossy().to_string());
        }
    }

    let which = Command::new("which").arg("mono").output()?;
    if which.status.success() {
        return Ok(String::from_utf8_lossy(&which.stdout).trim().to_string());
    }

    Err("mono not found — install with: brew install mono".into())
}

pub fn kill(pid: i32) -> Result<(), Box<dyn std::error::Error>> {
    Command::new("kill").arg(pid.to_string()).output()?;
    Ok(())
}

fn find_metalsharp_native() -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;

    let candidates = vec![
        PathBuf::from("/Applications/MetalSharp.app/Contents/Resources/metalsharp"),
        home.join(".metalsharp/metalsharp"),
        home.join("metalsharp/build/metalsharp"),
        home.join("metalsharp/build/metalsharp_native"),
        PathBuf::from("/usr/local/bin/metalsharp"),
        PathBuf::from("/opt/homebrew/bin/metalsharp"),
    ];

    for c in candidates {
        if c.exists() {
            return Ok(c.to_string_lossy().to_string());
        }
    }

    let which = Command::new("which").arg("metalsharp").output()?;
    if which.status.success() {
        return Ok(String::from_utf8_lossy(&which.stdout).trim().to_string());
    }

    Err("metalsharp binary not found — build with: cmake --build build".into())
}

pub fn get_config() -> Value {
    let available = find_metalsharp_native().is_ok();
    let mono_available = find_mono().is_ok();

    json!({
        "ok": true,
        "native_available": available,
        "mono_available": mono_available,
    })
}

pub fn set_config(_mode: &str) -> Result<Value, Box<dyn std::error::Error>> {
    Ok(get_config())
}
