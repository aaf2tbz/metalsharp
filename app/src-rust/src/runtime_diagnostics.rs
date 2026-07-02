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
    let gptk_prefix = ms_home.join("prefix-gptk");

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
    let source_report = source_readiness_report(&ms_home, &steam_prefix, &gog_prefix);
    let canonical_m12_ok = contracts.iter().any(|contract| {
        contract.id == "m12_dxmt_m12"
            && contract.runtime_surface_paths.iter().any(|path| path.ends_with("runtime/wine/lib/dxmt_m12"))
            && contract.runtime_surface_paths.iter().all(|path| !path.contains("dxmt-m12"))
    });
    let wine_binary = crate::platform::runtime_wine_binary(&wine_root);
    let wine_binary_present = file_nonempty(&wine_binary);
    let dxmt_current = crate::installer::dxmt_runtime_current_for_home(home);
    let dxmt_m12_current = crate::installer::dxmt_m12_runtime_current_for_home(home);
    let update_guard = crate::updater::update_source_guard_report();
    let update_guard_ok = update_guard.get("ok").and_then(|value| value.as_bool()).unwrap_or(false);
    let native_mono_report = serde_json::to_value(crate::fna_profile::native_mono_platform_doctor_for(home, None))
        .unwrap_or_else(|_| json!({ "ok": false, "error": "native Mono/FNA doctor serialization failed" }));
    let vulkan_report = vulkan_runtime_doctor_report(&wine_root, &ms_home);
    let runtime_ready = wine_binary_present && dxmt_current && dxmt_m12_current && manifest_ok;
    let ok = runtime_ready && prefix_ok && canonical_m12_ok && update_guard_ok;

    json!({
        "ok": ok,
        "schema": RUNTIME_DIAGNOSTICS_SCHEMA,
        "readOnly": true,
        "summary": runtime_summary(
            ok,
            runtime_ready,
            manifest_ok,
            dxmt_current,
            dxmt_m12_current,
            prefix_ok,
            update_guard_ok,
        ),
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
        "prefixMetadata": prefix_metadata_report(&manifest, &steam_prefix, &gog_prefix, &gptk_prefix),
        "sources": source_report,
        "nativeMono": native_mono_report.clone(),
        "vulkan": vulkan_report,
        "lanes": lane_readiness_report(
            &contracts,
            &LaneReadinessContext {
                wine_binary_present,
                dxmt_current,
                dxmt_m12_current,
                manifest_ok,
                prefix_ok,
                sources: &source_report,
                native_mono: &native_mono_report,
                artifacts: manifest.get("artifacts").unwrap_or(&Value::Null),
            },
        ),
        "updateGuard": update_guard,
        "installReplacementGuard": {
            "allowedNow": false,
            "reason": "Do not wipe or replace the current MetalSharp install from diagnostics. Replacement must be a final, explicit user-confirmed step after Wine 2.0 runtime validation passes."
        },
        "nextActions": next_actions(runtime_ready, manifest_ok, dxmt_current, dxmt_m12_current, prefix_ok, update_guard_ok),
    })
}

struct LaneReadinessContext<'a> {
    wine_binary_present: bool,
    dxmt_current: bool,
    dxmt_m12_current: bool,
    manifest_ok: bool,
    prefix_ok: bool,
    sources: &'a Value,
    native_mono: &'a Value,
    artifacts: &'a Value,
}

fn lane_readiness_report(
    contracts: &[crate::runtime_contracts::RuntimeLaneContract],
    context: &LaneReadinessContext<'_>,
) -> Value {
    let entries: Vec<Value> = contracts.iter().map(|contract| lane_readiness_entry(contract, context)).collect();
    let ready =
        entries.iter().filter(|entry| entry.get("ready").and_then(|value| value.as_bool()) == Some(true)).count();
    let available_total = contracts
        .iter()
        .filter(|contract| matches!(contract.status, crate::runtime_contracts::RuntimeLaneStatus::Available))
        .count();
    let available_ready = contracts
        .iter()
        .zip(entries.iter())
        .filter(|(contract, entry)| {
            matches!(contract.status, crate::runtime_contracts::RuntimeLaneStatus::Available)
                && entry.get("ready").and_then(|value| value.as_bool()) == Some(true)
        })
        .count();
    let planned = contracts
        .iter()
        .filter(|contract| matches!(contract.status, crate::runtime_contracts::RuntimeLaneStatus::Planned))
        .count();
    let external = contracts
        .iter()
        .filter(|contract| matches!(contract.status, crate::runtime_contracts::RuntimeLaneStatus::External))
        .count();

    json!({
        "total": entries.len(),
        "ready": ready,
        "availableTotal": available_total,
        "availableReady": available_ready,
        "planned": planned,
        "external": external,
        "entries": entries,
    })
}

