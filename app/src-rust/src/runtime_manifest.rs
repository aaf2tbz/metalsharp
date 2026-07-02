use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

const RUNTIME_MANIFEST_SCHEMA: &str = "metalsharp.runtime.manifest.v1";
const RUNTIME_MANIFEST_FILE: &str = "metalsharp-runtime-manifest.json";
const RUNTIME_INFO_HELPER: &str = "metalsharp-runtime-info";

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RuntimeManifest {
    pub schema: String,
    pub metalsharp_version: String,
    pub wine_version: Option<String>,
    pub host_arch: String,
    pub host_translation: String,
    pub windows_support: Vec<String>,
    pub runtime_root: String,
    pub wine_root: String,
    pub canonical_m12_surface: String,
    pub surfaces: Vec<RuntimeSurfaceManifest>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RuntimeSurfaceManifest {
    pub id: String,
    pub installed_path: String,
    pub present: bool,
    pub role: String,
}

pub fn handle_runtime_manifest() -> Value {
    match dirs::home_dir() {
        Some(home) => runtime_manifest_report_for(&home),
        None => json!({"ok": false, "error": "home directory could not be resolved"}),
    }
}

pub fn runtime_manifest_report_for(home: &Path) -> Value {
    runtime_manifest_report_for_with_wine_probe(home, true)
}

pub fn runtime_manifest_filesystem_report_for(home: &Path) -> Value {
    runtime_manifest_report_for_with_wine_probe(home, false)
}

fn runtime_manifest_report_for_with_wine_probe(home: &Path, probe_wine_version: bool) -> Value {
    let manifest = expected_runtime_manifest_for_with_wine_probe(home, probe_wine_version);
    let artifact_report = crate::installer::runtime_artifact_report_for(home);
    let manifest_path = manifest_path_for(home);
    let persisted = read_persisted_manifest(&manifest_path);
    let validation = validate_runtime_manifest_for(home, &manifest);

    let runtime_info_helper = runtime_info_helper_path_for(home);
    json!({
        "ok": validation.get("ok").and_then(|value| value.as_bool()).unwrap_or(false),
        "schema": RUNTIME_MANIFEST_SCHEMA,
        "manifestPath": manifest_path.to_string_lossy(),
        "runtimeInfoHelper": {
            "path": runtime_info_helper.to_string_lossy(),
            "present": runtime_info_helper.is_file(),
            "invokesWine": false,
            "prints": "persisted runtime manifest JSON"
        },
        "expected": manifest,
        "persisted": persisted,
        "validation": validation,
        "artifacts": artifact_report,
    })
}

pub fn expected_runtime_manifest_for(home: &Path) -> RuntimeManifest {
    expected_runtime_manifest_for_with_wine_probe(home, true)
}

fn expected_runtime_manifest_for_with_wine_probe(home: &Path, probe_wine_version: bool) -> RuntimeManifest {
    let ms_home = crate::platform::metalsharp_home_dir_for(&home.to_path_buf());
    let runtime_root = ms_home.join("runtime");
    let wine_root = runtime_root.join("wine");
    let host_arch = std::env::consts::ARCH.to_string();
    RuntimeManifest {
        schema: RUNTIME_MANIFEST_SCHEMA.to_string(),
        metalsharp_version: env!("CARGO_PKG_VERSION").to_string(),
        wine_version: if probe_wine_version { metalsharp_wine_version(&wine_root) } else { None },
        host_arch: host_arch.clone(),
        host_translation: if host_arch == "aarch64" || host_arch == "arm64" {
            "x86_64 Wine under Rosetta when launching Wine-backed routes".to_string()
        } else {
            "x86_64 host runtime".to_string()
        },
        windows_support: vec!["win32".to_string(), "win64".to_string(), "wow64".to_string()],
        runtime_root: runtime_root.to_string_lossy().to_string(),
        wine_root: wine_root.to_string_lossy().to_string(),
        canonical_m12_surface: "dxmt_m12".to_string(),
        surfaces: vec![
            surface_manifest("wine", &wine_root, "Wine host runtime"),
            surface_manifest("dxmt", &wine_root.join("lib").join("dxmt"), "M9/M10/M11 DXMT runtime surface"),
            surface_manifest(
                "dxmt_m12",
                &wine_root.join("lib").join("dxmt_m12"),
                "M12 D3D12/DXGI/winemetal runtime surface",
            ),
            surface_manifest(
                "metalsharp_hooks",
                &wine_root.join("lib").join("metalsharp"),
                "MetalSharp Wine hook DLLs",
            ),
            surface_manifest("dxvk", &wine_root.join("lib").join("dxvk"), "planned DXVK runtime surface"),
            surface_manifest("vkd3d", &wine_root.join("lib").join("vkd3d"), "planned VKD3D-Proton runtime surface"),
            surface_manifest("mono_arm64", &runtime_root.join("mono-arm64"), "native Mono/FNA ARM64 runtime"),
            surface_manifest("mono_x86", &runtime_root.join("mono-x86"), "native Mono/FNA x86_64 runtime"),
            surface_manifest("host", &runtime_root.join("host"), "MetalSharp host runtime ABI"),
        ],
    }
}

pub fn write_expected_runtime_manifest_for(home: &Path) -> Result<PathBuf, String> {
    let manifest = expected_runtime_manifest_for(home);
    let path = manifest_path_for(home);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|error| format!("create runtime manifest dir: {}", error))?;
    }
    let tmp = path.with_extension("json.tmp");
    let payload =
        serde_json::to_vec_pretty(&manifest).map_err(|error| format!("encode runtime manifest: {}", error))?;
    fs::write(&tmp, payload).map_err(|error| format!("write runtime manifest temp: {}", error))?;
    fs::rename(&tmp, &path).map_err(|error| format!("publish runtime manifest: {}", error))?;
    write_runtime_info_helper_for(home)?;
    Ok(path)
}

