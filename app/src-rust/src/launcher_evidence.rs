use crate::bottles::{self, AppDetection, BottleManifest};
use serde_json::{json, Map, Value};
use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::time::UNIX_EPOCH;
use walkdir::WalkDir;

const EVIDENCE_SCHEMA: &str = "metalsharp.launcher.evidence.v1";
const INVENTORY_SCHEMA: &str = "metalsharp.launcher.evidence.inventory.v1";
const TAIL_LINES: usize = 80;
const MAX_ARTIFACT_READ_BYTES: u64 = 1024 * 1024;
const MAX_LOG_DEPTH: usize = 10;

#[derive(Debug, Default)]
struct LauncherFacts {
    family: String,
    installed_exe: Option<String>,
    installer_launch_attempted: bool,
    direct_launcher_launch_attempted: bool,
    installer_auto_launch_evidence: bool,
    crash_reporter_evidence: bool,
    ea_inst_14_1603: bool,
    ea_hresult: Option<String>,
    ea_msi_package: Option<String>,
    ea_msi_log_empty: bool,
    ubisoft_launcher_started: bool,
    ubisoft_version: Option<String>,
}

pub fn launcher_evidence_inventory() -> Value {
    let targets: Vec<Value> =
        ["minecraft", "ea", "ubisoft"].into_iter().map(|family| launcher_evidence_target(family)).collect();
    let proven = targets
        .iter()
        .filter(|target| target.get("status").and_then(|value| value.as_str()) == Some("controlled_proof_recorded"))
        .count();
    json!({
        "ok": true,
        "schema": INVENTORY_SCHEMA,
        "readOnly": true,
        "summary": {
            "total": targets.len(),
            "controlledProofRecorded": proven,
            "pendingControlledProof": targets.len().saturating_sub(proven),
        },
        "targets": targets,
        "invariants": [
            "This inventory reads existing bottle manifests/logs only; it does not launch Wine, start launchers, repair bottles, or mutate prefixes.",
            "Controlled launcher proof for Minecraft/EA/Ubisoft requires explicit user approval."
        ],
    })
}

pub fn handle_launcher_evidence(body: &Map<String, Value>) -> Value {
    let id = match body.get("id").and_then(|v| v.as_str()) {
        Some(id) if !id.is_empty() => id.to_string(),
        _ => match body.get("family").and_then(|v| v.as_str()) {
            Some(family) => match latest_bottle_for_family(family) {
                Some(id) => id,
                None => return json!({"ok": false, "error": format!("no bottle found for launcher family {}", family)}),
            },
            None => return json!({"ok": false, "error": "id or family required"}),
        },
    };

    let manifest = match bottles::load_bottle(&id) {
        Ok(manifest) => manifest,
        Err(e) => return json!({"ok": false, "id": id, "error": e.to_string()}),
    };

    let family = launcher_family(&manifest);
    let artifacts = collect_launcher_artifacts(&manifest, &family);
    let detections = collect_launcher_detections(&manifest, &family);
    let facts = summarize_launcher(&manifest, &family, &artifacts, &detections);
    let status = launcher_status(&facts);

    json!({
        "ok": true,
        "schema": EVIDENCE_SCHEMA,
        "readOnly": true,
        "id": manifest.id,
        "family": family,
        "status": status,
        "summary": launcher_summary(&status, &facts),
        "bottle": {
            "name": manifest.name,
            "prefix": manifest.prefix_path,
            "runtimeProfile": format!("{:?}", manifest.runtime_profile).to_ascii_lowercase(),
            "sourceInstallerPath": manifest.source_installer_path,
            "lastLaunchLog": manifest.last_launch_log,
            "lastLaunchStatus": manifest.last_launch_status,
            "lastLaunchFinishedAt": manifest.last_launch_finished_at,
        },
        "facts": {
            "installedExe": facts.installed_exe,
            "installerLaunchAttempted": facts.installer_launch_attempted,
            "installerAutoLaunchEvidence": facts.installer_auto_launch_evidence,
            "directLauncherLaunchAttempted": facts.direct_launcher_launch_attempted,
            "crashReporterEvidence": facts.crash_reporter_evidence,
            "eaInst141603": facts.ea_inst_14_1603,
            "eaHresult": facts.ea_hresult,
            "eaMsiPackage": facts.ea_msi_package,
            "eaMsiLogEmpty": facts.ea_msi_log_empty,
            "ubisoftLauncherStarted": facts.ubisoft_launcher_started,
            "ubisoftVersion": facts.ubisoft_version,
        },
        "detections": detections,
        "artifacts": artifacts,
        "nextActions": launcher_next_actions(&status, &family),
    })
}