fn lane_readiness_entry(
    contract: &crate::runtime_contracts::RuntimeLaneContract,
    context: &LaneReadinessContext<'_>,
) -> Value {
    let mut blockers = Vec::new();

    match contract.status {
        crate::runtime_contracts::RuntimeLaneStatus::Planned => blockers.push("lane_planned"),
        crate::runtime_contracts::RuntimeLaneStatus::External => blockers.push("external_runtime"),
        crate::runtime_contracts::RuntimeLaneStatus::Available => {},
    }

    if contract.requires_wine && !context.wine_binary_present {
        blockers.push("wine_binary");
    }
    if contract.requires_wine && !context.manifest_ok {
        blockers.push("runtime_manifest");
    }

    match contract.id {
        "native_mono_arm64" | "native_mono_x86" => {
            blockers.extend(native_mono_lane_blockers(contract.id, context.native_mono));
        },
        "m9" | "m10" | "m11" if !context.dxmt_current => blockers.push("dxmt_runtime"),
        "m12_dxmt_m12" if !context.dxmt_m12_current => blockers.push("dxmt_m12_runtime"),
        "dxvk_d9" | "dxvk_d11" if !lane_artifacts_all_present(contract.id, context.artifacts) => {
            blockers.push("dxvk_runtime");
        },
        "vkd3d_d12" if !lane_artifacts_all_present(contract.id, context.artifacts) => {
            blockers.push("vkd3d_runtime");
        },
        "steam_background" => {
            if !context
                .sources
                .get("steam")
                .and_then(|steam| steam.get("prefixPresent"))
                .and_then(|value| value.as_bool())
                .unwrap_or(false)
            {
                blockers.push("steam_prefix");
            }
        },
        "gogdl_wine" => {
            if !context.prefix_ok {
                blockers.push("gog_prefix_policy");
            }
            if !context
                .sources
                .get("gog")
                .and_then(|gog| gog.get("ok"))
                .and_then(|value| value.as_bool())
                .unwrap_or(false)
            {
                blockers.push("gogdl_source");
            }
        },
        _ => {},
    }

    json!({
        "id": contract.id,
        "name": contract.name,
        "family": contract.family,
        "status": contract.status,
        "ready": blockers.is_empty(),
        "blockers": blockers,
        "requiresWine": contract.requires_wine,
        "sourceScopes": &contract.source_scopes,
        "runtimeSurfacePaths": &contract.runtime_surface_paths,
        "artifactSummary": lane_artifact_summary(contract.id, context.artifacts),
    })
}

fn native_mono_lane_blockers(lane_id: &str, native_mono: &Value) -> Vec<&'static str> {
    let mut blockers = Vec::new();
    let support_ok =
        native_mono.get("support_inventory").and_then(|inventory| inventory.as_array()).is_some_and(|inventory| {
            inventory.iter().all(|entry| {
                !entry.get("required").and_then(|value| value.as_bool()).unwrap_or(false)
                    || entry.get("present").and_then(|value| value.as_bool()) == Some(true)
            })
        });
    if !support_ok {
        blockers.push("native_mono_support_assets");
    }

    let lane = native_mono
        .get("lanes")
        .and_then(|lanes| lanes.as_array())
        .and_then(|lanes| lanes.iter().find(|lane| lane.get("id").and_then(|value| value.as_str()) == Some(lane_id)));
    let Some(lane) = lane else {
        blockers.push("native_mono_doctor");
        return blockers;
    };
    if let Some(lane_blockers) = lane.get("blockers").and_then(|value| value.as_array()) {
        for blocker in lane_blockers {
            match blocker.as_str() {
                Some("mono_binary") => blockers.push("mono_binary"),
                Some("mono_arch") => blockers.push("mono_arch"),
                Some(_) => blockers.push("native_mono_lane"),
                None => {},
            }
        }
    }
    blockers.sort_unstable();
    blockers.dedup();
    blockers
}

fn lane_artifacts_all_present(lane_id: &str, artifacts: &Value) -> bool {
    lane_artifact_report(lane_id, artifacts)
        .and_then(|report| report.get("all_present"))
        .and_then(|value| value.as_bool())
        .unwrap_or(false)
}

fn lane_artifact_summary(lane_id: &str, artifacts: &Value) -> Value {
    let Some(report) = lane_artifact_report(lane_id, artifacts) else {
        return Value::Null;
    };
    let mut total = 0_u64;
    let mut present = 0_u64;
    collect_artifact_counts(report, &mut total, &mut present);
    json!({
        "total": total,
        "present": present,
        "missing": total.saturating_sub(present),
        "allPresent": total > 0 && present == total,
    })
}

fn lane_artifact_report<'a>(lane_id: &str, artifacts: &'a Value) -> Option<&'a Value> {
    match lane_id {
        "m9" | "m10" | "m11" => artifacts.get("dxmt"),
        "m12_dxmt_m12" => artifacts.get("dxmt_m12"),
        "dxvk_d9" => artifacts.get("planned").and_then(|planned| planned.get("dxvk")).and_then(|dxvk| dxvk.get("d9")),
        "dxvk_d11" => artifacts.get("planned").and_then(|planned| planned.get("dxvk")).and_then(|dxvk| dxvk.get("d11")),
        "vkd3d_d12" => {
            artifacts.get("planned").and_then(|planned| planned.get("vkd3d")).and_then(|vkd3d| vkd3d.get("d12"))
        },
        _ => None,
    }
}

fn collect_artifact_counts(value: &Value, total: &mut u64, present: &mut u64) {
    if let Some(entries) = value.get("entries").and_then(|entries| entries.as_array()) {
        for entry in entries {
            *total += 1;
            if entry.get("present").and_then(|present| present.as_bool()).unwrap_or(false) {
                *present += 1;
            }
        }
    }
    if let Some(object) = value.as_object() {
        for child in object.values() {
            if child.is_object() {
                collect_artifact_counts(child, total, present);
            }
        }
    }
}