pub fn runtime_info_helper_path_for(home: &Path) -> PathBuf {
    crate::platform::metalsharp_home_dir_for(&home.to_path_buf())
        .join("runtime")
        .join("wine")
        .join("bin")
        .join(RUNTIME_INFO_HELPER)
}

pub fn write_runtime_info_helper_for(home: &Path) -> Result<PathBuf, String> {
    let helper = runtime_info_helper_path_for(home);
    let manifest = manifest_path_for(home);
    if let Some(parent) = helper.parent() {
        fs::create_dir_all(parent).map_err(|error| format!("create runtime info helper dir: {}", error))?;
    }
    let script = format!(
        r#"#!/bin/sh
set -eu
manifest={manifest}
case "${{1:---json}}" in
  --json|--metalsharp-runtime-info) exec /bin/cat "$manifest" ;;
  --path) printf '%s\n' "$manifest" ;;
  --help|-h) printf '%s\n' 'usage: metalsharp-runtime-info [--json|--path|--help]' ;;
  *) printf '%s\n' 'usage: metalsharp-runtime-info [--json|--path|--help]' >&2; exit 64 ;;
esac
"#,
        manifest = shell_quote(&manifest.to_string_lossy())
    );
    fs::write(&helper, script).map_err(|error| format!("write runtime info helper: {}", error))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = fs::metadata(&helper)
            .map_err(|error| format!("read runtime info helper metadata: {}", error))?
            .permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&helper, permissions).map_err(|error| format!("chmod runtime info helper: {}", error))?;
    }
    Ok(helper)
}

pub fn validate_runtime_manifest_for(home: &Path, manifest: &RuntimeManifest) -> Value {
    let ms_home = crate::platform::metalsharp_home_dir_for(&home.to_path_buf());
    let wine_root = ms_home.join("runtime").join("wine");
    let wine_binary = crate::platform::runtime_wine_binary(&wine_root);
    let wine_present = file_nonempty(&wine_binary);
    let dxmt_m12_root = wine_root.join("lib").join("dxmt_m12");
    let dxmt_m12_present = dxmt_m12_root.is_dir();
    let m12_missing = crate::installer::missing_m12_sidecars_for(home);
    let has_hyphenated_installed_m12_path =
        manifest.surfaces.iter().any(|surface| surface.id == "dxmt_m12" && surface.installed_path.contains("dxmt-m12"));
    let persisted_schema_ok = manifest.schema == RUNTIME_MANIFEST_SCHEMA;
    let ok = persisted_schema_ok
        && wine_present
        && dxmt_m12_present
        && m12_missing.is_empty()
        && !has_hyphenated_installed_m12_path;

    json!({
        "ok": ok,
        "checks": [
            {"id": "schema", "ok": persisted_schema_ok, "detail": manifest.schema},
            {"id": "wine_binary", "ok": wine_present, "detail": wine_binary.to_string_lossy()},
            {"id": "canonical_m12_surface", "ok": manifest.canonical_m12_surface == "dxmt_m12", "detail": manifest.canonical_m12_surface},
            {"id": "dxmt_m12_path", "ok": dxmt_m12_present && !has_hyphenated_installed_m12_path, "detail": dxmt_m12_root.to_string_lossy()},
            {"id": "dxmt_m12_sidecars", "ok": m12_missing.is_empty(), "detail": m12_missing},
        ]
    })
}

fn surface_manifest(id: &str, path: &Path, role: &str) -> RuntimeSurfaceManifest {
    RuntimeSurfaceManifest {
        id: id.to_string(),
        installed_path: path.to_string_lossy().to_string(),
        present: path.exists(),
        role: role.to_string(),
    }
}

pub(crate) fn manifest_path_for(home: &Path) -> PathBuf {
    crate::platform::metalsharp_home_dir_for(&home.to_path_buf()).join("runtime").join(RUNTIME_MANIFEST_FILE)
}

