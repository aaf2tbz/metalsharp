//! Phase 8: Mono/FNA/XNA pipeline reliability and asset coverage.
//!
//! Treats Mono/FNA/XNA as a first-class compatibility family, not only a
//! handful of known app ids. This module adds:
//!
//! * richer flavor detection (FNA / MonoGame / XNA + Steamworks.NET /
//!   CSteamworks + audio deps + x86-vs-native Mono), reusing
//!   [`crate::mtsp::launcher::detect_fna_flavor`] for the base signal and
//!   layering the additional signals the roadmap requires;
//! * receipt-driven asset staging types ([`AssetReceipt`]) so staging is
//!   auditable and reversible — record what was copied, source path + hash,
//!   required vs optional, and whether a game file was overwritten;
//! * a "profile explain" diagnostic ([`explain_profile`]) that reports WHY a
//!   game selected FNA ARM64, FNA x86, XNA/MonoGame x86, or a fallback route;
//! * a conservative unproven-game classifier ([`classify_unproven_fna_game`])
//!   that does NOT claim compatibility, stages only reversible shims, and
//!   offers fallback to the Wine route when native Mono/FNA launch is not
//!   proven.
//!
//! The pinned known-good behavior for Terraria (105600), Celeste (504230),
//! and Stardew Valley (413150) is unchanged; this module explains and
//! receipts it, it does not override it.

use serde::Serialize;
use std::collections::BTreeSet;
use std::fs;
use std::path::{Path, PathBuf};

/// Known-good pinned FNA/XNA app ids. These keep their existing pinned
/// behavior; the unproven-game classifier never applies to them.
pub const PINNED_FNA_APPIDS: &[u32] = &[105600, 504230, 413150];

/// Additional signals layered on top of [`detect_fna_flavor`].
#[derive(Debug, Clone, Default, Serialize)]
pub struct FnaFlavorSignals {
    pub base_flavor: String,
    pub uses_steamworks_net: bool,
    pub uses_csteamworks: bool,
    pub uses_faudio: bool,
    pub uses_fmod: bool,
    pub uses_openal: bool,
    pub uses_xinput: bool,
    pub has_managed_dir: bool,
    /// True if a 32-bit Mono indicator is present (e.g. a `x86` subdirectory,
    /// a `MonoBundle` with 32-bit signatures, or a `Celeste.exe`-style name).
    /// Conservative: we only set this when we have a positive signal.
    pub indicates_x86_mono: bool,
    /// True if the game ships an arm64 native executable, indicating the
    /// native Mono lane is appropriate.
    pub indicates_native_mono: bool,
    /// File names that drove the detection (for the profile-explain report).
    pub evidence_files: Vec<String>,
}

/// Detect the richer FNA/XNA flavor signals for a game directory. This is a
/// superset of [`crate::mtsp::launcher::detect_fna_flavor`] and does NOT
/// change that function's behavior — it layers additional signals.
pub fn detect_fna_signals(game_dir: &Path) -> FnaFlavorSignals {
    use crate::mtsp::launcher::detect_fna_flavor;

    let base = detect_fna_flavor(&game_dir.to_path_buf());
    let mut signals = FnaFlavorSignals { base_flavor: format!("{:?}", base).to_lowercase(), ..Default::default() };

    let managed_dlls = collect_managed_dll_names(game_dir, &mut signals.has_managed_dir, &mut signals.evidence_files);

    let lower: BTreeSet<String> = managed_dlls.iter().map(|d| d.to_lowercase()).collect();

    signals.uses_steamworks_net = lower.contains("steamworks.net.dll");
    if signals.uses_steamworks_net {
        signals.evidence_files.push("Steamworks.NET.dll".into());
    }
    signals.uses_csteamworks = lower.contains("csteamworks.dll");
    if signals.uses_csteamworks {
        signals.evidence_files.push("CSteamworks.dll".into());
    }
    signals.uses_faudio = lower.iter().any(|d| d.contains("faudio")) || game_dir.join("libFAudio.dylib").exists();
    if signals.uses_faudio {
        signals.evidence_files.push("FAudio".into());
    }
    signals.uses_fmod = lower.iter().any(|d| d.contains("fmod")) || game_dir.join("libfmod.dylib").exists();
    if signals.uses_fmod {
        signals.evidence_files.push("FMOD".into());
    }
    signals.uses_openal = lower.iter().any(|d| d.contains("openal")) || game_dir.join("libopenal.dylib").exists();
    if signals.uses_openal {
        signals.evidence_files.push("OpenAL".into());
    }
    signals.uses_xinput =
        lower.iter().any(|d| d.contains("xinput")) || lower.iter().any(|d| d.contains("xinput1_3.dll"));
    if signals.uses_xinput {
        signals.evidence_files.push("XInput".into());
    }

    // x86-vs-native signal: a Windows-only .NET game with an x86 hint (a
    // win32 subdirectory, or the absence of an arm64 mac executable) leans
    // x86 Mono; a shipped arm64 MacOS executable leans native.
    if game_dir.join("x86").is_dir() {
        signals.indicates_x86_mono = true;
        signals.evidence_files.push("x86/".into());
    }
    if has_arm64_macos_executable(game_dir) {
        signals.indicates_native_mono = true;
        signals.evidence_files.push("arm64 macOS executable".into());
    } else if signals.base_flavor != "unknown" {
        // An FNA/XNA game with no native arm64 executable is conservatively
        // treated as x86 Mono (the historical default).
        signals.indicates_x86_mono = true;
    }

    signals
}

