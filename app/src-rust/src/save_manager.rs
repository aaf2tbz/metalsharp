use serde_json::{json, Value};
use std::path::{Path, PathBuf};

const SCHEMA: &str = "metalsharp.save-manager.inventory.v1";

pub fn inventory() -> Value {
    let home = dirs::home_dir().unwrap_or_default();
    inventory_for(&home)
}

pub fn inventory_for(home: &Path) -> Value {
    let ms_home = crate::platform::metalsharp_home_dir_for(&home.to_path_buf());
    let candidates = vec![
        candidate("steam_userdata", "steam", home.join("Library/Application Support/Steam/userdata"), true),
        candidate(
            "wine_steam_userdata",
            "steam",
            ms_home.join("prefix-steam/drive_c/Program Files (x86)/Steam/userdata"),
            true,
        ),
        candidate("gog_games", "gog", ms_home.join("gog-games"), true),
        candidate("gog_prefix_users", "gog", ms_home.join("bottles/gog-prefix/prefix/drive_c/users"), true),
        candidate("sharp_library", "sharp", ms_home.join("sharp-library"), true),
        candidate("bottle_prefixes", "sharp", ms_home.join("bottles"), true),
        candidate("known_good", "metalsharp", ms_home.join("known-good"), false),
        candidate("launch_receipts", "metalsharp", ms_home.join("launch-receipts"), false),
    ];
    json!({
        "ok": true,
        "schema": SCHEMA,
        "readOnly": true,
        "backupRoot": ms_home.join("save-backups").to_string_lossy(),
        "candidates": candidates,
        "policies": {
            "beforeUninstall": "backup source-owned save candidates and receipts",
            "beforePrefixReset": "backup prefix users, AppData, Documents, Saved Games, and source manifests",
            "restore": "restore only after explicit user-selected backup and target source confirmation",
            "syncFolder": "optional user-selected folder/iCloud path"
        },
        "actions": ["GET /save-manager/inventory", "POST /save-manager/backup-plan"],
        "invariants": ["Inventory and backup plans are read-only; backup/restore requires an explicit future mutating action."],
    })
}

pub fn backup_plan(body: &serde_json::Map<String, Value>) -> Value {
    let source = body.get("source").and_then(|value| value.as_str()).unwrap_or("all");
    let inventory = inventory();
    let candidates = inventory
        .get("candidates")
        .and_then(|value| value.as_array())
        .cloned()
        .unwrap_or_default()
        .into_iter()
        .filter(|candidate| source == "all" || candidate.get("source").and_then(|value| value.as_str()) == Some(source))
        .collect::<Vec<_>>();
    json!({
        "ok": true,
        "schema": "metalsharp.save-manager.backup-plan.v1",
        "readOnly": true,
        "source": source,
        "wouldBackup": candidates,
        "requiresExplicitAction": true,
    })
}

fn candidate(id: &str, source: &str, path: PathBuf, save_sensitive: bool) -> Value {
    json!({
        "id": id,
        "source": source,
        "path": path.to_string_lossy(),
        "present": path.exists(),
        "saveSensitive": save_sensitive,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn save_inventory_covers_sources() {
        let home = std::env::temp_dir().join(format!("metalsharp-save-manager-{}", std::process::id()));
        let report = inventory_for(&home);
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        let candidates = report.get("candidates").and_then(|value| value.as_array()).expect("candidates");
        for source in ["steam", "gog", "sharp"] {
            assert!(
                candidates
                    .iter()
                    .any(|candidate| candidate.get("source").and_then(|value| value.as_str()) == Some(source)),
                "missing {source}"
            );
        }
    }
}
