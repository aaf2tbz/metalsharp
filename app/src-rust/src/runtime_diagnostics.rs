use serde_json::{json, Value};
use std::path::{Path, PathBuf};

const RUNTIME_DIAGNOSTICS_SCHEMA: &str = "metalsharp.runtime.diagnostics.v1";

pub fn handle_runtime_diagnostics() -> Value {
    match dirs::home_dir() {
        Some(home) => runtime_diagnostics_report_for(&home),
        None => json!({"ok": false, "error": "home directory could not be resolved"}),
    }
}

pub fn runtime_diagnostics_report_for(home: &Path) -> Value {
    let ms_home = crate::platform::metalsharp_home_dir_for(&home.to_path_buf());
    let runtime_root = ms_home.join("runtime");
    let wine_root = runtime_root.join("wine");
    let steam_prefix = ms_home.join("prefix-steam");
    let gog_prefix = ms_home.join("bottles").join("gog-prefix").join("prefix");

    let contracts = crate::runtime_contracts::runtime_lane_contracts();
    let available_lanes: Vec<_> = contracts
        .iter()
        .filter(|contract| contract.status == crate::runtime_contracts::RuntimeLaneStatus::Available)
        .map(|contract| contract.id)
        .collect();
    let planned_lanes: Vec<_> = contracts
        .iter()
        .filter(|contract| contract.status == crate::runtime_contracts::RuntimeLaneStatus::Planned)
        .map(|contract| contract.id)
        .collect();
    let external_lanes: Vec<_> = contracts
        .iter()
        .filter(|contract| contract.status == crate::runtime_contracts::RuntimeLaneStatus::External)
        .map(|contract| contract.id)
        .collect();

    let manifest = crate::runtime_manifest::runtime_manifest_filesystem_report_for(home);
    let manifest_ok = manifest
        .get("validation")
        .and_then(|validation| validation.get("ok"))
        .and_then(|value| value.as_bool())
        .unwrap_or(false);
    let prefix_report = prefix_policy_report(&steam_prefix, &gog_prefix);
    let prefix_ok = prefix_report.get("ok").and_then(|value| value.as_bool()).unwrap_or(false);
    let canonical_m12_ok = contracts.iter().any(|contract| {
        contract.id == "m12_dxmt_m12"
            && contract.runtime_surface_paths.iter().any(|path| path.ends_with("runtime/wine/lib/dxmt_m12"))
            && contract.runtime_surface_paths.iter().all(|path| !path.contains("dxmt-m12"))
    });
    let wine_binary = crate::platform::runtime_wine_binary(&wine_root);
    let wine_binary_present = file_nonempty(&wine_binary);
    let dxmt_current = crate::installer::dxmt_runtime_current_for_home(home);
    let dxmt_m12_current = crate::installer::dxmt_m12_runtime_current_for_home(home);
    let runtime_ready = wine_binary_present && dxmt_current && dxmt_m12_current && manifest_ok;
    let ok = runtime_ready && prefix_ok && canonical_m12_ok;

    json!({
        "ok": ok,
        "schema": RUNTIME_DIAGNOSTICS_SCHEMA,
        "readOnly": true,
        "summary": runtime_summary(ok, runtime_ready, manifest_ok, dxmt_current, dxmt_m12_current, prefix_ok),
        "paths": {
            "metalsharpHome": ms_home.to_string_lossy(),
            "runtimeRoot": runtime_root.to_string_lossy(),
            "wineRoot": wine_root.to_string_lossy(),
            "wineBinary": wine_binary.to_string_lossy(),
            "steamPrefix": steam_prefix.to_string_lossy(),
            "gogPrefix": gog_prefix.to_string_lossy(),
        },
        "contracts": {
            "schema": "metalsharp.runtime.contracts.v1",
            "canonicalM12Surface": "dxmt_m12",
            "canonicalM12InstalledPath": "runtime/wine/lib/dxmt_m12",
            "canonicalM12Ok": canonical_m12_ok,
            "total": contracts.len(),
            "available": available_lanes,
            "planned": planned_lanes,
            "external": external_lanes,
        },
        "runtime": {
            "ready": runtime_ready,
            "wineBinaryPresent": wine_binary_present,
            "dxmtCurrent": dxmt_current,
            "dxmtM12Current": dxmt_m12_current,
            "manifestOk": manifest_ok,
            "manifest": manifest,
        },
        "prefixes": prefix_report,
        "installReplacementGuard": {
            "allowedNow": false,
            "reason": "Do not wipe or replace the current MetalSharp install from diagnostics. Replacement must be a final, explicit user-confirmed step after Wine 2.0 runtime validation passes."
        },
        "nextActions": next_actions(runtime_ready, manifest_ok, dxmt_current, dxmt_m12_current, prefix_ok),
    })
}

