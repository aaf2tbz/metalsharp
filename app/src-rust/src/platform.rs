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
    let exe_dir = exe.parent()?;

    if cfg!(target_os = "macos") {
        return exe_dir.parent().map(|p| p.join("Resources"));
    }

    if exe_dir.file_name().and_then(|n| n.to_str()) == Some("resources") {
        return Some(exe_dir.to_path_buf());
    }

    Some(exe_dir.to_path_buf())
}

pub fn metalsharp_home() -> PathBuf {
    let fallback_home = dirs::home_dir().unwrap_or_else(|| PathBuf::from(".")).join(".metalsharp");
    metalsharp_home_from_env(std::env::var("METALSHARP_HOME").ok().as_deref(), fallback_home)
}

fn metalsharp_home_from_env(value: Option<&str>, fallback_home: PathBuf) -> PathBuf {
    let Some(value) = value else {
        return fallback_home;
    };
    let trimmed = value.trim();
    if trimmed.is_empty() {
        fallback_home
    } else {
        PathBuf::from(trimmed)
    }
}

pub fn runtime_dir() -> PathBuf {
    metalsharp_home().join("runtime")
}

pub fn wine_runtime_root() -> PathBuf {
    runtime_dir().join("wine")
}

pub fn steam_prefix_dir() -> PathBuf {
    metalsharp_home().join("prefix-steam")
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

    #[test]
    fn metalsharp_home_uses_override_when_present() {
        assert_eq!(
            metalsharp_home_from_env(Some("/Volumes/AverySSD/metalsharp"), PathBuf::from("/tmp/fallback")),
            PathBuf::from("/Volumes/AverySSD/metalsharp")
        );
    }

    #[test]
    fn metalsharp_home_uses_fallback_for_empty_override() {
        assert_eq!(
            metalsharp_home_from_env(Some("  "), PathBuf::from("/tmp/fallback")),
            PathBuf::from("/tmp/fallback")
        );
    }
}
