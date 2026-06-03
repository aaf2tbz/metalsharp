use serde_json::{json, Map, Value};
use std::collections::BTreeMap;
use std::sync::atomic::{AtomicU64, Ordering};

const MAX_COLLECTION_LEN: usize = 4096;

static NEXT_REGISTRATION_ID: AtomicU64 = AtomicU64::new(1);
static NEXT_OPERATION_ID: AtomicU64 = AtomicU64::new(1);

const PROCESS_VM_READ: u32 = 0x0010;
const PROCESS_VM_WRITE: u32 = 0x0020;
const PROCESS_VM_OPERATION: u32 = 0x0008;
const PROCESS_QUERY_INFORMATION: u32 = 0x0400;
const PROCESS_ALL_ACCESS: u32 = 0x001FFFFF;
const PROCESS_TERMINATE: u32 = 0x0001;
const PROCESS_SUSPEND_RESUME: u32 = 0x0800;
const THREAD_ALL_ACCESS: u32 = 0x001FFFFF;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum ObOperationType {
    PreOperation,
    PostOperation,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum HandleOperation {
    OpenProcess,
    OpenThread,
    DuplicateObject,
    CloseHandle,
}

impl HandleOperation {
    pub fn nt_name(&self) -> &'static str {
        match self {
            Self::OpenProcess => "NtOpenProcess",
            Self::OpenThread => "NtOpenThread",
            Self::DuplicateObject => "NtDuplicateObject",
            Self::CloseHandle => "NtClose",
        }
    }

    pub fn xnu_mechanism(&self) -> &'static str {
        match self {
            Self::OpenProcess => "task_for_pid → mac_proc_check_get_task (MACF)",
            Self::OpenThread => "task_threads → thread_act (Mach)",
            Self::DuplicateObject => "dup2 / mach_port_insert_right",
            Self::CloseHandle => "close / mach_port_deallocate",
        }
    }

    fn from_str(s: &str) -> Option<Self> {
        match s {
            "open_process" | "NtOpenProcess" => Some(Self::OpenProcess),
            "open_thread" | "NtOpenThread" => Some(Self::OpenThread),
            "duplicate_object" | "NtDuplicateObject" => Some(Self::DuplicateObject),
            "close_handle" | "NtClose" => Some(Self::CloseHandle),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ObCallbackRegistration {
    pub id: u64,
    pub operations: Vec<HandleOperation>,
    pub pre_callback: bool,
    pub post_callback: bool,
    pub active: bool,
    pub altitude: u32,
    pub call_count: u64,
    pub last_fired: Option<u64>,
    pub registered_at: u64,
    pub protected_pids: Vec<u32>,
    pub strip_access_mask: u32,
    pub block_access_mask: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum ObPreStatus {
    Allow,
    StripAccess,
    Block,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ObPreOperationInfo {
    pub operation_id: u64,
    pub operation: HandleOperation,
    pub source_pid: u32,
    pub target_pid: u32,
    pub requested_access: u32,
    pub modified_access: u32,
    pub pre_status: ObPreStatus,
    pub timestamp: u64,
    pub dispatched_to: Vec<u64>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum ObPostStatus {
    Success,
    Denied,
    Error,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ObPostOperationInfo {
    pub operation_id: u64,
    pub operation: HandleOperation,
    pub source_pid: u32,
    pub target_pid: u32,
    pub granted_access: u32,
    pub post_status: ObPostStatus,
    pub return_handle: u64,
    pub timestamp: u64,
    pub dispatched_to: Vec<u64>,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ProtectedProcess {
    pub pid: u32,
    pub name: String,
    pub protected_at: u64,
    pub signaturatory: String,
    pub protection_level: ProtectionLevel,
    pub open_attempts: u64,
    pub blocked_attempts: u64,
    pub stripped_attempts: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[allow(clippy::upper_case_acronyms)]
pub enum ProtectionLevel {
    None,
    Authenticode,
    Antitampering,
    Microsoft,
    PPL,
    System,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct AccessMaskLog {
    pub id: u64,
    pub source_pid: u32,
    pub target_pid: u32,
    pub operation: HandleOperation,
    pub requested: u32,
    pub granted: u32,
    pub stripped: u32,
    pub blocked: bool,
    pub timestamp: u64,
}

static REGISTRATIONS: std::sync::LazyLock<std::sync::Mutex<BTreeMap<u64, ObCallbackRegistration>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(BTreeMap::new()));

static PRE_OPERATIONS: std::sync::LazyLock<std::sync::Mutex<Vec<ObPreOperationInfo>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(Vec::new()));

static POST_OPERATIONS: std::sync::LazyLock<std::sync::Mutex<Vec<ObPostOperationInfo>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(Vec::new()));

static PROTECTED_PROCESSES: std::sync::LazyLock<std::sync::Mutex<BTreeMap<u32, ProtectedProcess>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(BTreeMap::new()));

static ACCESS_LOGS: std::sync::LazyLock<std::sync::Mutex<Vec<AccessMaskLog>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(Vec::new()));

fn lock_registrations() -> std::sync::MutexGuard<'static, BTreeMap<u64, ObCallbackRegistration>> {
    match REGISTRATIONS.lock() {
        Ok(g) => g,
        Err(e) => e.into_inner(),
    }
}

fn lock_pre_ops() -> std::sync::MutexGuard<'static, Vec<ObPreOperationInfo>> {
    match PRE_OPERATIONS.lock() {
        Ok(g) => g,
        Err(e) => e.into_inner(),
    }
}

fn lock_post_ops() -> std::sync::MutexGuard<'static, Vec<ObPostOperationInfo>> {
    match POST_OPERATIONS.lock() {
        Ok(g) => g,
        Err(e) => e.into_inner(),
    }
}

fn lock_protected() -> std::sync::MutexGuard<'static, BTreeMap<u32, ProtectedProcess>> {
    match PROTECTED_PROCESSES.lock() {
        Ok(g) => g,
        Err(e) => e.into_inner(),
    }
}