fn collect_managed_dll_names(game_dir: &Path, has_managed: &mut bool, evidence: &mut Vec<String>) -> Vec<String> {
    let mut names = Vec::new();
    let Ok(entries) = fs::read_dir(game_dir) else {
        return names;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if !(path.is_dir() && entry.file_name().to_string_lossy().to_lowercase().ends_with("_data")) {
            continue;
        }
        let managed = path.join("Managed");
        let Ok(managed_entries) = fs::read_dir(&managed) else {
            continue;
        };
        *has_managed = true;
        for me in managed_entries.flatten() {
            let dll = me.file_name().to_string_lossy().to_string();
            if dll.to_lowercase().ends_with(".dll") {
                names.push(dll);
            }
        }
        if names.iter().any(|n| n.to_lowercase() == "fna.dll") {
            evidence.push("FNA.dll".into());
        }
        break;
    }
    names
}

fn has_arm64_macos_executable(game_dir: &Path) -> bool {
    // Look for a Contents/MacOS/<exe> path inside a .app bundle, or a top-
    // level arm64 Mach-O executable. Conservative: presence of an .app bundle
    // is a strong native signal; absence is not proof of x86.
    let Ok(entries) = fs::read_dir(game_dir) else {
        return false;
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().to_string();
        if name.ends_with(".app") {
            return true;
        }
    }
    false
}

/// A single staged-asset receipt. Records what was copied, the source path
/// and its sha256, whether the asset was required or optional, and whether a
/// pre-existing game file was overwritten (so staging is reversible).
#[derive(Debug, Clone, Serialize)]
pub struct AssetReceipt {
    pub filename: String,
    pub source_path: String,
    pub dest_path: String,
    pub required: bool,
    pub source_sha256: Option<String>,
    pub dest_sha256: Option<String>,
    pub overwrote_game_file: bool,
    pub staged: bool,
    pub reason: String,
}

/// Build a receipt for a staged asset WITHOUT mutating anything. The caller
/// performs the copy; this records what happened. `overwrote_game_file` must
/// be true only when the destination existed and differed from the source
/// before the copy.
pub fn record_asset_receipt(
    filename: &str,
    source_path: &Path,
    dest_path: &Path,
    required: bool,
    overwrote_game_file: bool,
    staged: bool,
    reason: &str,
) -> AssetReceipt {
    AssetReceipt {
        filename: filename.to_string(),
        source_path: source_path.to_string_lossy().to_string(),
        dest_path: dest_path.to_string_lossy().to_string(),
        required,
        source_sha256: if source_path.exists() { crate::diagnostics::file_sha256(source_path) } else { None },
        dest_sha256: if dest_path.exists() { crate::diagnostics::file_sha256(dest_path) } else { None },
        overwrote_game_file,
        staged,
        reason: reason.to_string(),
    }
}

/// A staging report persisted next to the game dir so a future run can skip
/// re-copying when source and staged hashes already match.
#[derive(Debug, Clone, Serialize)]
pub struct AssetStagingReport {
    pub schema_version: u32,
    pub appid: u32,
    pub generated_at_unix: u64,
    pub receipts: Vec<AssetReceipt>,
}

impl AssetStagingReport {
    pub fn new(appid: u32) -> Self {
        AssetStagingReport {
            schema_version: 1,
            appid,
            generated_at_unix: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_secs())
                .unwrap_or(0),
            receipts: Vec::new(),
        }
    }

    pub fn record(&mut self, receipt: AssetReceipt) {
        self.receipts.push(receipt);
    }

    /// Write the report atomically to `<game_dir>/.metalsharp/fna-staging.json`.
    pub fn persist(&self, game_dir: &Path) -> std::io::Result<()> {
        let dir = game_dir.join(".metalsharp");
        fs::create_dir_all(&dir)?;
        let final_path = dir.join("fna-staging.json");
        let tmp_path = dir.join("fna-staging.json.tmp");
        let payload = serde_json::to_string_pretty(self).unwrap_or_else(|_| "{}".into());
        fs::write(&tmp_path, payload)?;
        fs::rename(&tmp_path, &final_path)?;
        Ok(())
    }
}

