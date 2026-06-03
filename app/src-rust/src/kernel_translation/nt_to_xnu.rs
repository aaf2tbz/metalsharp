use super::types::*;
use serde_json::{json, Value};

pub fn syscall_coverage_report() -> CoverageReport {
    let table = super::types::syscall_table();
    let mut direct = 0;
    let mut close = 0;
    let mut userspace = 0;
    let mut partial = 0;
    let mut blocked = 0;
    for entry in &table {
        match entry.quality {
            PairQuality::Direct => direct += 1,
            PairQuality::Close => close += 1,
            PairQuality::Userspace => userspace += 1,
            PairQuality::Partial => partial += 1,
            PairQuality::Blocked => blocked += 1,
            PairQuality::NotNeeded => {},
        }
    }
    CoverageReport { total: table.len(), direct, close, userspace, partial, blocked }
}

pub fn struct_coverage_report() -> CoverageReport {
    let table = super::types::struct_table();
    let mut direct = 0;
    let mut close = 0;
    let mut userspace = 0;
    let mut partial = 0;
    let mut blocked = 0;
    for entry in &table {
        match entry.quality {
            PairQuality::Direct => direct += 1,
            PairQuality::Close => close += 1,
            PairQuality::Userspace => userspace += 1,
            PairQuality::Partial => partial += 1,
            PairQuality::Blocked => blocked += 1,
            PairQuality::NotNeeded => {},
        }
    }
    CoverageReport { total: table.len(), direct, close, userspace, partial, blocked }
}

pub fn blocker_summary() -> BlockerSummary {
    BlockerSummary { blocker_count: CRITICAL_BLOCKERS.len(), blockers: CRITICAL_BLOCKERS.to_vec() }
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