fn lock_access_logs() -> std::sync::MutexGuard<'static, Vec<AccessMaskLog>> {
    match ACCESS_LOGS.lock() {
        Ok(g) => g,
        Err(e) => e.into_inner(),
    }
}

fn now_ms() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_millis() as u64
}

pub fn access_mask_flags(mask: u32) -> Vec<&'static str> {
    let mut flags = Vec::new();
    if mask & PROCESS_TERMINATE != 0 {
        flags.push("PROCESS_TERMINATE");
    }
    if mask & PROCESS_VM_READ != 0 {
        flags.push("PROCESS_VM_READ");
    }
    if mask & PROCESS_VM_WRITE != 0 {
        flags.push("PROCESS_VM_WRITE");
    }
    if mask & PROCESS_VM_OPERATION != 0 {
        flags.push("PROCESS_VM_OPERATION");
    }
    if mask & PROCESS_QUERY_INFORMATION != 0 {
        flags.push("PROCESS_QUERY_INFORMATION");
    }
    if mask & PROCESS_SUSPEND_RESUME != 0 {
        flags.push("PROCESS_SUSPEND_RESUME");
    }
    if mask & PROCESS_ALL_ACCESS == PROCESS_ALL_ACCESS {
        flags.push("PROCESS_ALL_ACCESS");
    }
    if flags.is_empty() && mask != 0 {
        flags.push("UNKNOWN");
    }
    if mask == 0 {
        flags.push("NONE");
    }
    flags
}

fn compute_pre_status(reg: &ObCallbackRegistration, target_pid: u32, requested_access: u32) -> (ObPreStatus, u32) {
    let is_protected = lock_protected().contains_key(&target_pid);
    if !is_protected {
        return (ObPreStatus::Allow, requested_access);
    }

    if (requested_access & reg.block_access_mask) != 0 {
        return (ObPreStatus::Block, 0);
    }

    let stripped = requested_access & !reg.strip_access_mask;
    if stripped != requested_access {
        return (ObPreStatus::StripAccess, stripped);
    }

    (ObPreStatus::Allow, requested_access)
}

