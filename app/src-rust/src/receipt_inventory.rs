use serde_json::{json, Value};
use std::fs;
use std::path::{Path, PathBuf};

const SCHEMA: &str = "metalsharp.receipts.inventory.v1";

struct ReceiptBucket {
    id: &'static str,
    label: &'static str,
    root: PathBuf,
    pattern: &'static str,
    recursive_depth: usize,
}

pub fn report() -> Value {
    let home = dirs::home_dir().unwrap_or_default();
    report_for_home(&home)
}

fn report_for_home(home: &Path) -> Value {
    let ms_home = crate::platform::metalsharp_home_dir_for(home);
    let buckets = receipt_buckets(&ms_home);
    let entries: Vec<Value> = buckets.iter().map(bucket_report).collect();
    let total = entries.iter().filter_map(|entry| entry.get("count").and_then(|value| value.as_u64())).sum::<u64>();
    json!({
        "ok": true,
        "schema": SCHEMA,
        "readOnly": true,
        "metalsharpHome": ms_home.to_string_lossy(),
        "total": total,
        "buckets": entries,
        "invariants": [
            "Receipt inventory is filesystem-only and does not launch Wine, Steam, GOGDL, Mono, or games.",
            "Receipts are evidence records; they do not authorize install replacement.",
            "Missing receipts mean proof is absent, not that a runtime lane is impossible."
        ],
    })
}

fn receipt_buckets(ms_home: &Path) -> Vec<ReceiptBucket> {
    vec![
        ReceiptBucket {
            id: "steam_launch",
            label: "Steam MTSP launch receipts",
            root: ms_home.join("launch-receipts").join("steam"),
            pattern: "*.json",
            recursive_depth: 0,
        },
        ReceiptBucket {
            id: "sharp_launch",
            label: "Sharp Library launch receipts",
            root: ms_home.join("launch-receipts").join("sharp"),
            pattern: "*.json",
            recursive_depth: 0,
        },
        ReceiptBucket {
            id: "native_mono_launch",
            label: "Native Mono/FNA launch receipts",
            root: ms_home.join("launch-receipts").join("native-mono"),
            pattern: "*.json",
            recursive_depth: 0,
        },
        ReceiptBucket {
            id: "gog_launch",
            label: "GOGDL launch receipts",
            root: ms_home.join("gog").join("receipts"),
            pattern: "*.json",
            recursive_depth: 0,
        },
        ReceiptBucket {
            id: "steam_prefix_wineboot",
            label: "Steam prefix wineboot receipts",
            root: ms_home.join("prefix-steam").join(".metalsharp").join("receipts"),
            pattern: "wineboot-*.json",
            recursive_depth: 0,
        },
        ReceiptBucket {
            id: "steam_prefix_route_dll_staging",
            label: "Steam prefix route DLL staging receipts",
            root: ms_home.join("prefix-steam").join(".metalsharp").join("receipts"),
            pattern: "route-dll-staging-*.json",
            recursive_depth: 0,
        },
        ReceiptBucket {
            id: "gog_prefix_wineboot",
            label: "GOG prefix wineboot receipts",
            root: ms_home.join("bottles").join("gog-prefix").join("prefix").join(".metalsharp").join("receipts"),
            pattern: "wineboot-*.json",
            recursive_depth: 0,
        },
        ReceiptBucket {
            id: "gog_prefix_route_dll_staging",
            label: "GOG prefix route DLL staging receipts",
            root: ms_home.join("bottles").join("gog-prefix").join("prefix").join(".metalsharp").join("receipts"),
            pattern: "route-dll-staging-*.json",
            recursive_depth: 0,
        },
        ReceiptBucket {
            id: "gptk_prefix_wineboot",
            label: "GPTK prefix wineboot receipts",
            root: ms_home.join("prefix-gptk").join(".metalsharp").join("receipts"),
            pattern: "wineboot-*.json",
            recursive_depth: 0,
        },
        ReceiptBucket {
            id: "gptk_prefix_route_dll_staging",
            label: "GPTK prefix route DLL staging receipts",
            root: ms_home.join("prefix-gptk").join(".metalsharp").join("receipts"),
            pattern: "route-dll-staging-*.json",
            recursive_depth: 0,
        },
        ReceiptBucket {
            id: "fna_staging",
            label: "FNA game-local staging receipts",
            root: ms_home.join("games"),
            pattern: "fna-staging.json",
            recursive_depth: 5,
        },
        ReceiptBucket {
            id: "sharp_fna_staging",
            label: "Sharp Library FNA staging receipts",
            root: ms_home.join("sharp-library"),
            pattern: "fna-staging.json",
            recursive_depth: 5,
        },
    ]
}

