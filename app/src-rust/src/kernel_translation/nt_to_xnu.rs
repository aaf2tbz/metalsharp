use super::types::*;
use serde_json::{json, Value};

pub fn syscall_coverage_report() -> CoverageReport {
    let table = super::types::syscall_table();
    let mut direct = 0;
    let mut close = 0;
    let mut userspace = 0;
    let mut partial = 0;
    let mut blocked = 0;
    let mut not_needed = 0;
    for entry in &table {
        match entry.quality {
            PairQuality::Direct => direct += 1,
            PairQuality::Close => close += 1,
            PairQuality::Userspace => userspace += 1,
            PairQuality::Partial => partial += 1,
            PairQuality::Blocked => blocked += 1,
            PairQuality::NotNeeded => not_needed += 1,
        }
    }
    CoverageReport { total: table.len(), direct, close, userspace, partial, blocked, not_needed }
}

pub fn struct_coverage_report() -> CoverageReport {
    let table = super::types::struct_table();
    let mut direct = 0;
    let mut close = 0;
    let mut userspace = 0;
    let mut partial = 0;
    let mut blocked = 0;
    let mut not_needed = 0;
    for entry in &table {
        match entry.quality {
            PairQuality::Direct => direct += 1,
            PairQuality::Close => close += 1,
            PairQuality::Userspace => userspace += 1,
            PairQuality::Partial => partial += 1,
            PairQuality::Blocked => blocked += 1,
            PairQuality::NotNeeded => not_needed += 1,
        }
    }
    CoverageReport { total: table.len(), direct, close, userspace, partial, blocked, not_needed }
}

pub fn object_type_coverage_report() -> CoverageReport {
    let table = super::types::object_type_table();
    let mut direct = 0;
    let mut close = 0;
    let mut userspace = 0;
    let mut partial = 0;
    let mut blocked = 0;
    let mut not_needed = 0;
    for entry in &table {
        match entry.quality {
            PairQuality::Direct => direct += 1,
            PairQuality::Close => close += 1,
            PairQuality::Userspace => userspace += 1,
            PairQuality::Partial => partial += 1,
            PairQuality::Blocked => blocked += 1,
            PairQuality::NotNeeded => not_needed += 1,
        }
    }
    CoverageReport { total: table.len(), direct, close, userspace, partial, blocked, not_needed }
}

pub fn drill_target_summary() -> Vec<DrillTarget> {
    DRILL_TARGETS.to_vec()
}

pub fn executive_function_summary() -> Vec<ExecutiveFunctionMapping> {
    super::types::executive_function_table()
}

pub fn endpoint_security_summary() -> Vec<EndpointSecurityMapping> {
    super::types::endpoint_security_table()
}

pub fn syscall_category_breakdown() -> Value {
    let table = super::types::syscall_table();
    let mut categories: std::collections::BTreeMap<&str, serde_json::Map<String, Value>> =
        std::collections::BTreeMap::new();
    for entry in &table {
        let cat = categories.entry(entry.category).or_default();
        let count = cat.get("count").and_then(|v| v.as_u64()).unwrap_or(0) + 1;
        cat.insert("count".to_string(), json!(count));
        let key = match entry.quality {
            PairQuality::Direct => "direct",
            PairQuality::Close => "close",
            PairQuality::Userspace => "userspace",
            PairQuality::Partial => "partial",
            PairQuality::Blocked => "blocked",
            PairQuality::NotNeeded => "notNeeded",
        };
        let kval = cat.get(key).and_then(|v| v.as_u64()).unwrap_or(0) + 1;
        cat.insert(key.to_string(), json!(kval));
    }
    json!(categories)
}

pub fn handle_syscall_lookup(nt_name: &str) -> Value {
    let table = super::types::syscall_table();
    match table.iter().find(|e| e.nt_name == nt_name) {
        Some(entry) => json!({
            "ok": true,
            "mapping": entry,
        }),
        None => json!({
            "ok": false,
            "error": format!("NT syscall '{}' not found in translation table", nt_name),
        }),
    }
}