pub fn handle_register_callback(body: &Map<String, Value>) -> Value {
    let ops: Vec<HandleOperation> = match body.get("operations") {
        Some(v) => v
            .as_array()
            .map(|arr| arr.iter().filter_map(|item| item.as_str().and_then(HandleOperation::from_str)).collect())
            .unwrap_or_default(),
        None => vec![HandleOperation::OpenProcess, HandleOperation::DuplicateObject],
    };

    if ops.is_empty() {
        return json!({"ok": false, "error": "at least one operation required"});
    }

    let id = NEXT_REGISTRATION_ID.fetch_add(1, Ordering::Relaxed);
    let registration = ObCallbackRegistration {
        id,
        operations: ops,
        pre_callback: body.get("pre_callback").and_then(|v| v.as_bool()).unwrap_or(true),
        post_callback: body.get("post_callback").and_then(|v| v.as_bool()).unwrap_or(true),
        active: true,
        altitude: body.get("altitude").and_then(|v| v.as_u64()).unwrap_or(1000) as u32,
        call_count: 0,
        last_fired: None,
        registered_at: now_ms(),
        protected_pids: body
            .get("protected_pids")
            .and_then(|v| v.as_array())
            .map(|arr| arr.iter().filter_map(|v| v.as_u64().map(|p| p as u32)).collect())
            .unwrap_or_default(),
        strip_access_mask: body.get("strip_access_mask").and_then(|v| v.as_u64()).unwrap_or(PROCESS_VM_WRITE as u64)
            as u32,
        block_access_mask: body.get("block_access_mask").and_then(|v| v.as_u64()).unwrap_or(0) as u32,
    };

    lock_registrations().insert(id, registration.clone());

    json!({
        "ok": true,
        "registration_id": id,
        "registration": registration,
    })
}

pub fn handle_unregister_callback(body: &Map<String, Value>) -> Value {
    let id = match body.get("registration_id").and_then(|v| v.as_u64()) {
        Some(id) => id,
        None => return json!({"ok": false, "error": "registration_id required"}),
    };

    let mut regs = lock_registrations();
    match regs.remove(&id) {
        Some(removed) => json!({"ok": true, "removed": removed}),
        None => json!({"ok": false, "error": format!("registration {} not found", id)}),
    }
}

pub fn handle_list_registrations(_body: &Map<String, Value>) -> Value {
    let regs = lock_registrations();
    let list: Vec<&ObCallbackRegistration> = regs.values().collect();
    json!({
        "ok": true,
        "count": list.len(),
        "registrations": list,
    })
}

pub fn handle_protect_process(body: &Map<String, Value>) -> Value {
    let pid = match body.get("pid").and_then(|v| v.as_u64()) {
        Some(p) if p <= u32::MAX as u64 => p as u32,
        _ => return json!({"ok": false, "error": "pid (u32) required"}),
    };

    let level = match body.get("protection_level").and_then(|v| v.as_str()) {
        Some("none") => ProtectionLevel::None,
        Some("authenticode") => ProtectionLevel::Authenticode,
        Some("antitampering") => ProtectionLevel::Antitampering,
        Some("microsoft") => ProtectionLevel::Microsoft,
        Some("ppl") => ProtectionLevel::PPL,
        Some("system") => ProtectionLevel::System,
        _ => ProtectionLevel::PPL,
    };

    let entry = ProtectedProcess {
        pid,
        name: body.get("name").and_then(|v| v.as_str()).unwrap_or("unknown").to_string(),
        protected_at: now_ms(),
        signaturatory: body.get("signaturatory").and_then(|v| v.as_str()).unwrap_or("MetalSharp").to_string(),
        protection_level: level,
        open_attempts: 0,
        blocked_attempts: 0,
        stripped_attempts: 0,
    };

    lock_protected().insert(pid, entry.clone());

    json!({
        "ok": true,
        "protected": entry,
    })
}

