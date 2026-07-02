use serde_json::{json, Value};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

const PREFIX_SCHEMA: &str = "metalsharp.prefix.v2";
const WINEBOOT_RECEIPT_SCHEMA: &str = "metalsharp.prefix.wineboot.receipt.v1";

pub fn metadata_path(prefix: &Path) -> PathBuf {
    prefix.join(".metalsharp").join("prefix-metadata-v2.json")
}

pub fn receipts_dir(prefix: &Path) -> PathBuf {
    prefix.join(".metalsharp").join("receipts")
}

pub fn read_metadata(prefix: &Path) -> Option<Value> {
    read_json(&metadata_path(prefix))
}

pub fn latest_wineboot_receipt(prefix: &Path) -> Option<Value> {
    let mut receipts: Vec<PathBuf> = fs::read_dir(receipts_dir(prefix))
        .ok()
        .into_iter()
        .flat_map(|entries| entries.flatten())
        .map(|entry| entry.path())
        .filter(|path| {
            path.file_name()
                .and_then(|name| name.to_str())
                .map(|name| name.starts_with("wineboot-") && name.ends_with(".json"))
                .unwrap_or(false)
        })
        .collect();
    receipts.sort_by_key(|path| std::cmp::Reverse(path.metadata().and_then(|meta| meta.modified()).ok()));
    receipts.first().and_then(|path| read_json(path))
}

pub fn record_wineboot_decision(
    prefix: &Path,
    source: &str,
    action: &str,
    command: &[&str],
    outcome: &str,
    detail: &str,
    exit_code: Option<i32>,
) -> Result<Value, String> {
    let now = now_secs();
    let receipt = json!({
        "schema": WINEBOOT_RECEIPT_SCHEMA,
        "timestamp": now,
        "source": source,
        "prefix": prefix.to_string_lossy(),
        "action": action,
        "command": command,
        "outcome": outcome,
        "detail": detail,
        "exitCode": exit_code,
        "readOnly": false,
    });
    let dir = receipts_dir(prefix);
    fs::create_dir_all(&dir).map_err(|error| format!("failed to create {}: {}", dir.display(), error))?;
    let path = dir.join(format!("wineboot-{}-{}.json", now, slug(source)));
    write_json(&path, &receipt)?;
    write_prefix_metadata(prefix, source, Some(&receipt))?;
    Ok(receipt)
}

pub fn write_prefix_metadata(
    prefix: &Path,
    owner: &str,
    last_wineboot_update: Option<&Value>,
) -> Result<PathBuf, String> {
    let path = metadata_path(prefix);
    let current = read_metadata(prefix).unwrap_or_else(|| json!({}));
    let metadata = json!({
        "schema": PREFIX_SCHEMA,
        "id": prefix_id_for(prefix, owner),
        "owner": owner,
        "path": prefix.to_string_lossy(),
        "present": prefix.is_dir(),
        "driveCPresent": prefix.join("drive_c").is_dir(),
        "systemRegPresent": prefix.join("system.reg").is_file(),
        "userRegPresent": prefix.join("user.reg").is_file(),
        "canonicalPath": prefix.canonicalize().ok().map(|path| path.to_string_lossy().to_string()),
        "createdOrUpdatedAt": now_secs(),
        "preservePolicy": preserve_policy_for(prefix, owner),
        "gamePayloadPolicy": game_payload_policy_for(prefix, owner),
        "lastWinebootUpdate": last_wineboot_update.cloned().or_else(|| current.get("lastWinebootUpdate").cloned()).unwrap_or(Value::Null),
        "receiptsDir": receipts_dir(prefix).to_string_lossy(),
    });
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|error| format!("failed to create {}: {}", parent.display(), error))?;
    }
    write_json(&path, &metadata)?;
    Ok(path)
}

fn read_json(path: &Path) -> Option<Value> {
    fs::read_to_string(path).ok().and_then(|data| serde_json::from_str(&data).ok())
}

fn write_json(path: &Path, value: &Value) -> Result<(), String> {
    let data = serde_json::to_string_pretty(value).map_err(|error| format!("failed to encode JSON: {}", error))?;
    fs::write(path, data).map_err(|error| format!("failed to write {}: {}", path.display(), error))
}

fn now_secs() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|duration| duration.as_secs()).unwrap_or(0)
}

fn slug(value: &str) -> String {
    value
        .chars()
        .map(|ch| if ch.is_ascii_alphanumeric() { ch.to_ascii_lowercase() } else { '-' })
        .collect::<String>()
        .trim_matches('-')
        .to_string()
}

fn prefix_id_for(prefix: &Path, owner: &str) -> String {
    if prefix.ends_with(Path::new("prefix-steam")) {
        "steam".to_string()
    } else if prefix.ends_with(Path::new("bottles/gog-prefix/prefix")) {
        "gog".to_string()
    } else if prefix.ends_with(Path::new("prefix-gptk")) {
        "gptk".to_string()
    } else {
        owner.to_string()
    }
}

fn preserve_policy_for(prefix: &Path, owner: &str) -> &'static str {
    if owner == "steam" || prefix.ends_with(Path::new("prefix-steam")) {
        "steam-metadata-only"
    } else {
        "preserve-if-present"
    }
}

fn game_payload_policy_for(prefix: &Path, owner: &str) -> &'static str {
    if owner == "steam" || prefix.ends_with(Path::new("prefix-steam")) {
        "external"
    } else if owner == "gptk" || prefix.ends_with(Path::new("prefix-gptk")) {
        "external-gptk-prefix"
    } else {
        "source-owned-prefix"
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp_prefix(name: &str) -> PathBuf {
        let path = std::env::temp_dir().join(format!("metalsharp-prefix-metadata-{}-{}", name, std::process::id()));
        let _ = fs::remove_dir_all(&path);
        fs::create_dir_all(path.join("drive_c")).expect("create temp prefix");
        path
    }

    #[test]
    fn records_wineboot_receipt_and_metadata() {
        let prefix = temp_prefix("record");
        let receipt = record_wineboot_decision(
            &prefix,
            "gog",
            "wineboot --init",
            &["metalsharp-wine", "wineboot", "--init"],
            "success",
            "prefix initialized",
            Some(0),
        )
        .expect("record receipt");
        assert_eq!(receipt.get("schema").and_then(|value| value.as_str()), Some(WINEBOOT_RECEIPT_SCHEMA));
        let metadata = read_metadata(&prefix).expect("metadata");
        assert_eq!(metadata.get("schema").and_then(|value| value.as_str()), Some(PREFIX_SCHEMA));
        assert_eq!(metadata.pointer("/lastWinebootUpdate/outcome").and_then(|value| value.as_str()), Some("success"));
        assert!(latest_wineboot_receipt(&prefix).is_some());
        let _ = fs::remove_dir_all(&prefix);
    }
}