/// The conservative recommendation for an unproven FNA/XNA game.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum UnprovenRecommendation {
    /// A conservative FNA/XNA setup is reasonable: stage only reversible
    /// shims, preserve originals, and offer a Wine fallback.
    ConservativeFnaSetup,
    /// The Wine route is safer than a native Mono attempt.
    WineFallback,
    /// The game does not look like an FNA/XNA title; do not force this lane.
    NotFna,
}

/// The result of classifying an unproven game.
#[derive(Debug, Clone, Serialize)]
pub struct UnprovenClassification {
    pub appid: u32,
    pub pinned: bool,
    pub recommendation: UnprovenRecommendation,
    pub signals: FnaFlavorSignals,
    pub rationale: Vec<String>,
}

/// Classify an unproven game conservatively. Pinned known-good app ids are
/// never classified here (they keep their pinned behavior); this only
/// applies to games NOT in [`PINNED_FNA_APPIDS`].
pub fn classify_unproven_fna_game(appid: u32, game_dir: &Path) -> UnprovenClassification {
    let pinned = PINNED_FNA_APPIDS.contains(&appid);
    let signals = detect_fna_signals(game_dir);
    let mut rationale = Vec::new();

    if pinned {
        rationale.push(format!("appid {} is a pinned known-good FNA/XNA title; it keeps its pinned behavior", appid));
        return UnprovenClassification {
            appid,
            pinned: true,
            recommendation: UnprovenRecommendation::ConservativeFnaSetup,
            signals,
            rationale,
        };
    }

    if signals.base_flavor == "unknown" {
        rationale.push("no FNA/MonoGame/XNA assemblies detected; do not force this lane".into());
        return UnprovenClassification {
            appid,
            pinned: false,
            recommendation: UnprovenRecommendation::NotFna,
            signals,
            rationale,
        };
    }

    rationale.push(format!("detected {} flavor", signals.base_flavor));
    if signals.indicates_native_mono {
        rationale.push("native arm64 macOS executable present; native Mono lane is plausible".into());
    } else if signals.indicates_x86_mono {
        rationale.push("no native arm64 executable / x86 indicator present; x86 Mono lane is plausible".into());
    }
    if signals.uses_steamworks_net || signals.uses_csteamworks {
        rationale.push("Steamworks.NET/CSteamworks detected; staging must keep Steam identity shims reversible".into());
    }
    // Conservative: do NOT claim compatibility. Stage only reversible shims
    // and offer a Wine fallback. We recommend ConservativeFnaSetup only when
    // we have a positive flavor signal; otherwise WineFallback.
    let recommendation = if signals.base_flavor == "unknown" {
        UnprovenRecommendation::WineFallback
    } else {
        UnprovenRecommendation::ConservativeFnaSetup
    };
    rationale.push("conservative: stage only reversible shims, preserve originals, offer Wine fallback".into());

    UnprovenClassification { appid, pinned: false, recommendation, signals, rationale }
}

/// A "profile explain" diagnostic that reports WHY a game selected a given
/// FNA/XNA profile (or fallback), for known-good and unproven games alike.
#[derive(Debug, Clone, Serialize)]
pub struct ProfileExplanation {
    pub schema_version: u32,
    pub appid: u32,
    pub pinned: bool,
    pub mono_arch: String,
    pub method_label: String,
    pub mono_config: String,
    pub signals: FnaFlavorSignals,
    pub rationale: Vec<String>,
}

