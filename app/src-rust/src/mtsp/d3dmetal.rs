use serde::Serialize;
use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use std::time::UNIX_EPOCH;

#[derive(Debug, Clone, Serialize)]
struct D3DMetalComponent {
    key: &'static str,
    description: &'static str,
    required: bool,
    path: PathBuf,
    present: bool,
    file_type: &'static str,
    size_bytes: Option<u64>,
    modified_unix: Option<u64>,
}

#[derive(Debug, Clone, Serialize)]
struct D3DMetalInstall {
    app_path: PathBuf,
    version: Option<String>,
    status: &'static str,
    usable_as_d3d12_reference: bool,
    missing_required: Vec<&'static str>,
    components: Vec<D3DMetalComponent>,
}

pub fn d3dmetal_status() -> serde_json::Value {
    let installs: Vec<D3DMetalInstall> =
        candidate_app_paths().into_iter().filter(|path| path.is_dir()).map(|path| inspect_install(&path)).collect();

    let ready = installs.iter().any(|install| install.usable_as_d3d12_reference);
    let status = if ready {
        "ready"
    } else if installs.is_empty() {
        "missing"
    } else {
        "partial"
    };

    serde_json::json!({
        "ok": true,
        "status": status,
        "ready": ready,
        "summary": summary_for_status(status),
        "installs": installs,
    })
}

fn inspect_install(app_path: &Path) -> D3DMetalInstall {
    let components = expected_components(app_path);
    let missing_required: Vec<&'static str> = components
        .iter()
        .filter(|component| component.required && !component.present)
        .map(|component| component.key)
        .collect();
    let usable_as_d3d12_reference = missing_required.is_empty();
    let status = if usable_as_d3d12_reference { "ready" } else { "partial" };

    D3DMetalInstall {
        app_path: app_path.to_path_buf(),
        version: read_bundle_version(app_path),
        status,
        usable_as_d3d12_reference,
        missing_required,
        components,
    }
}

fn expected_components(app_path: &Path) -> Vec<D3DMetalComponent> {
    let resources = app_path.join("Contents").join("Resources");
    let wine_lib = resources.join("wine").join("lib");
    let x64_windows = wine_lib.join("wine").join("x86_64-windows");
    let i386_windows = wine_lib.join("wine").join("i386-windows");
    let external = wine_lib.join("external");

    vec![
        component("x64_d3d12", "64-bit D3D12 Wine shim", true, x64_windows.join("d3d12.dll"), "file"),
        component("x64_dxgi", "64-bit DXGI Wine shim", true, x64_windows.join("dxgi.dll"), "file"),
        component("x64_d3d11", "64-bit D3D11 Wine shim", true, x64_windows.join("d3d11.dll"), "file"),
        component(
            "d3dmetal_framework",
            "Apple D3DMetal framework",
            true,
            external.join("D3DMetal.framework"),
            "directory",
        ),
        component(
            "libd3dshared",
            "Apple shared Direct3D support library",
            true,
            external.join("libd3dshared.dylib"),
            "file",
        ),
        component("i386_d3d12", "32-bit D3D12 Wine shim", false, i386_windows.join("d3d12.dll"), "file"),
        component("i386_dxgi", "32-bit DXGI Wine shim", false, i386_windows.join("dxgi.dll"), "file"),
        component("i386_d3d11", "32-bit D3D11 Wine shim", false, i386_windows.join("d3d11.dll"), "file"),
    ]
}

fn component(
    key: &'static str,
    description: &'static str,
    required: bool,
    path: PathBuf,
    file_type: &'static str,
) -> D3DMetalComponent {
    let metadata = path.metadata().ok();
    let present = match file_type {
        "directory" => metadata.as_ref().is_some_and(|m| m.is_dir()),
        _ => metadata.as_ref().is_some_and(|m| m.is_file()),
    };
    let size_bytes = metadata.as_ref().filter(|m| m.is_file()).map(|m| m.len());
    let modified_unix = metadata
        .as_ref()
        .and_then(|m| m.modified().ok())
        .and_then(|modified| modified.duration_since(UNIX_EPOCH).ok())
        .map(|duration| duration.as_secs());

    D3DMetalComponent { key, description, required, path, present, file_type, size_bytes, modified_unix }
}

fn candidate_app_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();

    if let Ok(path) = std::env::var("METALSHARP_GPTK_APP") {
        let trimmed = path.trim();
        if !trimmed.is_empty() {
            paths.push(PathBuf::from(trimmed));
        }
    }

    paths.push(PathBuf::from("/Applications/Game Porting Toolkit.app"));
    push_cask_candidates(&mut paths, Path::new("/opt/homebrew/Caskroom/game-porting-toolkit"));
    push_cask_candidates(&mut paths, Path::new("/usr/local/Caskroom/game-porting-toolkit"));
    dedupe_paths(paths)
}

fn push_cask_candidates(paths: &mut Vec<PathBuf>, cask_root: &Path) {
    let Ok(entries) = std::fs::read_dir(cask_root) else {
        return;
    };

    for entry in entries.flatten() {
        let path = entry.path().join("Game Porting Toolkit.app");
        if path.is_dir() {
            paths.push(path);
        }
    }
}

fn dedupe_paths(paths: Vec<PathBuf>) -> Vec<PathBuf> {
    let mut seen = BTreeSet::new();
    let mut deduped = Vec::new();
    for path in paths {
        let key = path.to_string_lossy().into_owned();
        if seen.insert(key) {
            deduped.push(path);
        }
    }
    deduped
}

fn read_bundle_version(app_path: &Path) -> Option<String> {
    let info_plist = app_path.join("Contents").join("Info.plist");
    let text = std::fs::read_to_string(info_plist).ok()?;
    read_plist_string(&text, "CFBundleShortVersionString").or_else(|| read_plist_string(&text, "CFBundleVersion"))
}

fn read_plist_string(text: &str, key: &str) -> Option<String> {
    let mut lines = text.lines();
    while let Some(line) = lines.next() {
        if !line.contains(&format!("<key>{}</key>", key)) {
            continue;
        }
        for value_line in lines.by_ref() {
            let trimmed = value_line.trim();
            if let Some(value) = trimmed.strip_prefix("<string>").and_then(|s| s.strip_suffix("</string>")) {
                return Some(value.to_string());
            }
            if trimmed.starts_with("<key>") {
                break;
            }
        }
    }
    None
}

fn summary_for_status(status: &str) -> &'static str {
    match status {
        "ready" => "Apple GPTK D3DMetal is installed with D3D12, D3D11, DXGI, and framework components present.",
        "partial" => "Apple GPTK was found, but one or more required D3DMetal/D3D12 components are missing.",
        _ => "Apple GPTK D3DMetal was not found in the standard install locations.",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_xml_plist_version_strings() {
        let plist = r#"
        <plist version="1.0">
        <dict>
            <key>CFBundleShortVersionString</key>
            <string>3.0-2</string>
        </dict>
        </plist>
        "#;

        assert_eq!(read_plist_string(plist, "CFBundleShortVersionString"), Some("3.0-2".into()));
    }

    #[test]
    fn expected_components_include_required_d3d12_reference_files() {
        let components = expected_components(Path::new("/tmp/GPTK.app"));
        let required: BTreeSet<&str> =
            components.iter().filter(|component| component.required).map(|component| component.key).collect();

        assert!(required.contains("x64_d3d12"));
        assert!(required.contains("x64_dxgi"));
        assert!(required.contains("x64_d3d11"));
        assert!(required.contains("d3dmetal_framework"));
        assert!(required.contains("libd3dshared"));
    }
}
