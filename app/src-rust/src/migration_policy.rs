use serde_json::{json, Value};

const SCHEMA: &str = "metalsharp.migration.policy.v1";
const ENTRY_SCHEMA: &str = "metalsharp.migration.policy.entry.v1";

struct MigrationPolicyEntry {
    id: &'static str,
    path: &'static str,
    owner: &'static str,
    preserve: &'static str,
    restore: &'static str,
    game_payload_policy: &'static str,
    wineboot_policy: &'static str,
    receipts: &'static [&'static str],
    invariants: &'static [&'static str],
}

fn entries() -> Vec<MigrationPolicyEntry> {
    vec![
        MigrationPolicyEntry {
            id: "prefix_steam",
            path: "~/.metalsharp/prefix-steam",
            owner: "steam",
            preserve: "metadata-only-if-present",
            restore: "restore-metadata-only",
            game_payload_policy: "exclude-steamapps-common-and-game-payloads",
            wineboot_policy:
                "run-wineboot-u-after-runtime-update-when-prefix-exists; preserve dosdevices links before and after",
            receipts: &["prefix-metadata-v2", "wineboot-receipt", "migration-report"],
            invariants: &[
                "Steam prefix is shared storefront/session state, not a game payload backup.",
                "Steam game content must remain outside migration payloads.",
            ],
        },
        MigrationPolicyEntry {
            id: "gog_prefix",
            path: "~/.metalsharp/bottles/gog-prefix/prefix",
            owner: "gog",
            preserve: "full-prefix-if-present",
            restore: "restore-full-prefix-if-preserved",
            game_payload_policy: "source-owned-prefix",
            wineboot_policy: "run-wineboot-u-after-runtime-update-when-prefix-exists; never redirect to prefix-steam",
            receipts: &["prefix-metadata-v2", "wineboot-receipt", "migration-report"],
            invariants: &[
                "GOG must use the dedicated gog-prefix bottle.",
                "GOG prefix must never alias or live under prefix-steam.",
            ],
        },
        MigrationPolicyEntry {
            id: "prefix_gptk",
            path: "~/.metalsharp/prefix-gptk",
            owner: "gptk",
            preserve: "full-prefix-if-present",
            restore: "restore-full-prefix-if-preserved",
            game_payload_policy: "external-gptk-prefix",
            wineboot_policy: "GPTK-owned wineboot policy; do not silently mix with DXMT/Wine Steam prefix",
            receipts: &["prefix-metadata-v2", "migration-report"],
            invariants: &[
                "GPTK/D3DMetal remains external runtime territory.",
                "GPTK prefix is not a substitute for dxmt_m12 readiness.",
            ],
        },
        MigrationPolicyEntry {
            id: "sharp_bottles",
            path: "~/.metalsharp/bottles/*",
            owner: "sharp-library",
            preserve: "preserve-bottle-metadata-and-runtime-state",
            restore: "restore-bottle-manifests-and-prefix-metadata",
            game_payload_policy: "preserve-installer-and-app-bottle-state-without-inventing-game-copies",
            wineboot_policy: "bottle-specific repair path only; no implicit launch during migration diagnostics",
            receipts: &["bottle-manifest", "prefix-metadata-v2", "migration-report"],
            invariants: &[
                "Installer bottles keep source installer provenance and detected app candidates.",
                "Launcher helper executables are runtime components, not Sharp Library apps.",
            ],
        },
        MigrationPolicyEntry {
            id: "launch_receipts",
            path: "~/.metalsharp/launch-receipts/",
            owner: "diagnostics",
            preserve: "preserve-if-present",
            restore: "restore-if-preserved",
            game_payload_policy: "metadata-only",
            wineboot_policy: "not-applicable",
            receipts: &["metalsharp.launch.receipt.v1"],
            invariants: &[
                "Launch receipts are evidence records and must not authorize install replacement.",
                "Receipts may describe launches but migration diagnostics must not launch games.",
            ],
        },
        MigrationPolicyEntry {
            id: "runtime_payloads",
            path: "~/.metalsharp/runtime/",
            owner: "installer",
            preserve: "replace-from-validated-bundles",
            restore: "reinstall-runtime-payloads-from-current-build",
            game_payload_policy: "no-game-payloads",
            wineboot_policy: "prefix updates occur after runtime payload replacement, never as diagnostics",
            receipts: &["runtime-manifest", "runtime-contracts", "runtime-diagnostics"],
            invariants: &[
                "Canonical M12 installed surface is runtime/wine/lib/dxmt_m12.",
                "DXVK/VKD3D fallback payloads are experimental fallback lanes, not defaults.",
            ],
        },
    ]
}

fn entry_json(entry: MigrationPolicyEntry) -> Value {
    json!({
        "schema": ENTRY_SCHEMA,
        "id": entry.id,
        "path": entry.path,
        "owner": entry.owner,
        "preserve": entry.preserve,
        "restore": entry.restore,
        "gamePayloadPolicy": entry.game_payload_policy,
        "winebootPolicy": entry.wineboot_policy,
        "receipts": entry.receipts,
        "invariants": entry.invariants,
    })
}

pub fn report() -> Value {
    json!({
        "ok": true,
        "schema": SCHEMA,
        "readOnly": true,
        "entries": entries().into_iter().map(entry_json).collect::<Vec<_>>(),
        "globalInvariants": [
            "Migration policy is read-only and must not launch Wine, Steam, GOGDL, or games.",
            "Install replacement remains explicitly guarded and is not authorized by diagnostics.",
            "GOG uses ~/.metalsharp/bottles/gog-prefix/prefix and must never use prefix-steam.",
            "Steam migration excludes game payloads while preserving storefront/session metadata."
        ],
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn migration_policy_reports_gog_and_steam_invariants() {
        let report = report();
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        assert_eq!(report.get("readOnly").and_then(|value| value.as_bool()), Some(true));
        let entries = report.get("entries").and_then(|value| value.as_array()).expect("entries");
        let steam = entries
            .iter()
            .find(|entry| entry.get("id").and_then(|value| value.as_str()) == Some("prefix_steam"))
            .expect("steam policy");
        assert_eq!(
            steam.get("gamePayloadPolicy").and_then(|value| value.as_str()),
            Some("exclude-steamapps-common-and-game-payloads")
        );
        let gog = entries
            .iter()
            .find(|entry| entry.get("id").and_then(|value| value.as_str()) == Some("gog_prefix"))
            .expect("gog policy");
        assert!(gog
            .get("invariants")
            .and_then(|value| value.as_array())
            .unwrap()
            .iter()
            .any(|invariant| invariant.as_str().unwrap_or_default().contains("never alias")));
    }
}