fn read_persisted_manifest(path: &Path) -> Value {
    match fs::read_to_string(path) {
        Ok(data) => serde_json::from_str(&data)
            .unwrap_or_else(|error| json!({"present": true, "valid": false, "error": error.to_string()})),
        Err(_) => json!({"present": false}),
    }
}

fn metalsharp_wine_version(wine_root: &Path) -> Option<String> {
    let wine = crate::platform::runtime_wine_binary(wine_root);
    if !file_nonempty(&wine) {
        return None;
    }
    let output = Command::new(wine).arg("--version").output().ok()?;
    if !output.status.success() {
        return None;
    }
    let version = String::from_utf8_lossy(&output.stdout).trim().to_string();
    (!version.is_empty()).then_some(version)
}

fn file_nonempty(path: &Path) -> bool {
    path.metadata().map(|meta| meta.is_file() && meta.len() > 0).unwrap_or(false)
}

fn shell_quote(value: &str) -> String {
    format!("'{}'", value.replace('\'', "'\\''"))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_home(name: &str) -> PathBuf {
        let mut path = std::env::temp_dir();
        path.push(format!("metalsharp-runtime-manifest-{}-{}", name, std::process::id()));
        let _ = fs::remove_dir_all(&path);
        fs::create_dir_all(&path).expect("create test home");
        path
    }

    fn write_file(path: &Path, data: &[u8]) {
        fs::create_dir_all(path.parent().unwrap()).expect("create parent");
        fs::write(path, data).expect("write file");
    }

    #[test]
    fn expected_manifest_uses_canonical_dxmt_m12_path() {
        let home = test_home("canonical");
        let manifest = expected_runtime_manifest_for(&home);
        let m12 = manifest.surfaces.iter().find(|surface| surface.id == "dxmt_m12").expect("m12 surface");
        assert!(m12.installed_path.ends_with("runtime/wine/lib/dxmt_m12"));
        assert!(!m12.installed_path.contains("dxmt-m12"));
        assert_eq!(manifest.canonical_m12_surface, "dxmt_m12");
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn runtime_manifest_validation_reports_missing_m12_sidecars() {
        let home = test_home("missing-sidecars");
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        write_file(
            &ms_home.join("runtime").join("wine").join("bin").join("metalsharp-wine"),
            b"#!/bin/sh\necho wine-11.5\n",
        );
        let manifest = expected_runtime_manifest_for(&home);
        let validation = validate_runtime_manifest_for(&home, &manifest);
        assert_eq!(validation.get("ok").and_then(|value| value.as_bool()), Some(false));
        let checks = validation.get("checks").and_then(|value| value.as_array()).expect("checks");
        let sidecars = checks
            .iter()
            .find(|check| check.get("id").and_then(|value| value.as_str()) == Some("dxmt_m12_sidecars"))
            .expect("sidecar check");
        assert_eq!(sidecars.get("ok").and_then(|value| value.as_bool()), Some(false));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn write_expected_runtime_manifest_is_atomic_and_readable() {
        let home = test_home("write");
        let path = write_expected_runtime_manifest_for(&home).expect("write manifest");
        assert!(path.is_file());
        let data = fs::read_to_string(&path).expect("read manifest");
        let parsed: RuntimeManifest = serde_json::from_str(&data).expect("parse manifest");
        assert_eq!(parsed.schema, RUNTIME_MANIFEST_SCHEMA);
        assert_eq!(parsed.canonical_m12_surface, "dxmt_m12");
        let helper = runtime_info_helper_path_for(&home);
        assert!(helper.is_file());
        let helper_script = fs::read_to_string(&helper).expect("read helper");
        assert!(helper_script.contains("/bin/cat"));
        assert!(!helper_script.contains("metalsharp-wine"));
        assert!(!helper_script.contains("wine --version"));
        let helper_path_output = Command::new(&helper).arg("--path").output().expect("run helper --path");
        assert!(helper_path_output.status.success());
        assert_eq!(String::from_utf8_lossy(&helper_path_output.stdout).trim(), path.to_string_lossy());
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn runtime_manifest_report_exposes_runtime_info_helper_without_invoking_wine() {
        let home = test_home("helper-report");
        let helper = write_runtime_info_helper_for(&home).expect("write helper");
        let report = runtime_manifest_filesystem_report_for(&home);
        assert_eq!(
            report.pointer("/runtimeInfoHelper/path").and_then(|value| value.as_str()),
            Some(helper.to_string_lossy().as_ref())
        );
        assert_eq!(report.pointer("/runtimeInfoHelper/present").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(report.pointer("/runtimeInfoHelper/invokesWine").and_then(|value| value.as_bool()), Some(false));
        let _ = fs::remove_dir_all(home);
    }
}