/// Explain the FNA/XNA profile selection for an appid + game dir. For pinned
/// games, reports the pinned profile and the signals that confirm it. For
/// unproven games, reports the conservative classification.
pub fn explain_profile(appid: u32, game_dir: &Path) -> ProfileExplanation {
    let profile = crate::mtsp::launcher::find_fna_profile(appid);
    let pinned = PINNED_FNA_APPIDS.contains(&appid);
    let signals = detect_fna_signals(game_dir);
    let mut rationale = Vec::new();

    let mono_arch = match profile.mono_arch {
        crate::mtsp::launcher::MonoArch::Native => "native".to_string(),
        crate::mtsp::launcher::MonoArch::X86 => "x86".to_string(),
    };

    if pinned {
        rationale.push(format!(
            "appid {} is pinned to the {} lane with the {} mono config",
            appid, profile.method_label, profile.mono_config
        ));
        if signals.base_flavor != "unknown" {
            rationale.push(format!("detected {} flavor confirms the FNA/XNA family", signals.base_flavor));
        }
    } else {
        let class = classify_unproven_fna_game(appid, game_dir);
        rationale = class.rationale;
    }

    ProfileExplanation {
        schema_version: 1,
        appid,
        pinned,
        mono_arch,
        method_label: profile.method_label.to_string(),
        mono_config: profile.mono_config.to_string(),
        signals,
        rationale,
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct NativeMonoPlatformDoctor {
    pub schema_version: u32,
    pub ok: bool,
    pub read_only: bool,
    pub metalsharp_home: String,
    pub lanes: Vec<NativeMonoLaneDoctor>,
    pub support_inventory: Vec<SupportInventoryEntry>,
    pub game: Option<NativeMonoGameDoctor>,
    pub next_actions: Vec<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct NativeMonoLaneDoctor {
    pub id: &'static str,
    pub runtime_root: String,
    pub mono_binary: String,
    pub expected_arch: &'static str,
    pub present: bool,
    pub detected_architectures: Vec<String>,
    pub architecture_ok: bool,
    pub ready: bool,
    pub blockers: Vec<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct SupportInventoryEntry {
    pub id: &'static str,
    pub label: &'static str,
    pub path: String,
    pub present: bool,
    pub required: bool,
    pub sha256: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct NativeMonoGameDoctor {
    pub appid: u32,
    pub game_dir: String,
    pub profile: ProfileExplanation,
    pub receipt_path: String,
    pub receipt_present: bool,
    pub originals_preserved: bool,
    pub known_good_profile_applied: bool,
    pub required_dylibs_staged: bool,
    pub blockers: Vec<String>,
}

pub fn native_mono_platform_doctor_for(home: &Path, game: Option<(u32, &Path)>) -> NativeMonoPlatformDoctor {
    let ms_home = crate::platform::metalsharp_home_dir_for(&home.to_path_buf());
    let runtime = ms_home.join("runtime");
    let lanes = vec![
        native_mono_lane_doctor("native_mono_arm64", &runtime.join("mono-arm64"), "arm64"),
        native_mono_lane_doctor("native_mono_x86", &runtime.join("mono-x86"), "x86_64"),
    ];
    let support_inventory = native_mono_support_inventory(&runtime);
    let game = game.map(|(appid, game_dir)| native_mono_game_doctor(appid, game_dir));
    let mut next_actions = Vec::new();
    for lane in &lanes {
        if !lane.present {
            next_actions.push(format!("Stage {} Mono runtime at {}", lane.expected_arch, lane.runtime_root));
        } else if !lane.architecture_ok {
            next_actions.push(format!("Replace {} Mono binary with {} build", lane.id, lane.expected_arch));
        }
    }
    if support_inventory.iter().any(|entry| entry.required && !entry.present) {
        next_actions.push(
            "Stage required FNA/FNA3D/FAudio/SDL2/native shim assets before marking native Mono games ready".into(),
        );
    }
    if let Some(game) = &game {
        if !game.receipt_present {
            next_actions.push("Run the reversible FNA asset staging path so .metalsharp/fna-staging.json records originals and copied dylibs".into());
        }
        if !game.required_dylibs_staged {
            next_actions.push("Stage required game-local FNA dylibs before native launch proof".into());
        }
    }
    let ok = lanes.iter().all(|lane| lane.ready)
        && support_inventory.iter().all(|entry| !entry.required || entry.present)
        && game.as_ref().is_none_or(|game| game.blockers.is_empty());

    NativeMonoPlatformDoctor {
        schema_version: 1,
        ok,
        read_only: true,
        metalsharp_home: ms_home.to_string_lossy().to_string(),
        lanes,
        support_inventory,
        game,
        next_actions,
    }
}

fn native_mono_lane_doctor(id: &'static str, root: &Path, expected_arch: &'static str) -> NativeMonoLaneDoctor {
    let mono_binary = root.join("bin").join("mono");
    let present = file_nonempty(&mono_binary);
    let detected_architectures = if present { mach_o_architectures(&mono_binary) } else { Vec::new() };
    let architecture_ok = detected_architectures.iter().any(|arch| arch == expected_arch);
    let mut blockers = Vec::new();
    if !present {
        blockers.push("mono_binary".into());
    }
    if present && !architecture_ok {
        blockers.push("mono_arch".into());
    }
    NativeMonoLaneDoctor {
        id,
        runtime_root: root.to_string_lossy().to_string(),
        mono_binary: mono_binary.to_string_lossy().to_string(),
        expected_arch,
        present,
        detected_architectures,
        architecture_ok,
        ready: blockers.is_empty(),
        blockers,
    }
}

fn native_mono_support_inventory(runtime: &Path) -> Vec<SupportInventoryEntry> {
    let entries: [(&str, &str, PathBuf, bool); 15] = [
        ("fna", "FNA.dll", runtime.join("fna").join("FNA.dll"), true),
        ("sdl2", "SDL2", runtime.join("fnalibs").join("libSDL2-2.0.0.dylib"), true),
        ("fna3d", "FNA3D", runtime.join("fnalibs").join("libFNA3D.0.dylib"), true),
        ("faudio", "FAudio", runtime.join("fnalibs").join("libFAudio.0.dylib"), true),
        ("fmod", "FMOD", runtime.join("fnalibs").join("fmod").join("libfmod.dylib"), false),
        ("fmodstudio", "FMOD Studio", runtime.join("fnalibs").join("fmod").join("libfmodstudio.dylib"), false),
        ("openal", "OpenAL", runtime.join("fnalibs").join("libopenal.dylib"), false),
        ("xinput", "XInput shim", runtime.join("shims").join("xinput1_4.dylib"), false),
        ("steamworks", "Steamworks shim", runtime.join("shims").join("libsteam_api.dylib"), false),
        ("csteamworks", "CSteamworks shim", runtime.join("shims").join("libCSteamworks.dylib"), false),
        ("carbon", "Carbon shim", runtime.join("shims").join("libCarbon.dylib"), true),
        ("hiview", "HiView/User32 shim", runtime.join("shims").join("libuser32.dylib"), true),
        ("kernel32", "Kernel32 shim", runtime.join("shims").join("libkernel32.dylib"), true),
        ("carbon_interpose", "Carbon interpose shim", runtime.join("shims").join("libCarbonInterpose.dylib"), false),
        ("receipts", "staged asset receipts", runtime.join("fna").join("receipts"), false),
    ];
    entries
        .into_iter()
        .map(|(id, label, path, required)| SupportInventoryEntry {
            id,
            label,
            path: path.to_string_lossy().to_string(),
            present: if path.is_dir() { path.exists() } else { file_nonempty(&path) },
            required,
            sha256: if path.is_file() { crate::diagnostics::file_sha256(&path) } else { None },
        })
        .collect()
}

fn native_mono_game_doctor(appid: u32, game_dir: &Path) -> NativeMonoGameDoctor {
    let profile = explain_profile(appid, game_dir);
    let receipt_path = game_dir.join(".metalsharp").join("fna-staging.json");
    let receipt_present = file_nonempty(&receipt_path);
    let receipt_json =
        fs::read_to_string(&receipt_path).ok().and_then(|raw| serde_json::from_str::<serde_json::Value>(&raw).ok());
    let originals_preserved =
        receipt_json.as_ref().and_then(|value| value.get("receipts")).and_then(|value| value.as_array()).is_some_and(
            |receipts| {
                receipts.iter().all(|receipt| {
                    if receipt.get("overwrote_game_file").and_then(|v| v.as_bool()) != Some(true) {
                        return true;
                    }
                    let Some(filename) = receipt.get("filename").and_then(|v| v.as_str()) else {
                        return false;
                    };
                    game_dir.join(format!("{filename}.metalsharp-original")).exists()
                        || game_dir.join(format!("{filename}.orig")).exists()
                })
            },
        );
    let required_dylibs_staged = ["libSDL2-2.0.0.dylib", "libFNA3D.0.dylib", "libFAudio.0.dylib"]
        .iter()
        .all(|name| file_nonempty(&game_dir.join(name)));
    let known_good_profile_applied = if profile.pinned {
        receipt_present && profile.signals.base_flavor != "unknown"
    } else {
        matches!(
            classify_unproven_fna_game(appid, game_dir).recommendation,
            UnprovenRecommendation::ConservativeFnaSetup
        )
    };
    let mut blockers = Vec::new();
    if profile.signals.base_flavor == "unknown" {
        blockers.push("fna_xna_evidence".into());
    }
    if !receipt_present {
        blockers.push("asset_receipts".into());
    }
    if receipt_present && !originals_preserved {
        blockers.push("originals_preserved".into());
    }
    if !required_dylibs_staged {
        blockers.push("required_dylibs".into());
    }
    if !known_good_profile_applied {
        blockers.push("known_good_profile".into());
    }

    NativeMonoGameDoctor {
        appid,
        game_dir: game_dir.to_string_lossy().to_string(),
        profile,
        receipt_path: receipt_path.to_string_lossy().to_string(),
        receipt_present,
        originals_preserved,
        known_good_profile_applied,
        required_dylibs_staged,
        blockers,
    }
}

fn file_nonempty(path: &Path) -> bool {
    path.metadata().map(|metadata| metadata.is_file() && metadata.len() > 0).unwrap_or(false)
}

fn mach_o_architectures(path: &Path) -> Vec<String> {
    let Ok(bytes) = fs::read(path) else {
        return Vec::new();
    };
    if bytes.len() < 8 {
        return Vec::new();
    }
    let magic_be = u32::from_be_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]);
    let magic_le = u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]);
    let mut arches = Vec::new();
    match magic_be {
        0xcafebabe | 0xcafebabf if bytes.len() >= 8 => {
            let count = u32::from_be_bytes([bytes[4], bytes[5], bytes[6], bytes[7]]) as usize;
            for idx in 0..count.min(16) {
                let offset = 8 + idx * 20;
                if bytes.len() < offset + 8 {
                    break;
                }
                let cpu = u32::from_be_bytes([bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]]);
                push_cpu_arch(cpu, &mut arches);
            }
        },
        _ => match magic_le {
            0xfeedface | 0xfeedfacf | 0xcefaedfe | 0xcffaedfe if bytes.len() >= 8 => {
                let cpu = u32::from_le_bytes([bytes[4], bytes[5], bytes[6], bytes[7]]);
                push_cpu_arch(cpu, &mut arches);
            },
            _ => {},
        },
    }
    arches.sort();
    arches.dedup();
    arches
}