fn bucket_report(bucket: &ReceiptBucket) -> Value {
    let files = collect_receipts(&bucket.root, bucket.pattern, bucket.recursive_depth);
    let latest = latest_receipt(&files);
    json!({
        "id": bucket.id,
        "label": bucket.label,
        "root": bucket.root.to_string_lossy(),
        "pattern": bucket.pattern,
        "present": bucket.root.is_dir(),
        "count": files.len(),
        "latest": latest,
        "paths": files.iter().map(|path| path.to_string_lossy().to_string()).collect::<Vec<_>>(),
    })
}

fn latest_receipt(files: &[PathBuf]) -> Option<Value> {
    let mut files = files.to_vec();
    files.sort_by_key(|path| std::cmp::Reverse(path.metadata().and_then(|meta| meta.modified()).ok()));
    let path = files.first()?;
    let json = read_json(path);
    Some(json!({
        "path": path.to_string_lossy(),
        "schema": json.as_ref().and_then(|value| value.get("schema")).and_then(|value| value.as_str()),
        "runtimeContractId": json.as_ref().and_then(|value| value.get("runtimeContractId")).and_then(|value| value.as_str()),
        "source": json.as_ref().and_then(|value| value.get("source")).and_then(|value| value.as_str()),
    }))
}

fn collect_receipts(root: &Path, pattern: &str, recursive_depth: usize) -> Vec<PathBuf> {
    let mut files = Vec::new();
    collect_receipts_inner(root, pattern, recursive_depth, 0, &mut files);
    files.sort();
    files
}

fn collect_receipts_inner(root: &Path, pattern: &str, max_depth: usize, depth: usize, files: &mut Vec<PathBuf>) {
    if !root.is_dir() || depth > max_depth {
        return;
    }
    let Ok(entries) = fs::read_dir(root) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_file() && matches_pattern(&path, pattern) {
            files.push(path);
        } else if path.is_dir() && depth < max_depth {
            collect_receipts_inner(&path, pattern, max_depth, depth + 1, files);
        }
    }
}

fn matches_pattern(path: &Path, pattern: &str) -> bool {
    let Some(name) = path.file_name().and_then(|name| name.to_str()) else {
        return false;
    };
    if pattern == "*.json" {
        return path.extension().and_then(|ext| ext.to_str()) == Some("json");
    }
    if let Some(prefix) = pattern.strip_suffix("*.json") {
        return name.starts_with(prefix) && name.ends_with(".json");
    }
    name == pattern
}

fn read_json(path: &Path) -> Option<Value> {
    fs::read_to_string(path).ok().and_then(|data| serde_json::from_str(&data).ok())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp_home(name: &str) -> PathBuf {
        let home = std::env::temp_dir().join(format!("metalsharp-receipts-{}-{}", name, std::process::id()));
        let _ = fs::remove_dir_all(&home);
        fs::create_dir_all(&home).expect("create home");
        home
    }

    #[test]
    fn inventory_counts_launch_and_prefix_receipts() {
        let home = temp_home("inventory");
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        let steam = ms_home.join("launch-receipts/steam");
        let gog = ms_home.join("bottles/gog-prefix/prefix/.metalsharp/receipts");
        fs::create_dir_all(&steam).expect("create steam receipts");
        fs::create_dir_all(&gog).expect("create gog receipts");
        fs::write(
            steam.join("620-launch.json"),
            br#"{"schema":"metalsharp.launch.receipt.v1","runtimeContractId":"m12_dxmt_m12","source":"steam"}"#,
        )
        .expect("write steam receipt");
        fs::write(
            gog.join("wineboot-1-gog.json"),
            br#"{"schema":"metalsharp.prefix.wineboot.receipt.v1","source":"gog"}"#,
        )
        .expect("write gog receipt");

        let report = report_for_home(&home);
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        assert_eq!(report.get("total").and_then(|value| value.as_u64()), Some(2));
        let buckets = report.get("buckets").and_then(|value| value.as_array()).expect("buckets");
        let steam_bucket = buckets
            .iter()
            .find(|bucket| bucket.get("id").and_then(|value| value.as_str()) == Some("steam_launch"))
            .expect("steam bucket");
        assert_eq!(steam_bucket.get("count").and_then(|value| value.as_u64()), Some(1));
        assert_eq!(
            steam_bucket.pointer("/latest/runtimeContractId").and_then(|value| value.as_str()),
            Some("m12_dxmt_m12")
        );
        let _ = fs::remove_dir_all(&home);
    }
}