fn launcher_evidence_target(family: &str) -> Value {
    match latest_bottle_for_family(family) {
        Some(id) => {
            let mut body = Map::new();
            body.insert("id".to_string(), json!(id));
            let report = handle_launcher_evidence(&body);
            json!({
                "family": family,
                "status": launcher_inventory_status(&report),
                "bottleId": report.get("id").cloned().unwrap_or(Value::Null),
                "evidenceStatus": report.get("status").cloned().unwrap_or(Value::Null),
                "summary": report.get("summary").cloned().unwrap_or(Value::Null),
                "nextActions": report.get("nextActions").cloned().unwrap_or_else(|| json!([])),
                "report": report,
            })
        },
        None => json!({
            "family": family,
            "status": "pending_no_bottle_evidence",
            "bottleId": Value::Null,
            "evidenceStatus": Value::Null,
            "summary": format!("No existing launcher bottle evidence found for {family}."),
            "nextActions": ["Create or select a launcher proof bottle only after explicit user approval."],
        }),
    }
}

fn launcher_inventory_status(report: &Value) -> &'static str {
    match report.get("status").and_then(|value| value.as_str()) {
        Some("installed_launcher_detected") => "filesystem_validated",
        Some("ea_msi_1603") | Some("ubisoft_auto_started_then_crash_reporter") => "known_blocker_recorded",
        Some("ubisoft_installed_no_direct_launch_proof") | Some("installed_not_launched_directly") => {
            "pending_controlled_direct_launch"
        },
        _ => "pending_controlled_proof",
    }
}

fn latest_bottle_for_family(family: &str) -> Option<String> {
    let family_lc = family.to_ascii_lowercase();
    let tokens = family_tokens(&family_lc);
    let mut matches: Vec<(u64, String)> = Vec::new();
    let root = bottles::bottles_root();
    let entries = fs::read_dir(root).ok()?;
    for entry in entries.filter_map(Result::ok) {
        let path = entry.path().join("bottle.json");
        let Some(data) = fs::read_to_string(&path).ok() else {
            continue;
        };
        let Some(manifest) = serde_json::from_str::<BottleManifest>(&data).ok() else {
            continue;
        };
        let haystack =
            format!("{} {} {}", manifest.id, manifest.name, manifest.source_installer_path.clone().unwrap_or_default())
                .to_ascii_lowercase();
        if !tokens.iter().any(|token| haystack.contains(token)) {
            continue;
        }
        let modified = fs::metadata(&path)
            .and_then(|m| m.modified())
            .ok()
            .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
            .map(|d| d.as_secs())
            .unwrap_or(0);
        matches.push((modified, manifest.id));
    }
    matches.sort_by_key(|(modified, _)| *modified);
    matches.pop().map(|(_, id)| id)
}

fn family_tokens(family: &str) -> Vec<String> {
    match family {
        "ea" | "ea_app" | "origin" => vec!["eaapp".to_string(), "ea app".to_string(), "origin".to_string()],
        "minecraft" | "mojang" => vec!["minecraft".to_string(), "mojang".to_string()],
        "ubisoft" | "ubisoft_connect" | "uplay" => vec!["ubisoft".to_string(), "uplay".to_string()],
        _ => vec![family.to_string()],
    }
}