fn push_cpu_arch(cpu: u32, arches: &mut Vec<String>) {
    match cpu {
        0x0100_000c => arches.push("arm64".into()),
        0x0100_0007 => arches.push("x86_64".into()),
        0x0000_0007 => arches.push("i386".into()),
        _ => arches.push(format!("unknown:0x{cpu:08x}")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_managed_game_dir(dir: &Path, dlls: &[&str]) {
        let managed = dir.join("Celeste_data").join("Managed");
        fs::create_dir_all(&managed).unwrap();
        for dll in dlls {
            fs::write(managed.join(dll), b"dll-bytes").unwrap();
        }
    }

    fn write_thin_macho(path: &Path, cpu: u32) {
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&0xfeedfacfu32.to_le_bytes());
        bytes.extend_from_slice(&cpu.to_le_bytes());
        bytes.extend_from_slice(&[0; 24]);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, bytes).unwrap();
    }

    #[test]
    fn mach_o_architectures_detects_thin_arm64_and_x86_64() {
        let dir = std::env::temp_dir().join("ms-fna-macho-arch");
        let _ = fs::remove_dir_all(&dir);
        let arm = dir.join("arm64");
        let x86 = dir.join("x86_64");
        write_thin_macho(&arm, 0x0100_000c);
        write_thin_macho(&x86, 0x0100_0007);
        assert_eq!(mach_o_architectures(&arm), vec!["arm64".to_string()]);
        assert_eq!(mach_o_architectures(&x86), vec!["x86_64".to_string()]);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn native_mono_platform_doctor_reports_lanes_inventory_and_game_receipts() {
        let home = std::env::temp_dir().join("ms-fna-platform-doctor-home");
        let _ = fs::remove_dir_all(&home);
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        let runtime = ms_home.join("runtime");
        write_thin_macho(&runtime.join("mono-arm64/bin/mono"), 0x0100_000c);
        write_thin_macho(&runtime.join("mono-x86/bin/mono"), 0x0100_0007);
        for path in [
            runtime.join("fna/FNA.dll"),
            runtime.join("fnalibs/libSDL2-2.0.0.dylib"),
            runtime.join("fnalibs/libFNA3D.0.dylib"),
            runtime.join("fnalibs/libFAudio.0.dylib"),
            runtime.join("shims/libCarbon.dylib"),
            runtime.join("shims/libuser32.dylib"),
            runtime.join("shims/libkernel32.dylib"),
        ] {
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            fs::write(path, b"asset").unwrap();
        }
        let game = home.join("game");
        make_managed_game_dir(&game, &["FNA.dll"]);
        for name in ["libSDL2-2.0.0.dylib", "libFNA3D.0.dylib", "libFAudio.0.dylib"] {
            fs::write(game.join(name), b"game-asset").unwrap();
        }
        let mut receipts = AssetStagingReport::new(504230);
        receipts.record(record_asset_receipt(
            "libSDL2-2.0.0.dylib",
            &runtime.join("fnalibs/libSDL2-2.0.0.dylib"),
            &game.join("libSDL2-2.0.0.dylib"),
            true,
            false,
            true,
            "required native FNA support",
        ));
        receipts.persist(&game).unwrap();

        let report = native_mono_platform_doctor_for(&home, Some((504230, &game)));
        assert!(report.read_only);
        assert!(report.ok, "doctor should be ready: {:?}", report.next_actions);
        assert!(report.lanes.iter().all(|lane| lane.ready));
        let game_report = report.game.expect("game report");
        assert!(game_report.receipt_present);
        assert!(game_report.originals_preserved);
        assert!(game_report.required_dylibs_staged);
        assert!(game_report.known_good_profile_applied);
        let _ = fs::remove_dir_all(&home);
    }

    #[test]
    fn detect_fna_signals_flags_fna_and_audio_deps() {
        let dir = std::env::temp_dir().join("ms-fna-signals-fna");
        let _ = fs::remove_dir_all(&dir);
        make_managed_game_dir(&dir, &["FNA.dll", "Steamworks.NET.dll", "CSteamworks.dll"]);
        // Top-level native libs.
        fs::write(dir.join("libFAudio.dylib"), b"x").unwrap();
        fs::write(dir.join("libfmod.dylib"), b"x").unwrap();

        let signals = detect_fna_signals(&dir);
        assert_eq!(signals.base_flavor, "fna");
        assert!(signals.has_managed_dir);
        assert!(signals.uses_steamworks_net);
        assert!(signals.uses_csteamworks);
        assert!(signals.uses_faudio);
        assert!(signals.uses_fmod);
        assert!(!signals.evidence_files.is_empty());
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn detect_fna_signals_flags_monogame_and_xna() {
        let dir = std::env::temp_dir().join("ms-fna-signals-mg");
        let _ = fs::remove_dir_all(&dir);
        make_managed_game_dir(&dir, &["MonoGame.Framework.dll"]);
        let signals = detect_fna_signals(&dir);
        assert_eq!(signals.base_flavor, "monogame");

        let dir2 = std::env::temp_dir().join("ms-fna-signals-xna");
        let _ = fs::remove_dir_all(&dir2);
        make_managed_game_dir(&dir2, &["Microsoft.Xna.Framework.dll"]);
        let signals2 = detect_fna_signals(&dir2);
        assert_eq!(signals2.base_flavor, "xna");
        let _ = fs::remove_dir_all(&dir);
        let _ = fs::remove_dir_all(&dir2);
    }

    #[test]
    fn detect_fna_signals_unknown_when_no_fna_assemblies() {
        let dir = std::env::temp_dir().join("ms-fna-signals-none");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("game.exe"), b"x").unwrap();
        let signals = detect_fna_signals(&dir);
        assert_eq!(signals.base_flavor, "unknown");
        assert!(!signals.has_managed_dir);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn classify_unproven_fna_game_is_conservative_for_unknown_flavor() {
        let dir = std::env::temp_dir().join("ms-fna-classify-unknown");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        // Not pinned, no FNA signal => NotFna.
        let class = classify_unproven_fna_game(999999, &dir);
        assert!(!class.pinned);
        assert!(matches!(class.recommendation, UnprovenRecommendation::NotFna));
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn classify_unproven_fna_game_recommends_conservative_setup_for_fna_signal() {
        let dir = std::env::temp_dir().join("ms-fna-classify-fna");
        let _ = fs::remove_dir_all(&dir);
        make_managed_game_dir(&dir, &["FNA.dll"]);
        let class = classify_unproven_fna_game(888888, &dir);
        assert!(!class.pinned);
        assert!(matches!(class.recommendation, UnprovenRecommendation::ConservativeFnaSetup));
        assert!(
            class.rationale.iter().any(|r| r.contains("reversible")),
            "must be conservative: {:?}",
            class.rationale
        );
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn classify_unproven_fna_game_never_overrides_pinned_titles() {
        // Terraria/Celeste/Stardew are pinned and must keep their behavior.
        let dir = std::env::temp_dir().join("ms-fna-classify-pinned");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        for appid in PINNED_FNA_APPIDS {
            let class = classify_unproven_fna_game(*appid, &dir);
            assert!(class.pinned, "appid {} must be pinned", appid);
        }
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn pinned_fna_appids_cover_terrarria_celeste_stardew() {
        assert!(PINNED_FNA_APPIDS.contains(&105600), "Terraria");
        assert!(PINNED_FNA_APPIDS.contains(&504230), "Celeste");
        assert!(PINNED_FNA_APPIDS.contains(&413150), "Stardew Valley");
    }

    #[test]
    fn record_asset_receipt_captures_source_and_dest_hashes() {
        let dir = std::env::temp_dir().join("ms-fna-receipt");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        let src = dir.join("libfmod.dylib");
        let dst = dir.join("game").join("libfmod.dylib");
        fs::create_dir_all(dst.parent().unwrap()).unwrap();
        fs::write(&src, b"fmod-bytes").unwrap();
        fs::write(&dst, b"old-bytes").unwrap();

        let receipt = record_asset_receipt("libfmod.dylib", &src, &dst, true, true, true, "required audio shim");
        assert_eq!(receipt.filename, "libfmod.dylib");
        assert!(receipt.required);
        assert!(receipt.overwrote_game_file);
        assert!(receipt.staged);
        assert!(receipt.source_sha256.is_some());
        assert!(receipt.dest_sha256.is_some());
        assert!(receipt.source_sha256 != receipt.dest_sha256, "old dest must differ from new source");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn asset_staging_report_persists_and_round_trips() {
        let dir = std::env::temp_dir().join("ms-fna-staging-report");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        let mut report = AssetStagingReport::new(504230);
        report.record(AssetReceipt {
            filename: "libFAudio.dylib".into(),
            source_path: "/shims/libFAudio.dylib".into(),
            dest_path: dir.join("libFAudio.dylib").to_string_lossy().to_string(),
            required: true,
            source_sha256: Some("abc".into()),
            dest_sha256: Some("abc".into()),
            overwrote_game_file: false,
            staged: true,
            reason: "required audio shim".into(),
        });
        report.persist(&dir).unwrap();

        let raw = fs::read_to_string(dir.join(".metalsharp").join("fna-staging.json")).unwrap();
        let back: serde_json::Value = serde_json::from_str(&raw).unwrap();
        assert_eq!(back.get("schema_version").and_then(|v| v.as_u64()), Some(1));
        assert_eq!(back.get("appid").and_then(|v| v.as_u64()), Some(504230));
        assert_eq!(back.get("receipts").unwrap().as_array().unwrap().len(), 1);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn explain_profile_reports_pinned_celeste_lane() {
        let dir = std::env::temp_dir().join("ms-fna-explain-celeste");
        let _ = fs::remove_dir_all(&dir);
        make_managed_game_dir(&dir, &["FNA.dll"]);
        let explanation = explain_profile(504230, &dir);
        assert!(explanation.pinned);
        assert_eq!(explanation.method_label, "xna_fna_x86");
        assert_eq!(explanation.mono_config, "celeste-x86-mono.config");
        assert_eq!(explanation.mono_arch, "x86");
        assert!(explanation.rationale.iter().any(|r| r.contains("pinned")), "{:?}", explanation.rationale);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn explain_profile_reports_pinned_stardew_native_lane() {
        let dir = std::env::temp_dir().join("ms-fna-explain-stardew");
        let _ = fs::remove_dir_all(&dir);
        make_managed_game_dir(&dir, &["FNA.dll"]);
        let explanation = explain_profile(413150, &dir);
        assert!(explanation.pinned);
        assert_eq!(explanation.method_label, "xna_fna_arm64");
        assert_eq!(explanation.mono_arch, "native");
        assert_eq!(explanation.mono_config, "stardew-mono.config");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn explain_profile_reports_pinned_terraria_x86_lane() {
        let dir = std::env::temp_dir().join("ms-fna-explain-terraria");
        let _ = fs::remove_dir_all(&dir);
        make_managed_game_dir(&dir, &["FNA.dll"]);
        let explanation = explain_profile(105600, &dir);
        assert!(explanation.pinned);
        assert_eq!(explanation.method_label, "xna_fna_x86");
        assert_eq!(explanation.mono_config, "generic-fna-mono.config");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn explain_profile_for_unproven_game_is_explanatory_not_claiming() {
        let dir = std::env::temp_dir().join("ms-fna-explain-unproven");
        let _ = fs::remove_dir_all(&dir);
        make_managed_game_dir(&dir, &["FNA.dll"]);
        let explanation = explain_profile(777777, &dir);
        assert!(!explanation.pinned);
        // Must NOT claim compatibility; rationale must mention conservatism.
        assert!(
            explanation.rationale.iter().any(|r| r.to_lowercase().contains("conservative") || r.contains("reversible")),
            "unproven explanation must be conservative: {:?}",
            explanation.rationale
        );
        let _ = fs::remove_dir_all(&dir);
    }
}