fn vulkan_runtime_doctor_report(wine_root: &Path, ms_home: &Path) -> Value {
    let runtime_lib = wine_root.join("lib");
    let dxvk_root = runtime_lib.join("dxvk");
    let vkd3d_root = runtime_lib.join("vkd3d");
    let moltenvk_runtime = runtime_lib.join("wine").join("x86_64-unix").join("libMoltenVK.dylib");
    let icd_dir = wine_root.join("etc").join("vulkan").join("icd.d");
    let dxvk_report = vulkan_surface_report(
        &dxvk_root,
        &[
            "x86_64-windows/d3d9.dll",
            "i386-windows/d3d9.dll",
            "i386-windows/d3d10core.dll",
            "i386-windows/d3d11.dll",
            "i386-windows/dxgi.dll",
            "x86_64-windows/d3d10core.dll",
            "x86_64-windows/d3d11.dll",
            "x86_64-windows/dxgi.dll",
            "x86_64-unix/libMoltenVK.dylib",
        ],
    );
    let vkd3d_report = vulkan_surface_report(
        &vkd3d_root,
        &[
            "x86_64-windows/d3d12.dll",
            "x86_64-windows/d3d12core.dll",
            "x86_64-windows/dxgi.dll",
            "x86_64-unix/libMoltenVK.dylib",
        ],
    );
    let icd_report = vulkan_icd_report(&icd_dir, &moltenvk_runtime);
    let dxvk_state_cache = dxvk_state_cache_report(ms_home);
    let dxvk_ok = dxvk_report.get("ok").and_then(|value| value.as_bool()).unwrap_or(false);
    let vkd3d_ok = vkd3d_report.get("ok").and_then(|value| value.as_bool()).unwrap_or(false);
    let icd_ok = icd_report.get("ok").and_then(|value| value.as_bool()).unwrap_or(false);
    let dxvk_state_cache_ok = dxvk_state_cache.get("ok").and_then(|value| value.as_bool()).unwrap_or(false);

    json!({
        "ok": dxvk_ok && vkd3d_ok && icd_ok && dxvk_state_cache_ok,
        "readOnly": true,
        "runtimeLibraryRoot": runtime_lib.to_string_lossy(),
        "dxvk": dxvk_report,
        "vkd3d": vkd3d_report,
        "icd": icd_report,
        "dxvkStateCache": dxvk_state_cache,
        "limitations": vulkan_limitations_report(),
    })
}

fn vulkan_limitations_report() -> Value {
    json!([
        {
            "id": "filesystem_only",
            "severity": "info",
            "detail": "This doctor verifies files, ICD paths, and cache permissions only; it does not execute Wine, Vulkan, DXVK, VKD3D-Proton, or MoltenVK."
        },
        {
            "id": "moltenvk_feature_level_unproven",
            "severity": "warning",
            "detail": "MoltenVK feature support is not proven by filesystem readiness. VKD3D-Proton remains an experimental fallback until per-game launch validation confirms required Vulkan features."
        },
        {
            "id": "m12_not_replaced",
            "severity": "info",
            "detail": "VKD3D-Proton is reported as a fallback lane and must not silently replace M12/dxmt_m12 for D3D12 games."
        }
    ])
}

fn dxvk_state_cache_report(ms_home: &Path) -> Value {
    let cache_root = ms_home.join("shader-cache");
    let entries: Vec<Value> = ["dxvk-d9", "dxvk-d11"]
        .into_iter()
        .map(|lane| {
            let path = cache_root.join(lane);
            let probe = non_mutating_writable_path_report(&path);
            json!({
                "lane": lane,
                "path": path.to_string_lossy(),
                "exists": path.is_dir(),
                "writableByMode": probe.get("writableByMode").and_then(|value| value.as_bool()).unwrap_or(false),
                "checkedPath": probe.get("checkedPath").cloned().unwrap_or(Value::Null),
                "reason": probe.get("reason").cloned().unwrap_or(Value::Null),
            })
        })
        .collect();
    let ok = entries.iter().all(|entry| entry.get("writableByMode").and_then(|value| value.as_bool()) == Some(true));
    json!({
        "ok": ok,
        "readOnly": true,
        "cacheRoot": cache_root.to_string_lossy(),
        "method": "permission_bits_no_probe_file",
        "entries": entries,
    })
}

fn non_mutating_writable_path_report(path: &Path) -> Value {
    let checked = if path.exists() {
        path.to_path_buf()
    } else {
        path.ancestors().skip(1).find(|ancestor| ancestor.exists()).unwrap_or(path).to_path_buf()
    };
    let metadata = std::fs::metadata(&checked).ok();
    let writable = metadata.as_ref().is_some_and(|metadata| metadata.is_dir() && !metadata.permissions().readonly());
    let reason = if path.exists() {
        if writable {
            "target_dir_writable_by_mode"
        } else {
            "target_dir_not_writable_by_mode"
        }
    } else if writable {
        "nearest_existing_parent_writable_by_mode"
    } else {
        "nearest_existing_parent_not_writable_by_mode"
    };
    json!({
        "writableByMode": writable,
        "checkedPath": checked.to_string_lossy(),
        "reason": reason,
    })
}