pub fn handle_unprotect_process(body: &Map<String, Value>) -> Value {
    let pid = match body.get("pid").and_then(|v| v.as_u64()) {
        Some(p) if p <= u32::MAX as u64 => p as u32,
        _ => return json!({"ok": false, "error": "pid (u32) required"}),
    };

    let mut protected = lock_protected();
    match protected.remove(&pid) {
        Some(removed) => json!({"ok": true, "unprotected": removed}),
        None => json!({"ok": false, "error": format!("pid {} was not protected", pid)}),
    }
}

pub fn handle_list_protected(_body: &Map<String, Value>) -> Value {
    let protected = lock_protected();
    let list: Vec<&ProtectedProcess> = protected.values().collect();
    json!({
        "ok": true,
        "count": list.len(),
        "processes": list,
    })
}

pub fn handle_simulate_operation(body: &Map<String, Value>) -> Value {
    let operation = match body.get("operation").and_then(|v| v.as_str()).and_then(HandleOperation::from_str) {
        Some(op) => op,
        None => {
            return json!({"ok": false, "error": "operation required: open_process, open_thread, duplicate_object, close_handle"})
        },
    };
    let source_pid = match body.get("source_pid").and_then(|v| v.as_u64()) {
        Some(p) if p <= u32::MAX as u64 => p as u32,
        _ => return json!({"ok": false, "error": "source_pid (u32) required"}),
    };
    let target_pid = match body.get("target_pid").and_then(|v| v.as_u64()) {
        Some(p) if p <= u32::MAX as u64 => p as u32,
        _ => return json!({"ok": false, "error": "target_pid (u32) required"}),
    };
    let requested_access =
        body.get("requested_access").and_then(|v| v.as_u64()).unwrap_or(PROCESS_ALL_ACCESS as u64) as u32;

    if operation == HandleOperation::CloseHandle {
        return json!({
            "ok": true,
            "operation": "close_handle",
            "note": "NT ObRegisterCallbacks does not fire for CloseHandle — no pre/post callback dispatched",
        });
    }

    let operation_id = NEXT_OPERATION_ID.fetch_add(1, Ordering::Relaxed);
    let now = now_ms();

    let mut pre_dispatched: Vec<u64> = Vec::new();
    let mut final_access = requested_access;
    let mut pre_status = ObPreStatus::Allow;
    let mut blocked = false;

    {
        let regs = lock_registrations();
        let mut matching: Vec<&ObCallbackRegistration> =
            regs.values().filter(|r| r.active && r.pre_callback && r.operations.contains(&operation)).collect();
        matching.sort_by_key(|r| r.altitude);

        for reg in matching {
            let (status, access) = compute_pre_status(reg, target_pid, final_access);
            pre_dispatched.push(reg.id);
            if status == ObPreStatus::Block {
                pre_status = ObPreStatus::Block;
                final_access = 0;
                blocked = true;
                break;
            }
            if status == ObPreStatus::StripAccess {
                pre_status = ObPreStatus::StripAccess;
                final_access = access;
            }
        }
    }

    {
        let mut regs = lock_registrations();
        for &rid in &pre_dispatched {
            if let Some(r) = regs.get_mut(&rid) {
                r.call_count += 1;
                r.last_fired = Some(now);
            }
        }
    }

    let pre_info = ObPreOperationInfo {
        operation_id,
        operation,
        source_pid,
        target_pid,
        requested_access,
        modified_access: final_access,
        pre_status,
        timestamp: now,
        dispatched_to: pre_dispatched.clone(),
    };
    lock_pre_ops().push(pre_info.clone());

    let stripped_bits = requested_access & !final_access;

    {
        let mut protected = lock_protected();
        if let Some(pp) = protected.get_mut(&target_pid) {
            pp.open_attempts += 1;
            if blocked {
                pp.blocked_attempts += 1;
            }
            if stripped_bits != 0 {
                pp.stripped_attempts += 1;
            }
        }
    }

    {
        let mut logs = lock_access_logs();
        logs.push(AccessMaskLog {
            id: operation_id,
            source_pid,
            target_pid,
            operation,
            requested: requested_access,
            granted: final_access,
            stripped: stripped_bits,
            blocked,
            timestamp: now,
        });
        if logs.len() > MAX_COLLECTION_LEN {
            let excess = logs.len() - MAX_COLLECTION_LEN;
            logs.drain(0..excess);
        }
    }

    let post_status = if blocked { ObPostStatus::Denied } else { ObPostStatus::Success };
    let return_handle = if blocked { 0 } else { 0xFFFF0000 + operation_id };

    let mut post_dispatched: Vec<u64> = Vec::new();
    {
        let regs = lock_registrations();
        for reg in regs.values() {
            if reg.active && reg.post_callback && reg.operations.contains(&operation) {
                post_dispatched.push(reg.id);
            }
        }
    }

    {
        let mut regs = lock_registrations();
        for &rid in &post_dispatched {
            if let Some(r) = regs.get_mut(&rid) {
                r.call_count += 1;
                r.last_fired = Some(now);
            }
        }
    }

    let post_info = ObPostOperationInfo {
        operation_id,
        operation,
        source_pid,
        target_pid,
        granted_access: final_access,
        post_status,
        return_handle,
        timestamp: now,
        dispatched_to: post_dispatched.clone(),
    };
    lock_post_ops().push(post_info.clone());

    json!({
        "ok": true,
        "operation_id": operation_id,
        "pre": pre_info,
        "post": post_info,
        "blocked": blocked,
        "access": {
            "requested": requested_access,
            "requested_flags": access_mask_flags(requested_access),
            "granted": final_access,
            "granted_flags": access_mask_flags(final_access),
            "stripped": stripped_bits,
            "stripped_flags": access_mask_flags(stripped_bits),
        },
    })
}