fn launcher_family(manifest: &BottleManifest) -> String {
    let haystack =
        format!("{} {} {}", manifest.id, manifest.name, manifest.source_installer_path.clone().unwrap_or_default())
            .to_ascii_lowercase();
    if haystack.contains("ubisoft") || haystack.contains("uplay") {
        "ubisoft".to_string()
    } else if haystack.contains("eaapp") || haystack.contains("ea app") || haystack.contains("origin") {
        "ea".to_string()
    } else if haystack.contains("minecraft") || haystack.contains("mojang") {
        "minecraft".to_string()
    } else {
        "unknown".to_string()
    }
}

fn collect_launcher_artifacts(manifest: &BottleManifest, family: &str) -> Vec<Value> {
    let prefix = PathBuf::from(&manifest.prefix_path);
    let mut candidates = Vec::new();
    if let Some(log) = manifest.last_launch_log.as_deref() {
        candidates.push(("last_launch_log", PathBuf::from(log)));
    }
    collect_logs_in(&bottles::bottle_logs_dir(&manifest.id), "bottle_log", &mut candidates);

    match family {
        "ea" => {
            collect_logs_in(&prefix.join("drive_c").join("users"), "ea_user_temp_log", &mut candidates);
            candidates.push((
                "ea_edge_update_log",
                prefix
                    .join("drive_c")
                    .join("ProgramData")
                    .join("Microsoft")
                    .join("EdgeUpdate")
                    .join("Log")
                    .join("MicrosoftEdgeUpdate.log"),
            ));
            candidates.push((
                "ea_webview2_installer_log",
                prefix.join("drive_c").join("Program Files").join("msedge_installer.log"),
            ));
        },
        "ubisoft" => {
            let root = prefix.join("drive_c").join("Program Files (x86)").join("Ubisoft").join("Ubisoft Game Launcher");
            collect_logs_in(&root.join("logs"), "ubisoft_launcher_log", &mut candidates);
        },
        _ => {
            collect_logs_in(&prefix.join("drive_c").join("users"), "launcher_user_log", &mut candidates);
            collect_logs_in(&prefix.join("drive_c").join("Program Files"), "launcher_program_log", &mut candidates);
            collect_logs_in(
                &prefix.join("drive_c").join("Program Files (x86)"),
                "launcher_program_x86_log",
                &mut candidates,
            );
        },
    }

    let mut seen = std::collections::HashSet::new();
    candidates
        .into_iter()
        .filter(|(_, path)| seen.insert(path.clone()))
        .map(|(id, path)| artifact_json(id, &path))
        .collect()
}

fn collect_logs_in(root: &Path, id: &'static str, candidates: &mut Vec<(&'static str, PathBuf)>) {
    if !root.exists() {
        return;
    }
    for entry in WalkDir::new(root).max_depth(MAX_LOG_DEPTH).into_iter().filter_map(Result::ok) {
        if !entry.file_type().is_file() {
            continue;
        }
        let path = entry.path();
        let name = path.file_name().and_then(|v| v.to_str()).unwrap_or("").to_ascii_lowercase();
        if matches!(
            path.extension().and_then(|v| v.to_str()).unwrap_or("").to_ascii_lowercase().as_str(),
            "log" | "txt"
        ) && (name.contains("ea")
            || name.contains("ubisoft")
            || name.contains("uplay")
            || name.contains("launcher")
            || name.contains("crash")
            || name.contains("msedge")
            || name.contains("edgeupdate"))
        {
            candidates.push((id, path.to_path_buf()));
        }
    }
}

fn collect_launcher_detections(manifest: &BottleManifest, family: &str) -> Vec<Value> {
    let mut detections: Vec<AppDetection> = manifest.installed_app_detections.clone();
    let prefix = PathBuf::from(&manifest.prefix_path);
    for exe in launcher_exe_candidates(&prefix, family) {
        let exe_str = exe.to_string_lossy().to_string();
        if detections.iter().any(|d| d.exe_path == exe_str) {
            continue;
        }
        let name = exe.file_stem().and_then(|v| v.to_str()).unwrap_or("Launcher").to_string();
        detections.push(AppDetection { name, exe_path: exe_str, source: "launcher_evidence_scan".to_string() });
    }
    detections.into_iter().map(|d| json!({"name": d.name, "exePath": d.exe_path, "source": d.source})).collect()
}