fn vulkan_surface_report(root: &Path, required: &[&str]) -> Value {
    let entries: Vec<Value> = required
        .iter()
        .map(|relative| {
            let path = root.join(relative);
            json!({
                "path": path.to_string_lossy(),
                "relativePath": relative,
                "present": file_nonempty(&path),
            })
        })
        .collect();
    let present =
        entries.iter().filter(|entry| entry.get("present").and_then(|value| value.as_bool()) == Some(true)).count();

    json!({
        "ok": present == required.len(),
        "root": root.to_string_lossy(),
        "present": present,
        "total": required.len(),
        "missing": required.len().saturating_sub(present),
        "entries": entries,
    })
}

fn vulkan_icd_report(icd_dir: &Path, moltenvk_runtime: &Path) -> Value {
    let mut entries = Vec::new();
    if let Ok(read_dir) = std::fs::read_dir(icd_dir) {
        for entry in read_dir.flatten() {
            let path = entry.path();
            let name = entry.file_name().to_string_lossy().to_string();
            if !name.starts_with("MoltenVK") || !name.ends_with(".json") {
                continue;
            }
            let library_path = std::fs::read_to_string(&path)
                .ok()
                .and_then(|data| serde_json::from_str::<Value>(&data).ok())
                .and_then(|data| {
                    data.get("ICD")
                        .and_then(|icd| icd.get("library_path"))
                        .and_then(|value| value.as_str())
                        .map(str::to_owned)
                });
            let expected_library_path = moltenvk_runtime.to_string_lossy().to_string();
            let points_to_runtime_moltenvk = library_path.as_deref() == Some(expected_library_path.as_str());
            entries.push(json!({
                "path": path.to_string_lossy(),
                "libraryPath": library_path,
                "pointsToRuntimeMoltenVK": points_to_runtime_moltenvk,
            }));
        }
    }
    let present = !entries.is_empty();
    let all_point_to_runtime = present
        && entries
            .iter()
            .all(|entry| entry.get("pointsToRuntimeMoltenVK").and_then(|value| value.as_bool()) == Some(true));

    json!({
        "ok": present && file_nonempty(moltenvk_runtime) && all_point_to_runtime,
        "icdDir": icd_dir.to_string_lossy(),
        "moltenvkRuntimePath": moltenvk_runtime.to_string_lossy(),
        "moltenvkRuntimePresent": file_nonempty(moltenvk_runtime),
        "present": present,
        "allPointToRuntimeMoltenVK": all_point_to_runtime,
        "entries": entries,
    })
}

fn prefix_metadata_report(manifest: &Value, steam_prefix: &Path, gog_prefix: &Path, gptk_prefix: &Path) -> Value {
    let wine_runtime_version = manifest
        .get("expected")
        .and_then(|expected| expected.get("wineVersion"))
        .or_else(|| manifest.get("persisted").and_then(|persisted| persisted.get("wineVersion")))
        .and_then(|value| value.as_str())
        .map(str::to_owned);
    json!({
        "schema": "metalsharp.prefix.metadata.v2.preview",
        "readOnly": true,
        "entries": [
            prefix_metadata_entry(
                "steam",
                "steam",
                steam_prefix,
                wine_runtime_version.as_deref(),
                "steam-metadata-only",
                "external",
                "Wine Steam shared session prefix; preserve metadata/manifests/config/registry/DLLs and exclude game payloads during migration."
            ),
            prefix_metadata_entry(
                "gog",
                "gog",
                gog_prefix,
                wine_runtime_version.as_deref(),
                "preserve-if-present",
                "source-owned-prefix",
                "Dedicated GOGDL source prefix; preserve full prefix only when present and never alias prefix-steam."
            ),
            prefix_metadata_entry(
                "gptk",
                "gptk",
                gptk_prefix,
                None,
                "preserve-if-present",
                "external-gptk-prefix",
                "Homebrew GPTK-owned prefix; managed separately from DXMT/Wine surfaces and never silently mixed with prefix-steam."
            ),
        ],
    })
}

