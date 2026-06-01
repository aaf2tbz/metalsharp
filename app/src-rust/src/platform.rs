use std::path::PathBuf;
use std::process::Command;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HostPlatform {
    Macos,
    Linux,
    Other,
}

pub fn current() -> HostPlatform {
    if cfg!(target_os = "macos") {
        HostPlatform::Macos
    } else if cfg!(target_os = "linux") {
        HostPlatform::Linux
    } else {
        HostPlatform::Other
    }
}

pub fn name() -> &'static str {
    match current() {
        HostPlatform::Macos => "macos",
        HostPlatform::Linux => "linux",
        HostPlatform::Other => "other",
    }
}

pub fn app_resources_dir() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?;
    app_resources_dir_for_exe(&exe)
}

fn app_resources_dir_for_exe(exe: &std::path::Path) -> Option<PathBuf> {
    let exe_dir = exe.parent()?;

    if cfg!(target_os = "macos") {
        return macos_resources_dir_for_exe(exe);
    }

    if exe_dir.file_name().and_then(|n| n.to_str()) == Some("resources") {
        return Some(exe_dir.to_path_buf());
    }

    Some(exe_dir.to_path_buf())
}

fn macos_resources_dir_for_exe(exe: &std::path::Path) -> Option<PathBuf> {
    let mut dir = exe.parent()?;
    loop {
        if dir.file_name().and_then(|n| n.to_str()) == Some("Resources") {
            return Some(dir.to_path_buf());
        }

        let contents = dir.parent()?;
        if contents.file_name().and_then(|n| n.to_str()) == Some("Contents") {
            return Some(contents.join("Resources"));
        }

        dir = contents;
    }
}

pub fn runtime_library_env(ms_root: &std::path::Path) -> Option<(&'static str, String)> {
    let value = format!(
        "{}:{}",
        ms_root.join("lib").to_string_lossy(),
        ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy()
    );

    match current() {
        HostPlatform::Macos => Some(("DYLD_FALLBACK_LIBRARY_PATH", value)),
        HostPlatform::Linux => Some(("LD_LIBRARY_PATH", value)),
        HostPlatform::Other => None,
    }
}

pub fn set_runtime_library_env(cmd: &mut Command, ms_root: &std::path::Path) {
    if let Some((key, value)) = runtime_library_env(ms_root) {
        cmd.env(key, value);
    }
}

pub fn runtime_wine_binary(ms_root: &std::path::Path) -> PathBuf {
    let wrapped = ms_root.join("bin").join("metalsharp-wine");
    if wrapped.exists() {
        return wrapped;
    }

    ms_root.join("bin").join("wine")
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::Path;

    #[test]
    fn macos_resources_dir_accepts_backend_under_resources_runtime() {
        let exe = Path::new("/Applications/MetalSharp.app/Contents/Resources/runtime/metalsharp-backend");

        assert_eq!(
            macos_resources_dir_for_exe(exe),
            Some(PathBuf::from("/Applications/MetalSharp.app/Contents/Resources"))
        );
    }

    #[test]
    fn macos_resources_dir_accepts_app_binary_under_macos() {
        let exe = Path::new("/Applications/MetalSharp.app/Contents/MacOS/MetalSharp");

        assert_eq!(
            macos_resources_dir_for_exe(exe),
            Some(PathBuf::from("/Applications/MetalSharp.app/Contents/Resources"))
        );
    }
}