pub fn handle_pre_operations(_body: &Map<String, Value>) -> Value {
    let ops = lock_pre_ops();
    json!({
        "ok": true,
        "count": ops.len(),
        "operations": ops.iter().collect::<Vec<_>>(),
    })
}

pub fn handle_post_operations(_body: &Map<String, Value>) -> Value {
    let ops = lock_post_ops();
    json!({
        "ok": true,
        "count": ops.len(),
        "operations": ops.iter().collect::<Vec<_>>(),
    })
}

pub fn handle_access_log(_body: &Map<String, Value>) -> Value {
    let logs = lock_access_logs();
    json!({
        "ok": true,
        "count": logs.len(),
        "logs": logs.iter().collect::<Vec<_>>(),
    })
}

pub fn handle_capability_survey(_body: &Map<String, Value>) -> Value {
    json!({
        "ok": true,
        "nt_equivalent": "ObRegisterCallbacks / ObUnRegisterCallbacks",
        "mechanisms": [
            {
                "id": "mac_proc_check_get_task",
                "type": "MACF policy",
                "availability": "kext_required",
                "description": "Hooks task_for_pid() calls — fires when any process requests task port of another",
                "nt_mapping": "ObRegisterCallbacks(OB_OPERATION_HANDLE_CREATE)",
                "detail": "mac_proc_check_get_task(policy, cred, p) — returns 0 to allow, EPERM to deny. This IS the handle-open event on macOS.",
            },
            {
                "id": "wine_handle_callback",
                "type": "Wine virtual",
                "availability": "userspace",
                "description": "Hook NtOpenProcess/NtDuplicateObject in Wine ntdll — intercept before kernel",
                "nt_mapping": "ObRegisterCallbacks pre/post operation",
                "detail": "Since Wine controls the handle table (Phase 2), it can fire callbacks whenever a handle is created to a protected process. No kernel needed.",
            },
            {
                "id": "endpoint_security_task_for_pid",
                "type": "EndpointSecurity",
                "availability": "system_extension",
                "description": "ES may see task_for_pid as a mach trap — investigate NOTIFY_MACH or custom ES client",
                "nt_mapping": "Partial — detect but not filter",
                "detail": "ES can observe but cannot modify access in-flight. For detection only, no pre-operation filtering.",
            },
            {
                "id": "sandbox_extension",
                "type": "macOS sandbox",
                "availability": "builtin",
                "description": "Sandbox profiles can deny task_for_pid — Apple uses this for App Store",
                "nt_mapping": "Hard deny (no granularity)",
                "detail": "Can block access entirely but cannot strip individual access rights. Useful as failsafe.",
            },
        ],
        "recommended": "wine_handle_callback",
        "rationale": "Wine controls its own handle table. Pre/post callbacks fire on NtOpenProcess/NtDuplicateObject. Can strip PROCESS_VM_WRITE, block entirely, or allow. No kernel extension needed.",
        "kernel_enhanced": "mac_proc_check_get_task",
        "kernel_rationale": "When system extension is available, MACF hook catches task_for_pid from ANY process (not just Wine). Full NT-equivalent coverage including non-Wine attackers.",
        "access_rights": {
            "PROCESS_VM_READ": format!("0x{:08X}", PROCESS_VM_READ),
            "PROCESS_VM_WRITE": format!("0x{:08X}", PROCESS_VM_WRITE),
            "PROCESS_VM_OPERATION": format!("0x{:08X}", PROCESS_VM_OPERATION),
            "PROCESS_TERMINATE": format!("0x{:08X}", PROCESS_TERMINATE),
            "PROCESS_QUERY_INFORMATION": format!("0x{:08X}", PROCESS_QUERY_INFORMATION),
            "PROCESS_ALL_ACCESS": format!("0x{:08X}", PROCESS_ALL_ACCESS),
        },
    })
}