fn launcher_exe_candidates(prefix: &Path, family: &str) -> Vec<PathBuf> {
    let mut roots = Vec::new();
    match family {
        "ubisoft" => {
            roots.push(prefix.join("drive_c").join("Program Files (x86)").join("Ubisoft").join("Ubisoft Game Launcher"))
        },
        "ea" => {
            roots.push(prefix.join("drive_c").join("Program Files").join("Electronic Arts").join("EA Desktop"));
            roots.push(prefix.join("drive_c").join("Program Files").join("Electronic Arts"));
        },
        _ => roots.push(prefix.join("drive_c").join("Program Files")),
    }

    let mut out = Vec::new();
    for root in roots {
        if !root.exists() {
            continue;
        }
        for entry in WalkDir::new(root).max_depth(5).into_iter().filter_map(Result::ok) {
            if !entry.file_type().is_file() {
                continue;
            }
            let path = entry.path();
            let name = path.file_name().and_then(|v| v.to_str()).unwrap_or("").to_ascii_lowercase();
            let wanted = match family {
                "ubisoft" => name == "ubisoftconnect.exe" || name == "ubisoftgamelauncher.exe",
                "ea" => name == "eadesktop.exe" || name == "ealauncher.exe" || name == "eabackgroundservice.exe",
                _ => name.ends_with(".exe"),
            };
            if wanted {
                out.push(path.to_path_buf());
            }
        }
    }
    out
}

fn summarize_launcher(
    manifest: &BottleManifest,
    family: &str,
    artifacts: &[Value],
    detections: &[Value],
) -> LauncherFacts {
    let mut facts = LauncherFacts { family: family.to_string(), ..LauncherFacts::default() };
    facts.installed_exe =
        detections.first().and_then(|d| d.get("exePath")).and_then(|v| v.as_str()).map(str::to_string);

    for artifact in artifacts {
        let path = artifact.get("path").and_then(|v| v.as_str()).unwrap_or("").to_ascii_lowercase();
        let full_text = artifact_full_text(artifact);
        if path.contains("launch-") {
            if full_text.to_ascii_lowercase().contains("installer.exe") {
                facts.installer_launch_attempted = true;
            }
            if family == "ubisoft"
                && (full_text.to_ascii_lowercase().contains("exe=ubisoftconnect.exe")
                    || full_text.to_ascii_lowercase().contains("exe=ubisoftgamelauncher.exe"))
            {
                facts.direct_launcher_launch_attempted = true;
            }
            if family == "ea"
                && (full_text.to_ascii_lowercase().contains("exe=eadesktop.exe")
                    || full_text.to_ascii_lowercase().contains("exe=ealauncher.exe"))
            {
                facts.direct_launcher_launch_attempted = true;
            }
        }
        for line in full_text.lines() {
            parse_launcher_line(family, line, &mut facts);
        }
        if path.contains(".msi.log") && artifact.get("bytes").and_then(|v| v.as_u64()) == Some(0) {
            facts.ea_msi_log_empty = true;
        }
    }

    if family == "ubisoft" && facts.ubisoft_launcher_started && !facts.direct_launcher_launch_attempted {
        facts.installer_auto_launch_evidence = true;
    }
    if manifest.last_launch_log.as_deref().unwrap_or("").to_ascii_lowercase().contains("launch-") {
        facts.installer_launch_attempted |= manifest
            .source_installer_path
            .as_deref()
            .map(|source| source.to_ascii_lowercase().contains("installer"))
            .unwrap_or(false);
    }
    facts
}

