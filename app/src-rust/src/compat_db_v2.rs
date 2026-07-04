use serde_json::{json, Value};

const SCHEMA: &str = "metalsharp.compatibility.db.v2.preview";

pub fn report() -> Value {
    let entries = vec![
        entry(
            "1245620",
            "Elden Ring",
            "m12_dxmt_m12",
            &["d3dmetal_native", "vkd3d_d12", "m11"],
            &["vcrun2019_x64", "vcrun2019_x86"],
            "offline_supported",
            "pending_launch_proof",
        ),
        entry("620", "Portal 2", "m11", &["dxvk_d11", "wine_bare"], &["vcrun2019_x86"], "none", "filesystem_validated"),
        entry("400", "Portal", "m9", &["dxvk_d9", "wine_bare"], &["vcrun2019_x86"], "none", "filesystem_validated"),
        entry(
            "504230",
            "Celeste",
            "native_mono_x86",
            &["native_mono_arm64", "wine_bare"],
            &["fna", "faudio"],
            "none",
            "receipt_backed",
        ),
        entry(
            "105600",
            "Terraria",
            "native_mono_arm64",
            &["native_mono_x86", "wine_bare"],
            &["fna", "faudio"],
            "none",
            "receipt_backed",
        ),
    ];
    json!({
        "ok": true,
        "schema": SCHEMA,
        "readOnly": true,
        "entries": entries,
        "tracks": ["preferred route", "fallback route matrix", "dependency requirements", "launch args", "anti-cheat status", "proof level", "known issues", "user override"],
        "storage": "runtime-generated preview plus existing bottle compatibility overrides",
    })
}

fn entry(
    appid: &str,
    name: &str,
    preferred: &str,
    fallbacks: &[&str],
    requires: &[&str],
    anti_cheat: &str,
    proof: &str,
) -> Value {
    json!({
        "schema": "metalsharp.compatibility.record.v2",
        "appid": appid,
        "name": name,
        "preferred": preferred,
        "fallbacks": fallbacks,
        "requires": requires,
        "antiCheat": anti_cheat,
        "proof": proof,
        "lastKnownGood": Value::Null,
        "knownIssues": [],
        "userOverride": Value::Null,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn compatibility_db_v2_contains_required_tracks() {
        let report = report();
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        let tracks = report.get("tracks").and_then(|value| value.as_array()).expect("tracks");
        for track in
            ["preferred route", "fallback route matrix", "dependency requirements", "anti-cheat status", "proof level"]
        {
            assert!(tracks.iter().any(|value| value.as_str() == Some(track)), "missing {track}");
        }
    }
}