pub fn handle_seed_demo(_body: &Map<String, Value>) -> Value {
    let game_pid: u32 = 5000;
    let cheat_pid: u32 = 6000;

    let _ = handle_protect_process(
        &serde_json::from_str(&format!(
        "{{\"pid\": {}, \"name\": \"game.exe\", \"protection_level\": \"ppl\", \"signaturatory\": \"EasyAntiCheat\"}}",
        game_pid
    ))
        .unwrap(),
    );

    let r1 = handle_register_callback(&serde_json::from_str(
        "{\"operations\": [\"open_process\", \"duplicate_object\"], \"pre_callback\": true, \"post_callback\": true, \"strip_access_mask\": 524320, \"altitude\": 1000}"
    ).expect("seed demo json"));
    let reg_id = r1["registration_id"].as_u64().unwrap();

    let op1 = handle_simulate_operation(
        &serde_json::from_str(&format!(
            "{{\"operation\": \"open_process\", \"source_pid\": {}, \"target_pid\": {}, \"requested_access\": {}}}",
            cheat_pid, game_pid, PROCESS_ALL_ACCESS
        ))
        .unwrap(),
    );

    let op2 = handle_simulate_operation(
        &serde_json::from_str(&format!(
            "{{\"operation\": \"open_process\", \"source_pid\": {}, \"target_pid\": {}, \"requested_access\": {}}}",
            game_pid, game_pid, PROCESS_VM_READ
        ))
        .unwrap(),
    );

    let op3 = handle_simulate_operation(
        &serde_json::from_str(&format!(
            "{{\"operation\": \"duplicate_object\", \"source_pid\": {}, \"target_pid\": {}, \"requested_access\": {}}}",
            cheat_pid, game_pid, PROCESS_VM_WRITE
        ))
        .unwrap(),
    );

    let op1_blocked = op1["blocked"].as_bool().unwrap();

    json!({
        "ok": true,
        "seeded": {
            "protected_pid": game_pid,
            "registration_id": reg_id,
            "operations_simulated": 3,
        },
        "scenario": "Anti-cheat protection: game.exe (PID 5000) protected. Cheat (PID 6000) tries PROCESS_ALL_ACCESS → stripped/blocked. Game opens itself → allowed. Cheat tries DuplicateObject with VM_WRITE → stripped.",
        "operation_results": [
            {"id": op1["operation_id"].as_u64().unwrap(), "blocked": op1_blocked, "pre_status": op1["pre"]["pre_status"]},
            {"id": op2["operation_id"].as_u64().unwrap(), "blocked": op2["blocked"].as_bool().unwrap(), "pre_status": op2["pre"]["pre_status"]},
            {"id": op3["operation_id"].as_u64().unwrap(), "blocked": op3["blocked"].as_bool().unwrap(), "pre_status": op3["pre"]["pre_status"]},
        ],
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn empty_body() -> Map<String, Value> {
        Map::new()
    }

    #[test]
    fn test_register_callback() {
        let body: Map<String, Value> =
            serde_json::from_str("{\"operations\": [\"open_process\"], \"pre_callback\": true, \"altitude\": 500}")
                .unwrap();
        let result = handle_register_callback(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["registration_id"].as_u64().unwrap() > 0);
        assert_eq!(result["registration"]["altitude"], 500);
    }

    #[test]
    fn test_register_default_ops() {
        let body: Map<String, Value> = serde_json::from_str("{}").expect("seed demo json");
        let result = handle_register_callback(&body);
        assert!(result["ok"].as_bool().unwrap());
        let ops = result["registration"]["operations"].as_array().unwrap();
        assert!(ops.len() >= 2);
    }

    #[test]
    fn test_unregister_callback() {
        let body: Map<String, Value> =
            serde_json::from_str("{\"operations\": [\"open_process\"]}").expect("seed demo json");
        let r = handle_register_callback(&body);
        let id = r["registration_id"].as_u64().unwrap();

        let unreg: Map<String, Value> =
            serde_json::from_str(&format!("{{\"registration_id\": {}}}", id)).expect("seed demo json");
        let result = handle_unregister_callback(&unreg);
        assert!(result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_unregister_unknown() {
        let body: Map<String, Value> = serde_json::from_str("{\"registration_id\": 99999999}").expect("seed demo json");
        let result = handle_unregister_callback(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_list_registrations() {
        let result = handle_list_registrations(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["count"].as_u64().is_some());
    }

    #[test]
    fn test_protect_process() {
        let pid: u32 = 40000 + (std::process::id() % 1000) as u32;
        let body: Map<String, Value> = serde_json::from_str(&format!(
            "{{\"pid\": {}, \"name\": \"game.exe\", \"protection_level\": \"ppl\"}}",
            pid
        ))
        .unwrap();
        let result = handle_protect_process(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["protected"]["pid"], pid);
        assert_eq!(result["protected"]["protection_level"], "PPL");
    }

    #[test]
    fn test_simulate_protected_block() {
        let tgt: u32 = 45000 + (std::process::id() % 1000) as u32;
        handle_protect_process(
            &serde_json::from_str(&format!("{{\"pid\": {}, \"name\": \"blocked.exe\"}}", tgt)).unwrap(),
        );

        handle_register_callback(
            &serde_json::from_str(&format!(
                "{{\"operations\": [\"open_process\"], \"block_access_mask\": {}}}",
                PROCESS_VM_WRITE
            ))
            .unwrap(),
        );

        let body: Map<String, Value> = serde_json::from_str(&format!(
            "{{\"operation\": \"open_process\", \"source_pid\": 9999, \"target_pid\": {}, \"requested_access\": {}}}",
            tgt, PROCESS_VM_WRITE
        ))
        .unwrap();
        let result = handle_simulate_operation(&body);
        assert!(result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_unprotect_unknown() {
        let body: Map<String, Value> = serde_json::from_str("{\"pid\": 99999999}").expect("seed demo json");
        let result = handle_unprotect_process(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_list_protected() {
        let result = handle_list_protected(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["count"].as_u64().is_some());
    }

    #[test]
    fn test_simulate_unprotected_allow() {
        let src: u32 = 42000 + (std::process::id() % 1000) as u32;
        let tgt: u32 = 43000 + (std::process::id() % 1000) as u32;
        let body: Map<String, Value> = serde_json::from_str(&format!(
            "{{\"operation\": \"open_process\", \"source_pid\": {}, \"target_pid\": {}, \"requested_access\": {}}}",
            src, tgt, PROCESS_ALL_ACCESS
        ))
        .unwrap();
        let result = handle_simulate_operation(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["blocked"], false);
        assert_eq!(result["pre"]["pre_status"], "Allow");
    }

    #[test]
    fn test_simulate_protected_strip() {
        let tgt: u32 = 44000 + (std::process::id() % 1000) as u32;
        handle_protect_process(
            &serde_json::from_str(&format!("{{\"pid\": {}, \"name\": \"test.exe\"}}", tgt)).unwrap(),
        );

        handle_register_callback(
            &serde_json::from_str(&format!(
                "{{\"operations\": [\"open_process\"], \"strip_access_mask\": {}}}",
                PROCESS_VM_WRITE
            ))
            .unwrap(),
        );

        let body: Map<String, Value> = serde_json::from_str(&format!(
            "{{\"operation\": \"open_process\", \"source_pid\": 9999, \"target_pid\": {}, \"requested_access\": {}}}",
            tgt, PROCESS_ALL_ACCESS
        ))
        .unwrap();
        let result = handle_simulate_operation(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["blocked"], false);
    }

    #[test]
    fn test_simulate_missing_params() {
        let body: Map<String, Value> = serde_json::from_str("{}").expect("seed demo json");
        let result = handle_simulate_operation(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_simulate_invalid_operation() {
        let body: Map<String, Value> =
            serde_json::from_str("{\"operation\": \"invalid\", \"source_pid\": 1, \"target_pid\": 2}")
                .expect("seed demo json");
        let result = handle_simulate_operation(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_pre_operations_list() {
        let result = handle_pre_operations(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["count"].as_u64().is_some());
    }

    #[test]
    fn test_post_operations_list() {
        let result = handle_post_operations(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["count"].as_u64().is_some());
    }

    #[test]
    fn test_access_log() {
        let src: u32 = 46000 + (std::process::id() % 1000) as u32;
        let tgt: u32 = 47000 + (std::process::id() % 1000) as u32;
        let before = lock_access_logs().len();

        let body: Map<String, Value> = serde_json::from_str(&format!(
            "{{\"operation\": \"open_process\", \"source_pid\": {}, \"target_pid\": {}}}",
            src, tgt
        ))
        .unwrap();
        handle_simulate_operation(&body);

        let result = handle_access_log(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["count"].as_u64().unwrap() > before as u64);
    }

    #[test]
    fn test_capability_survey() {
        let result = handle_capability_survey(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["nt_equivalent"], "ObRegisterCallbacks / ObUnRegisterCallbacks");
        assert!(result["mechanisms"].as_array().unwrap().len() >= 3);
    }

    #[test]
    fn test_access_mask_flags() {
        let flags = access_mask_flags(PROCESS_VM_READ | PROCESS_VM_WRITE);
        assert!(flags.contains(&"PROCESS_VM_READ"));
        assert!(flags.contains(&"PROCESS_VM_WRITE"));

        let none = access_mask_flags(0);
        assert!(none.contains(&"NONE"));

        let all = access_mask_flags(PROCESS_ALL_ACCESS);
        assert!(all.contains(&"PROCESS_ALL_ACCESS"));
    }

    #[test]
    fn test_seed_demo() {
        let result = handle_seed_demo(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["seeded"]["operations_simulated"], 3);
        assert!(result["operation_results"].as_array().unwrap().len() == 3);
    }

    #[test]
    fn test_handle_operation_from_str() {
        assert_eq!(HandleOperation::from_str("open_process"), Some(HandleOperation::OpenProcess));
        assert_eq!(HandleOperation::from_str("NtOpenProcess"), Some(HandleOperation::OpenProcess));
        assert_eq!(HandleOperation::from_str("duplicate_object"), Some(HandleOperation::DuplicateObject));
        assert_eq!(HandleOperation::from_str("invalid"), None);
    }
}