fn parse_launcher_line(family: &str, line: &str, facts: &mut LauncherFacts) {
    let lower = line.to_ascii_lowercase();
    match family {
        "ea" => {
            if lower.contains("inst-14-1603") {
                facts.ea_inst_14_1603 = true;
            }
            if lower.contains("0x80070643") {
                facts.ea_hresult = Some("0x80070643".to_string());
            }
            if let Some(pkg) = line.split("Applying execute package: ").nth(1).and_then(|s| s.split(',').next()) {
                facts.ea_msi_package = Some(pkg.trim().to_string());
            }
        },
        "ubisoft" => {
            if line.contains("-- Starting Ubisoft Game Launcher --") {
                facts.ubisoft_launcher_started = true;
            }
            if let Some(version) = line.split("Exe version:").nth(1) {
                facts.ubisoft_version = Some(version.trim().trim_end_matches('.').to_string());
            }
            if lower.contains("crash") || lower.contains("dbghelp module loaded") {
                facts.crash_reporter_evidence = true;
            }
        },
        _ => {},
    }
}

fn launcher_status(facts: &LauncherFacts) -> String {
    match facts.family.as_str() {
        "ea" if facts.ea_inst_14_1603 || facts.ea_hresult.as_deref() == Some("0x80070643") => "ea_msi_1603".to_string(),
        "ea" if facts.installed_exe.is_some() && !facts.direct_launcher_launch_attempted => {
            "installed_not_launched_directly".to_string()
        },
        "ubisoft" if facts.ubisoft_launcher_started && facts.crash_reporter_evidence => {
            "ubisoft_auto_started_then_crash_reporter".to_string()
        },
        "ubisoft" if facts.installed_exe.is_some() && !facts.direct_launcher_launch_attempted => {
            "ubisoft_installed_no_direct_launch_proof".to_string()
        },
        _ if facts.installed_exe.is_some() => "installed_launcher_detected".to_string(),
        _ => "installer_evidence_incomplete".to_string(),
    }
}

fn launcher_summary(status: &str, facts: &LauncherFacts) -> String {
    match status {
        "ea_msi_1603" => format!(
            "EA installer reaches MSI apply and fails with {} / INST-14-1603{}.",
            facts.ea_hresult.as_deref().unwrap_or("0x80070643"),
            facts.ea_msi_package.as_ref().map(|p| format!(" in {}", p)).unwrap_or_default()
        ),
        "ubisoft_auto_started_then_crash_reporter" => format!(
            "Ubisoft Connect installed and auto-started the launcher{} from the installer, then entered the crash-reporter path; no direct post-install launcher relaunch is recorded.",
            facts.ubisoft_version.as_ref().map(|v| format!(" {}", v)).unwrap_or_default()
        ),
        "ubisoft_installed_no_direct_launch_proof" => {
            "Ubisoft Connect files are installed and a launcher executable is detected, but no direct post-install launcher launch is recorded.".to_string()
        },
        "installed_not_launched_directly" => {
            "Launcher files are installed, but no direct post-install launcher launch is recorded.".to_string()
        },
        "installed_launcher_detected" => "Launcher files are installed and detected.".to_string(),
        _ => "Installer evidence is incomplete; run or relaunch the installer and refresh this report.".to_string(),
    }
}

fn launcher_next_actions(status: &str, family: &str) -> Vec<&'static str> {
    match status {
        "ea_msi_1603" => vec![
            "Preserve the EA bootstrapper and MSI logs before rerunning.",
            "Probe Wine MSI custom-action/service/elevation behavior; the failure is below missing WebView/.NET runtime assets.",
            "Rerun with fresh bottle evidence only after adding a specific MSI diagnostic change.",
        ],
        "ubisoft_auto_started_then_crash_reporter" => vec![
            "Repair or verify corefonts and WebView2 in this bottle.",
            "Launch UbisoftConnect.exe directly from the installed app path and compare logs against the installer auto-launch trail.",
            "If direct launch still reaches crash reporter, classify the next blocker as WebView/CEF, service/elevation, or auth bootstrap.",
        ],
        "ubisoft_installed_no_direct_launch_proof" if family == "ubisoft" => vec![
            "Launch UbisoftConnect.exe directly from the detected app path.",
            "Refresh this report and inspect launcher_log.txt plus client_crash_reporter.txt.",
        ],
        _ => vec![
            "Relaunch the installed launcher executable directly, then refresh this evidence report.",
        ],
    }
}