fn prefix_metadata_entry(
    id: &str,
    owner: &str,
    path: &Path,
    wine_runtime_version: Option<&str>,
    preserve_policy: &str,
    game_payload_policy: &str,
    notes: &str,
) -> Value {
    let persisted = crate::prefix_metadata::read_metadata(path);
    let latest_wineboot = crate::prefix_metadata::latest_wineboot_receipt(path)
        .or_else(|| persisted.as_ref().and_then(|value| value.get("lastWinebootUpdate").cloned()))
        .unwrap_or(serde_json::Value::Null);
    json!({
        "schema": "metalsharp.prefix.v2",
        "id": id,
        "owner": owner,
        "path": path.to_string_lossy(),
        "present": path.is_dir(),
        "driveCPresent": path.join("drive_c").is_dir(),
        "systemRegPresent": path.join("system.reg").is_file(),
        "userRegPresent": path.join("user.reg").is_file(),
        "canonicalPath": fs_canonicalize_if_exists(path).as_ref().map(|path| path.to_string_lossy().to_string()),
        "containsSymlink": path_contains_symlink_below(path.parent().unwrap_or(Path::new("/")), path),
        "wineRuntimeVersion": wine_runtime_version,
        "metadataPath": crate::prefix_metadata::metadata_path(path).to_string_lossy(),
        "metadataPersisted": persisted.is_some(),
        "persisted": persisted,
        "lastWinebootUpdate": latest_wineboot,
        "installedComponents": [],
        "preservePolicy": preserve_policy,
        "gamePayloadPolicy": game_payload_policy,
        "notes": notes,
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

fn source_readiness_report(ms_home: &Path, steam_prefix: &Path, gog_prefix: &Path) -> Value {
    let gog_auth = ms_home.join("gog_store").join("auth.json");
    let gog_config = ms_home.join("gogdl");
    let gog_support = gog_config.join("gog-support");
    let gogdl_binary = gogdl_binary_for(ms_home);
    let gogdl_present = gogdl_binary.as_ref().is_some_and(|path| file_nonempty(path));
    let gog_auth_present = file_nonempty(&gog_auth);
    let gog_prefix_present = gog_prefix.is_dir();

    json!({
        "steam": {
            "id": "steam_background",
            "prefixPath": steam_prefix.to_string_lossy(),
            "prefixPresent": steam_prefix.is_dir(),
            "usesDedicatedPrefix": true,
        },
        "gog": {
            "id": "gogdl_wine",
            "ok": gogdl_present && gog_auth_present && gog_prefix_present,
            "gogdlAvailable": gogdl_present,
            "gogdlPath": gogdl_binary.as_ref().map(|path| path.to_string_lossy().to_string()),
            "authPresent": gog_auth_present,
            "authPath": gog_auth.to_string_lossy(),
            "configPath": gog_config.to_string_lossy(),
            "supportPath": gog_support.to_string_lossy(),
            "prefixPath": gog_prefix.to_string_lossy(),
            "prefixPresent": gog_prefix_present,
            "mustNotUsePrefixSteam": true,
        },
    })
}

fn gogdl_binary_for(ms_home: &Path) -> Option<PathBuf> {
    gogdl_candidates_for(ms_home).into_iter().find(|path| file_nonempty(path))
}

fn gogdl_candidates_for(ms_home: &Path) -> Vec<PathBuf> {
    let mut candidates = Vec::new();
    if let Ok(explicit) = std::env::var("METALSHARP_GOGDL_BIN") {
        let explicit = explicit.trim();
        if !explicit.is_empty() {
            candidates.push(PathBuf::from(explicit));
        }
    }
    candidates.push(ms_home.join("tools").join("gogdl"));
    candidates.push(ms_home.join("runtime").join("gogdl"));
    if let Ok(path_env) = std::env::var("PATH") {
        for path in std::env::split_paths(&path_env) {
            candidates.push(path.join("gogdl"));
        }
    }
    candidates
}

fn runtime_summary(
    ok: bool,
    runtime_ready: bool,
    manifest_ok: bool,
    dxmt_current: bool,
    dxmt_m12_current: bool,
    prefix_ok: bool,
    update_guard_ok: bool,
) -> &'static str {
    if ok {
        return "Runtime diagnostics passed: Wine, DXMT, dxmt_m12, manifest, prefix policies, and update guard are ready.";
    }
    if !update_guard_ok {
        return "Runtime diagnostics blocked: private fork update guard is not safe.";
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
    update_guard_ok: bool,
) -> Vec<&'static str> {
    let mut actions = Vec::new();
    if !update_guard_ok {
        actions.push("Disable public updates or configure METALSHARP_UPDATE_REPO_API to an intentional private Wine 2.0 release feed before replacement.");
    }
    if !prefix_ok {
        actions.push("Keep GOG on ~/.metalsharp/bottles/gog-prefix/prefix and Steam on ~/.metalsharp/prefix-steam; do not merge prefixes.");
    }
    if !dxmt_current || !dxmt_m12_current {
        actions.push("Run setup/install-all or the graphics runtime repair flow before launching Wine-backed routes.");
    }
    if !manifest_ok {
        actions.push("Regenerate the runtime manifest through setup after runtime assets are staged, then re-check /runtime/manifest.");
    }
    if runtime_ready && prefix_ok && update_guard_ok {
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

    fn write_thin_macho(path: &Path, cpu: u32) {
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&0xfeedfacfu32.to_le_bytes());
        bytes.extend_from_slice(&cpu.to_le_bytes());
        bytes.extend_from_slice(&[0; 24]);
        fs::create_dir_all(path.parent().unwrap()).expect("create macho parent");
        fs::write(path, bytes).expect("write macho");
    }

    fn stage_native_mono_support_assets(runtime_root: &Path) {
        for path in [
            runtime_root.join("fna/FNA.dll"),
            runtime_root.join("fnalibs/libSDL2-2.0.0.dylib"),
            runtime_root.join("fnalibs/libFNA3D.0.dylib"),
            runtime_root.join("fnalibs/libFAudio.0.dylib"),
            runtime_root.join("shims/libCarbon.dylib"),
            runtime_root.join("shims/libuser32.dylib"),
            runtime_root.join("shims/libkernel32.dylib"),
        ] {
            fs::create_dir_all(path.parent().unwrap()).expect("create asset parent");
            fs::write(path, b"asset").expect("write asset");
        }
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
    fn diagnostics_report_prefix_metadata_v2_preview() {
        let home = test_home("prefix-metadata");
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        fs::create_dir_all(ms_home.join("prefix-steam/drive_c")).expect("create steam drive_c");
        fs::write(ms_home.join("prefix-steam/system.reg"), b"registry").expect("write system reg");
        fs::create_dir_all(ms_home.join("bottles/gog-prefix/prefix/drive_c")).expect("create gog drive_c");
        crate::prefix_metadata::record_wineboot_decision(
            &ms_home.join("bottles/gog-prefix/prefix"),
            "gog",
            "wineboot -u",
            &["metalsharp-wine", "wineboot", "-u"],
            "success",
            "test receipt",
            Some(0),
        )
        .expect("record gog wineboot receipt");
        let report = runtime_diagnostics_report_for(&home);
        let metadata = report.get("prefixMetadata").expect("prefix metadata");
        assert_eq!(
            metadata.get("schema").and_then(|value| value.as_str()),
            Some("metalsharp.prefix.metadata.v2.preview")
        );
        assert_eq!(metadata.get("readOnly").and_then(|value| value.as_bool()), Some(true));
        let entries = metadata.get("entries").and_then(|value| value.as_array()).expect("prefix entries");
        let steam = entries
            .iter()
            .find(|entry| entry.get("id").and_then(|value| value.as_str()) == Some("steam"))
            .expect("steam metadata");
        assert_eq!(steam.get("schema").and_then(|value| value.as_str()), Some("metalsharp.prefix.v2"));
        assert_eq!(steam.get("owner").and_then(|value| value.as_str()), Some("steam"));
        assert_eq!(steam.get("present").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(steam.get("driveCPresent").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(steam.get("preservePolicy").and_then(|value| value.as_str()), Some("steam-metadata-only"));
        assert_eq!(steam.get("gamePayloadPolicy").and_then(|value| value.as_str()), Some("external"));
        let gog = entries
            .iter()
            .find(|entry| entry.get("id").and_then(|value| value.as_str()) == Some("gog"))
            .expect("gog metadata");
        assert_eq!(gog.get("preservePolicy").and_then(|value| value.as_str()), Some("preserve-if-present"));
        assert_eq!(gog.get("metadataPersisted").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(gog.pointer("/lastWinebootUpdate/outcome").and_then(|value| value.as_str()), Some("success"));
        assert!(gog
            .get("path")
            .and_then(|value| value.as_str())
            .unwrap_or_default()
            .ends_with("bottles/gog-prefix/prefix"));
        let gptk = entries
            .iter()
            .find(|entry| entry.get("id").and_then(|value| value.as_str()) == Some("gptk"))
            .expect("gptk metadata");
        assert_eq!(gptk.get("gamePayloadPolicy").and_then(|value| value.as_str()), Some("external-gptk-prefix"));
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
    fn diagnostics_report_gogdl_source_readiness_without_using_prefix_steam() {
        let home = test_home("gog-source");
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        let gogdl = ms_home.join("tools").join("gogdl");
        let auth = ms_home.join("gog_store").join("auth.json");
        let gog_prefix = ms_home.join("bottles").join("gog-prefix").join("prefix");
        fs::create_dir_all(gogdl.parent().unwrap()).expect("create gogdl parent");
        fs::create_dir_all(auth.parent().unwrap()).expect("create auth parent");
        fs::create_dir_all(&gog_prefix).expect("create gog prefix");
        fs::write(&gogdl, b"#!/bin/sh\necho gogdl\n").expect("write gogdl");
        fs::write(&auth, br#"{"access_token":"test-token"}"#).expect("write auth");

        let report = runtime_diagnostics_report_for(&home);
        let gog = report.get("sources").and_then(|sources| sources.get("gog")).expect("gog source diagnostics");
        assert_eq!(gog.get("ok").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(gog.get("gogdlAvailable").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(gog.get("authPresent").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(gog.get("prefixPresent").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(gog.get("mustNotUsePrefixSteam").and_then(|value| value.as_bool()), Some(true));
        assert!(gog
            .get("prefixPath")
            .and_then(|value| value.as_str())
            .expect("gog prefix path")
            .ends_with(".metalsharp/bottles/gog-prefix/prefix"));
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
    fn diagnostics_include_native_mono_platform_doctor() {
        let home = test_home("native-mono-doctor");
        let report = runtime_diagnostics_report_for(&home);
        let native_mono = report.get("nativeMono").expect("native mono doctor");
        assert_eq!(native_mono.get("read_only").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(native_mono.get("ok").and_then(|value| value.as_bool()), Some(false));
        let lanes = native_mono.get("lanes").and_then(|value| value.as_array()).expect("native mono lanes");
        assert_eq!(lanes.len(), 2);
        assert!(lanes.iter().any(|lane| lane.get("id").and_then(|value| value.as_str()) == Some("native_mono_arm64")));
        assert!(lanes.iter().any(|lane| lane.get("id").and_then(|value| value.as_str()) == Some("native_mono_x86")));
        let readiness_lanes = report
            .get("lanes")
            .and_then(|value| value.get("entries"))
            .and_then(|value| value.as_array())
            .expect("runtime lane entries");
        let arm64 = readiness_lanes
            .iter()
            .find(|lane| lane.get("id").and_then(|value| value.as_str()) == Some("native_mono_arm64"))
            .expect("arm64 lane readiness");
        let blockers = arm64.get("blockers").and_then(|value| value.as_array()).expect("arm64 blockers");
        assert!(blockers.iter().any(|blocker| blocker.as_str() == Some("mono_binary")));
        assert!(blockers.iter().any(|blocker| blocker.as_str() == Some("native_mono_support_assets")));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn diagnostics_use_native_mono_doctor_for_lane_readiness() {
        let home = test_home("native-mono-ready");
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        let runtime = ms_home.join("runtime");
        write_thin_macho(&runtime.join("mono-arm64/bin/mono"), 0x0100_000c);
        write_thin_macho(&runtime.join("mono-x86/bin/mono"), 0x0100_0007);
        stage_native_mono_support_assets(&runtime);
        let report = runtime_diagnostics_report_for(&home);
        let native_mono = report.get("nativeMono").expect("native mono doctor");
        assert_eq!(native_mono.get("ok").and_then(|value| value.as_bool()), Some(true));
        let readiness_lanes = report
            .get("lanes")
            .and_then(|value| value.get("entries"))
            .and_then(|value| value.as_array())
            .expect("runtime lane entries");
        for id in ["native_mono_arm64", "native_mono_x86"] {
            let lane = readiness_lanes
                .iter()
                .find(|lane| lane.get("id").and_then(|value| value.as_str()) == Some(id))
                .expect("native mono lane readiness");
            assert_eq!(lane.get("ready").and_then(|value| value.as_bool()), Some(true), "{id}");
            assert_eq!(lane.get("blockers").and_then(|value| value.as_array()).map(Vec::len), Some(0), "{id}");
        }
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn diagnostics_report_lane_readiness_blockers() {
        let home = test_home("lane-blockers");
        let report = runtime_diagnostics_report_for(&home);
        let lanes = report.get("lanes").expect("lanes");
        assert_eq!(lanes.get("total").and_then(|value| value.as_u64()), Some(13));
        assert_eq!(lanes.get("availableTotal").and_then(|value| value.as_u64()), Some(12));
        assert_eq!(lanes.get("planned").and_then(|value| value.as_u64()), Some(0));
        assert_eq!(lanes.get("external").and_then(|value| value.as_u64()), Some(1));
        let entries = lanes.get("entries").and_then(|entries| entries.as_array()).expect("lane entries");
        let m12 = entries
            .iter()
            .find(|entry| entry.get("id").and_then(|value| value.as_str()) == Some("m12_dxmt_m12"))
            .expect("m12 lane");
        assert_eq!(m12.get("ready").and_then(|value| value.as_bool()), Some(false));
        assert!(m12
            .get("blockers")
            .and_then(|value| value.as_array())
            .expect("m12 blockers")
            .iter()
            .any(|blocker| blocker.as_str() == Some("dxmt_m12_runtime")));
        let dxvk = entries
            .iter()
            .find(|entry| entry.get("id").and_then(|value| value.as_str()) == Some("dxvk_d11"))
            .expect("dxvk d11 lane");
        assert_eq!(dxvk.get("ready").and_then(|value| value.as_bool()), Some(false));
        let dxvk_blockers = dxvk.get("blockers").and_then(|value| value.as_array()).expect("dxvk blockers");
        assert!(!dxvk_blockers.iter().any(|blocker| blocker.as_str() == Some("lane_planned")));
        assert!(dxvk_blockers.iter().any(|blocker| blocker.as_str() == Some("dxvk_runtime")));
        let dxvk_artifacts = dxvk.get("artifactSummary").expect("dxvk artifact summary");
        assert_eq!(dxvk_artifacts.get("total").and_then(|value| value.as_u64()), Some(7));
        assert_eq!(dxvk_artifacts.get("present").and_then(|value| value.as_u64()), Some(0));
        assert_eq!(dxvk_artifacts.get("missing").and_then(|value| value.as_u64()), Some(7));

        let vkd3d = entries
            .iter()
            .find(|entry| entry.get("id").and_then(|value| value.as_str()) == Some("vkd3d_d12"))
            .expect("vkd3d lane");
        let vkd3d_blockers = vkd3d.get("blockers").and_then(|value| value.as_array()).expect("vkd3d blockers");
        assert!(!vkd3d_blockers.iter().any(|blocker| blocker.as_str() == Some("lane_planned")));
        assert!(vkd3d_blockers.iter().any(|blocker| blocker.as_str() == Some("vkd3d_runtime")));
        let vkd3d_artifacts = vkd3d.get("artifactSummary").expect("vkd3d artifact summary");
        assert_eq!(vkd3d_artifacts.get("total").and_then(|value| value.as_u64()), Some(4));
        assert_eq!(vkd3d_artifacts.get("present").and_then(|value| value.as_u64()), Some(0));
        assert_eq!(vkd3d_artifacts.get("missing").and_then(|value| value.as_u64()), Some(4));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn diagnostics_report_vulkan_runtime_doctor_from_filesystem_only() {
        let home = test_home("vulkan-doctor");
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        let wine_root = ms_home.join("runtime").join("wine");
        let dxvk_root = wine_root.join("lib").join("dxvk");
        let vkd3d_root = wine_root.join("lib").join("vkd3d");
        let moltenvk_runtime = wine_root.join("lib").join("wine").join("x86_64-unix").join("libMoltenVK.dylib");
        let icd_dir = wine_root.join("etc").join("vulkan").join("icd.d");

        for relative in [
            "x86_64-windows/d3d9.dll",
            "i386-windows/d3d9.dll",
            "i386-windows/d3d10core.dll",
            "i386-windows/d3d11.dll",
            "i386-windows/dxgi.dll",
            "x86_64-windows/d3d10core.dll",
            "x86_64-windows/d3d11.dll",
            "x86_64-windows/dxgi.dll",
            "x86_64-unix/libMoltenVK.dylib",
        ] {
            let path = dxvk_root.join(relative);
            fs::create_dir_all(path.parent().unwrap()).expect("create dxvk parent");
            fs::write(path, b"dxvk").expect("write dxvk file");
        }
        for relative in [
            "x86_64-windows/d3d12.dll",
            "x86_64-windows/d3d12core.dll",
            "x86_64-windows/dxgi.dll",
            "x86_64-unix/libMoltenVK.dylib",
        ] {
            let path = vkd3d_root.join(relative);
            fs::create_dir_all(path.parent().unwrap()).expect("create vkd3d parent");
            fs::write(path, b"vkd3d").expect("write vkd3d file");
        }
        fs::create_dir_all(moltenvk_runtime.parent().unwrap()).expect("create moltenvk parent");
        fs::write(&moltenvk_runtime, b"moltenvk").expect("write runtime moltenvk");
        fs::create_dir_all(&icd_dir).expect("create icd dir");
        fs::write(
            icd_dir.join("MoltenVK_icd.json"),
            serde_json::json!({ "ICD": { "library_path": moltenvk_runtime.to_string_lossy() } }).to_string(),
        )
        .expect("write icd json");

        let report = runtime_diagnostics_report_for(&home);
        let vulkan = report.get("vulkan").expect("vulkan doctor");
        assert_eq!(vulkan.get("readOnly").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(vulkan.get("ok").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(vulkan.get("dxvk").and_then(|value| value.get("present")).and_then(|value| value.as_u64()), Some(9));
        assert_eq!(
            vulkan.get("vkd3d").and_then(|value| value.get("present")).and_then(|value| value.as_u64()),
            Some(4)
        );
        assert_eq!(
            vulkan
                .get("icd")
                .and_then(|value| value.get("allPointToRuntimeMoltenVK"))
                .and_then(|value| value.as_bool()),
            Some(true)
        );
        let state_cache = vulkan.get("dxvkStateCache").expect("dxvk state cache doctor");
        assert_eq!(state_cache.get("readOnly").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(state_cache.get("ok").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(state_cache.get("method").and_then(|value| value.as_str()), Some("permission_bits_no_probe_file"));
        assert_eq!(state_cache.get("entries").and_then(|value| value.as_array()).map(Vec::len), Some(2));
        let limitations = vulkan.get("limitations").and_then(|value| value.as_array()).expect("vulkan limitations");
        assert!(limitations.iter().any(|limitation| {
            limitation.get("id").and_then(|value| value.as_str()) == Some("moltenvk_feature_level_unproven")
        }));
        assert!(limitations
            .iter()
            .any(|limitation| { limitation.get("id").and_then(|value| value.as_str()) == Some("m12_not_replaced") }));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn diagnostics_clear_gog_source_blocker_when_source_is_ready() {
        let home = test_home("gog-source-ready");
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        let gogdl = ms_home.join("tools").join("gogdl");
        let auth = ms_home.join("gog_store").join("auth.json");
        let gog_prefix = ms_home.join("bottles").join("gog-prefix").join("prefix");
        let wine = ms_home.join("runtime").join("wine").join("bin").join("metalsharp-wine");
        fs::create_dir_all(gogdl.parent().unwrap()).expect("create gogdl parent");
        fs::create_dir_all(auth.parent().unwrap()).expect("create auth parent");
        fs::create_dir_all(&gog_prefix).expect("create gog prefix");
        fs::create_dir_all(wine.parent().unwrap()).expect("create wine parent");
        fs::write(&gogdl, b"#!/bin/sh\necho gogdl\n").expect("write gogdl");
        fs::write(&auth, br#"{"access_token":"test-token"}"#).expect("write auth");
        fs::write(&wine, b"#!/bin/sh\necho wine\n").expect("write wine");

        let report = runtime_diagnostics_report_for(&home);
        let entries = report
            .get("lanes")
            .and_then(|lanes| lanes.get("entries"))
            .and_then(|entries| entries.as_array())
            .expect("lane entries");
        let gog = entries
            .iter()
            .find(|entry| entry.get("id").and_then(|value| value.as_str()) == Some("gogdl_wine"))
            .expect("gog lane");
        assert_eq!(gog.get("ready").and_then(|value| value.as_bool()), Some(false));
        assert!(!gog
            .get("blockers")
            .and_then(|value| value.as_array())
            .expect("gog blockers")
            .iter()
            .any(|blocker| blocker.as_str() == Some("gogdl_source")));
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