fn prefix_policy_report(steam_prefix: &Path, gog_prefix: &Path) -> Value {
    let steam_prefix = normalize_path(steam_prefix);
    let gog_prefix = normalize_path(gog_prefix);
    let gog_is_dedicated = gog_prefix.ends_with(Path::new("bottles/gog-prefix/prefix"));
    let gog_lexically_uses_prefix_steam = gog_prefix == steam_prefix || gog_prefix.starts_with(&steam_prefix);
    let policy_root = steam_prefix.parent().unwrap_or(Path::new("/"));
    let steam_contains_symlink = path_contains_symlink_below(policy_root, &steam_prefix);
    let gog_contains_symlink = path_contains_symlink_below(policy_root, &gog_prefix);
    let steam_canonical = fs_canonicalize_if_exists(&steam_prefix);
    let gog_canonical = fs_canonicalize_if_exists(&gog_prefix);
    let canonical_overlap = match (&steam_canonical, &gog_canonical) {
        (Some(steam), Some(gog)) => gog == steam || gog.starts_with(steam),
        _ => false,
    };
    let gog_uses_prefix_steam = gog_lexically_uses_prefix_steam || canonical_overlap;
    let ok = gog_is_dedicated && !gog_uses_prefix_steam && !gog_contains_symlink;

    json!({
        "ok": ok,
        "steam": {
            "id": "steam_background",
            "prefixPolicy": "prefix-steam only",
            "path": steam_prefix.to_string_lossy(),
            "present": steam_prefix.exists(),
            "containsSymlink": steam_contains_symlink,
            "canonicalPath": steam_canonical.as_ref().map(|path| path.to_string_lossy().to_string()),
        },
        "gog": {
            "id": "gogdl_wine",
            "prefixPolicy": "dedicated gog-prefix only",
            "path": gog_prefix.to_string_lossy(),
            "present": gog_prefix.exists(),
            "dedicatedPathOk": gog_is_dedicated,
            "containsSymlink": gog_contains_symlink,
            "usesPrefixSteam": gog_uses_prefix_steam,
            "lexicallyUsesPrefixSteam": gog_lexically_uses_prefix_steam,
            "canonicalOverlapsPrefixSteam": canonical_overlap,
            "canonicalPath": gog_canonical.as_ref().map(|path| path.to_string_lossy().to_string()),
        },
    })
}

fn runtime_summary(
    ok: bool,
    runtime_ready: bool,
    manifest_ok: bool,
    dxmt_current: bool,
    dxmt_m12_current: bool,
    prefix_ok: bool,
) -> &'static str {
    if ok {
        return "Runtime diagnostics passed: Wine, DXMT, dxmt_m12, manifest, and prefix policies are ready.";
    }
    if !prefix_ok {
        return "Runtime diagnostics blocked: Steam and GOG prefix policies are not safely separated.";
    }
    if !manifest_ok {
        return "Runtime diagnostics blocked: runtime manifest validation is not green.";
    }
    if !dxmt_m12_current {
        return "Runtime diagnostics blocked: canonical dxmt_m12 runtime is missing, stale, or has invalid sidecars.";
    }
    if !dxmt_current {
        return "Runtime diagnostics blocked: legacy DXMT runtime surface is missing or stale.";
    }
    if !runtime_ready {
        return "Runtime diagnostics blocked: Wine runtime is incomplete.";
    }
    "Runtime diagnostics blocked: one or more runtime checks failed."
}

fn next_actions(
    runtime_ready: bool,
    manifest_ok: bool,
    dxmt_current: bool,
    dxmt_m12_current: bool,
    prefix_ok: bool,
) -> Vec<&'static str> {
    let mut actions = Vec::new();
    if !prefix_ok {
        actions.push("Keep GOG on ~/.metalsharp/bottles/gog-prefix/prefix and Steam on ~/.metalsharp/prefix-steam; do not merge prefixes.");
    }
    if !dxmt_current || !dxmt_m12_current {
        actions.push("Run setup/install-all or the graphics runtime repair flow before launching Wine-backed routes.");
    }
    if !manifest_ok {
        actions.push("Regenerate the runtime manifest through setup after runtime assets are staged, then re-check /runtime/manifest.");
    }
    if runtime_ready && prefix_ok {
        actions.push(
            "Proceed to per-route doctors or game-specific launch diagnostics; do not wipe the current install yet.",
        );
    }
    actions
}

fn normalize_path(path: &Path) -> PathBuf {
    path.components().collect()
}

fn fs_canonicalize_if_exists(path: &Path) -> Option<PathBuf> {
    path.exists().then(|| std::fs::canonicalize(path).ok()).flatten()
}