fn artifact_json(id: &str, path: &Path) -> Value {
    let metadata = fs::metadata(path).ok();
    let modified_at = metadata
        .as_ref()
        .and_then(|m| m.modified().ok())
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map(|d| d.as_secs());
    let tail = read_recent_text_limited(path).map(|text| tail_lines(&text, TAIL_LINES)).unwrap_or_default();
    json!({
        "id": id,
        "path": path.to_string_lossy(),
        "exists": metadata.is_some(),
        "bytes": metadata.map(|m| m.len()),
        "modifiedAtEpoch": modified_at,
        "tail": tail,
    })
}

fn artifact_full_text(artifact: &Value) -> String {
    artifact
        .get("path")
        .and_then(|v| v.as_str())
        .and_then(|path| read_recent_text_limited(Path::new(path)))
        .unwrap_or_else(|| {
            artifact
                .get("tail")
                .and_then(|v| v.as_array())
                .map(|lines| lines.iter().filter_map(|v| v.as_str()).collect::<Vec<_>>().join("\n"))
                .unwrap_or_default()
        })
}

fn read_recent_text_limited(path: &Path) -> Option<String> {
    let mut file = File::open(path).ok()?;
    let len = file.metadata().ok()?.len();
    if len > MAX_ARTIFACT_READ_BYTES {
        file.seek(SeekFrom::Start(len - MAX_ARTIFACT_READ_BYTES)).ok()?;
    }
    let mut bytes = Vec::new();
    file.take(MAX_ARTIFACT_READ_BYTES).read_to_end(&mut bytes).ok()?;
    Some(String::from_utf8_lossy(&bytes).into_owned())
}

fn tail_lines(text: &str, max_lines: usize) -> Vec<String> {
    let lines: Vec<&str> = text.lines().collect();
    let start = lines.len().saturating_sub(max_lines);
    lines[start..].iter().map(|line| line.trim_end_matches('\r').to_string()).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn launcher_evidence_inventory_is_read_only_and_tracks_proof_targets() {
        let inventory = launcher_evidence_inventory();
        assert_eq!(inventory.get("schema").and_then(|value| value.as_str()), Some(INVENTORY_SCHEMA));
        assert_eq!(inventory.get("readOnly").and_then(|value| value.as_bool()), Some(true));
        let targets = inventory.get("targets").and_then(|value| value.as_array()).expect("targets");
        for family in ["minecraft", "ea", "ubisoft"] {
            assert!(
                targets.iter().any(|target| target.get("family").and_then(|value| value.as_str()) == Some(family)),
                "missing launcher proof target {family}"
            );
        }
    }

    #[test]
    fn parses_ea_inst_14_1603() {
        let mut facts = LauncherFacts { family: "ea".to_string(), ..LauncherFacts::default() };
        parse_launcher_line(
            "ea",
            "[0240:0244]i000: EAXBA INFO ecod[0x80070643 aka 'INST-14-1603'], estr[Installation failure.]",
            &mut facts,
        );
        assert!(facts.ea_inst_14_1603);
        assert_eq!(facts.ea_hresult.as_deref(), Some("0x80070643"));
        assert_eq!(launcher_status(&facts), "ea_msi_1603");
    }

    #[test]
    fn parses_ubisoft_auto_start_crash_reporter() {
        let mut facts = LauncherFacts { family: "ubisoft".to_string(), ..LauncherFacts::default() };
        parse_launcher_line("ubisoft", "-- Starting Ubisoft Game Launcher --", &mut facts);
        parse_launcher_line("ubisoft", "DbgHelp module loaded successfully", &mut facts);
        assert!(facts.ubisoft_launcher_started);
        assert!(facts.crash_reporter_evidence);
        assert_eq!(launcher_status(&facts), "ubisoft_auto_started_then_crash_reporter");
    }
}
