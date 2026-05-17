use serde::Serialize;
use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use std::process::Command;

const DEFAULT_PACKAGE_NAME: &str = "microsoft.direct3d.d3d12.1.720.0-preview.nupkg";

#[derive(Debug, Clone, Serialize)]
struct PackageComponent {
    key: &'static str,
    description: &'static str,
    required: bool,
    entry: &'static str,
    present: bool,
}

pub fn inspect_d3d12_sdk_package(path: Option<PathBuf>) -> serde_json::Value {
    let package_path = path.unwrap_or_else(default_package_path);
    let present = package_path.is_file();
    if !present {
        return serde_json::json!({
            "ok": false,
            "status": "missing",
            "package_path": package_path,
            "summary": "Microsoft.Direct3D.D3D12 NuGet package was not found.",
            "components": expected_components(&BTreeSet::new()),
        });
    }

    let entries = match unzip_listing(&package_path) {
        Ok(entries) => entries,
        Err(error) => {
            return serde_json::json!({
                "ok": false,
                "status": "unreadable",
                "package_path": package_path,
                "error": error,
                "summary": "NuGet package exists but could not be listed with unzip.",
            });
        },
    };

    let package_version =
        unzip_entry(&package_path, "Microsoft.Direct3D.D3D12.nuspec").ok().and_then(|text| parse_nuspec_version(&text));
    let d3d12_header = unzip_entry(&package_path, "build/native/include/d3d12.h").ok();
    let sdk_version = d3d12_header.as_deref().and_then(parse_sdk_version);
    let components = expected_components(&entries);
    let missing_required: Vec<&'static str> = components
        .iter()
        .filter(|component| component.required && !component.present)
        .map(|component| component.key)
        .collect();
    let status = if missing_required.is_empty() { "ready" } else { "partial" };

    serde_json::json!({
        "ok": missing_required.is_empty(),
        "status": status,
        "package_path": package_path,
        "package_version": package_version,
        "sdk_version": sdk_version,
        "component_count": entries.len(),
        "missing_required": missing_required,
        "components": components,
        "usage": {
            "safe_to_auto_deploy": false,
            "recommended_role": "spec_headers_state_object_compiler_and_runtime_comparison",
            "note": "The package contains Windows Agility SDK binaries and headers. MetalSharp should use it as a versioned reference unless an explicit controlled deployment step is requested."
        },
    })
}

fn default_package_path() -> PathBuf {
    dirs::home_dir().unwrap_or_else(|| PathBuf::from(".")).join("Downloads").join(DEFAULT_PACKAGE_NAME)
}

fn expected_components(entries: &BTreeSet<String>) -> Vec<PackageComponent> {
    vec![
        component("x64_d3d12core", "x64 Agility runtime core", true, "build/native/bin/x64/D3D12Core.dll", entries),
        component(
            "x64_sdk_layers",
            "x64 D3D12 SDK debug layers",
            true,
            "build/native/bin/x64/d3d12SDKLayers.dll",
            entries,
        ),
        component(
            "x64_state_object_compiler",
            "x64 D3D12 state object compiler",
            true,
            "build/native/bin/x64/D3D12StateObjectCompiler.exe",
            entries,
        ),
        component("d3d12_header", "D3D12 API header", true, "build/native/include/d3d12.h", entries),
        component(
            "d3d12_compiler_header",
            "D3D12 compiler header",
            true,
            "build/native/include/d3d12compiler.h",
            entries,
        ),
        component(
            "d3dx12_core",
            "D3DX12 helper core header",
            true,
            "build/native/include/d3dx12/d3dx12_core.h",
            entries,
        ),
        component(
            "d3dx12_state_object",
            "D3DX12 state object helper header",
            true,
            "build/native/include/d3dx12/d3dx12_state_object.h",
            entries,
        ),
        component(
            "arm64_d3d12core",
            "arm64 Agility runtime core",
            false,
            "build/native/bin/arm64/D3D12Core.dll",
            entries,
        ),
        component(
            "win32_d3d12core",
            "win32 Agility runtime core",
            false,
            "build/native/bin/win32/D3D12Core.dll",
            entries,
        ),
    ]
}

fn component(
    key: &'static str,
    description: &'static str,
    required: bool,
    entry: &'static str,
    entries: &BTreeSet<String>,
) -> PackageComponent {
    PackageComponent { key, description, required, entry, present: entries.contains(entry) }
}

fn unzip_listing(package_path: &Path) -> Result<BTreeSet<String>, String> {
    let output = Command::new("unzip")
        .arg("-l")
        .arg(package_path)
        .output()
        .map_err(|error| format!("failed to run unzip: {}", error))?;
    if !output.status.success() {
        return Err(String::from_utf8_lossy(&output.stderr).trim().to_string());
    }
    Ok(parse_unzip_listing(&String::from_utf8_lossy(&output.stdout)))
}

fn unzip_entry(package_path: &Path, entry: &str) -> Result<String, String> {
    let output = Command::new("unzip")
        .arg("-p")
        .arg(package_path)
        .arg(entry)
        .output()
        .map_err(|error| format!("failed to run unzip: {}", error))?;
    if !output.status.success() {
        return Err(String::from_utf8_lossy(&output.stderr).trim().to_string());
    }
    Ok(String::from_utf8_lossy(&output.stdout).into_owned())
}

fn parse_unzip_listing(text: &str) -> BTreeSet<String> {
    text.lines()
        .filter_map(|line| {
            let mut parts = line.split_whitespace();
            let length = parts.next()?;
            if length.parse::<u64>().is_err() {
                return None;
            }
            parts.next()?;
            parts.next()?;
            parts.next().map(str::to_string)
        })
        .collect()
}

fn parse_nuspec_version(text: &str) -> Option<String> {
    let start = text.find("<version>")? + "<version>".len();
    let end = text[start..].find("</version>")? + start;
    Some(text[start..end].trim().to_string())
}

fn parse_sdk_version(text: &str) -> Option<u32> {
    text.lines().find_map(|line| {
        if !line.contains("D3D12_SDK_VERSION") {
            return None;
        }
        let value = line.split("D3D12_SDK_VERSION").nth(1)?;
        let digits: String = value.chars().filter(|ch| ch.is_ascii_digit()).collect();
        digits.parse().ok()
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_unzip_listing_entries() {
        let listing = r#"
Archive: package.nupkg
  Length      Date    Time    Name
---------  ---------- -----   ----
      856  04-24-2026 00:59   Microsoft.Direct3D.D3D12.nuspec
  5397304  04-24-2026 00:58   build/native/bin/x64/D3D12Core.dll
---------                     -------
"#;
        let entries = parse_unzip_listing(listing);

        assert!(entries.contains("Microsoft.Direct3D.D3D12.nuspec"));
        assert!(entries.contains("build/native/bin/x64/D3D12Core.dll"));
    }

    #[test]
    fn parses_nuspec_version() {
        assert_eq!(parse_nuspec_version("<version>1.720.0-preview</version>"), Some("1.720.0-preview".into()));
    }

    #[test]
    fn parses_d3d12_sdk_version_define() {
        assert_eq!(parse_sdk_version("#define\tD3D12_SDK_VERSION\t( 620 )"), Some(620));
    }
}