fn path_contains_symlink_below(root: &Path, path: &Path) -> bool {
    let relative = match path.strip_prefix(root) {
        Ok(relative) => relative,
        Err(_) => path,
    };
    let mut current = root.to_path_buf();
    for component in relative.components() {
        current.push(component.as_os_str());
        if std::fs::symlink_metadata(&current).map(|meta| meta.file_type().is_symlink()).unwrap_or(false) {
            return true;
        }
    }
    false
}

fn file_nonempty(path: &Path) -> bool {
    path.metadata().map(|meta| meta.is_file() && meta.len() > 0).unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn test_home(name: &str) -> PathBuf {
        let mut path = std::env::temp_dir();
        path.push(format!("metalsharp-runtime-diagnostics-{}-{}", name, std::process::id()));
        let _ = fs::remove_dir_all(&path);
        fs::create_dir_all(&path).expect("create test home");
        path
    }

    #[test]
    fn diagnostics_keep_gog_and_steam_prefixes_separate() {
        let home = test_home("prefixes");
        let report = runtime_diagnostics_report_for(&home);
        let prefixes = report.get("prefixes").expect("prefixes");
        assert_eq!(prefixes.get("ok").and_then(|value| value.as_bool()), Some(true));
        let gog = prefixes.get("gog").expect("gog");
        assert_eq!(gog.get("dedicatedPathOk").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(gog.get("usesPrefixSteam").and_then(|value| value.as_bool()), Some(false));
        assert!(gog
            .get("path")
            .and_then(|value| value.as_str())
            .expect("gog path")
            .ends_with(".metalsharp/bottles/gog-prefix/prefix"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn diagnostics_report_canonical_m12_contract_surface() {
        let home = test_home("m12-contract");
        let report = runtime_diagnostics_report_for(&home);
        let contracts = report.get("contracts").expect("contracts");
        assert_eq!(contracts.get("canonicalM12Surface").and_then(|value| value.as_str()), Some("dxmt_m12"));
        assert_eq!(contracts.get("canonicalM12Ok").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(
            contracts.get("canonicalM12InstalledPath").and_then(|value| value.as_str()),
            Some("runtime/wine/lib/dxmt_m12")
        );
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn diagnostics_detect_symlinked_gog_prefix_aliasing_steam() {
        let home = test_home("symlink-prefix");
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        let steam_prefix = ms_home.join("prefix-steam");
        let gog_parent = ms_home.join("bottles").join("gog-prefix");
        fs::create_dir_all(&steam_prefix).expect("create steam prefix");
        fs::create_dir_all(&gog_parent).expect("create gog parent");
        std::os::unix::fs::symlink(&steam_prefix, gog_parent.join("prefix")).expect("symlink gog prefix to steam");

        let report = runtime_diagnostics_report_for(&home);
        let prefixes = report.get("prefixes").expect("prefixes");
        assert_eq!(prefixes.get("ok").and_then(|value| value.as_bool()), Some(false));
        let gog = prefixes.get("gog").expect("gog");
        assert_eq!(gog.get("containsSymlink").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(gog.get("canonicalOverlapsPrefixSteam").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(gog.get("usesPrefixSteam").and_then(|value| value.as_bool()), Some(true));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn diagnostics_do_not_execute_wine_wrapper_for_version_probe() {
        let home = test_home("no-wine-exec");
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        let wine = ms_home.join("runtime").join("wine").join("bin").join("metalsharp-wine");
        let marker = home.join("wine-wrapper-executed");
        fs::create_dir_all(wine.parent().unwrap()).expect("create wine parent");
        fs::write(&wine, format!("#!/bin/sh\ntouch '{}'\necho wine-test\n", marker.to_string_lossy()))
            .expect("write wine wrapper");
        let mut perms = fs::metadata(&wine).expect("wine metadata").permissions();
        use std::os::unix::fs::PermissionsExt;
        perms.set_mode(0o755);
        fs::set_permissions(&wine, perms).expect("chmod wine wrapper");

        let report = runtime_diagnostics_report_for(&home);
        assert_eq!(report.get("readOnly").and_then(|value| value.as_bool()), Some(true));
        assert!(!marker.exists(), "runtime diagnostics must not execute the Wine wrapper");
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn diagnostics_do_not_authorize_install_wipe() {
        let home = test_home("replacement-guard");
        let report = runtime_diagnostics_report_for(&home);
        let guard = report.get("installReplacementGuard").expect("install replacement guard");
        assert_eq!(guard.get("allowedNow").and_then(|value| value.as_bool()), Some(false));
        assert_eq!(report.get("readOnly").and_then(|value| value.as_bool()), Some(true));
        let _ = fs::remove_dir_all(home);
    }
}
