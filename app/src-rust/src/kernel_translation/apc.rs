use serde_json::{json, Map, Value};
use std::collections::BTreeMap;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum ApcMode {
    User,
    Kernel,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ApcEntry {
    pub id: u64,
    pub thread_handle: u64,
    pub target_thread_id: u64,
    pub apc_routine: String,
    pub apc_context: String,
    pub arg1: String,
    pub arg2: String,
    pub arg3: String,
    pub mode: ApcMode,
    pub status: ApcStatus,
    pub enqueued_at: u64,
    pub delivered_at: Option<u64>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum ApcStatus {
    Pending,
    Delivering,
    Delivered,
    Cancelled,
    Failed,
}

static NEXT_APC_ID: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);

static APC_QUEUES: std::sync::LazyLock<std::sync::Mutex<BTreeMap<u64, Vec<ApcEntry>>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(BTreeMap::new()));

static TRAMPOLINE_STATE: std::sync::LazyLock<std::sync::Mutex<TrampolineState>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(TrampolineState::new()));

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct TrampolineState {
    pub allocated: bool,
    pub page_address: String,
    pub page_size: usize,
    pub trampoline_offset: usize,
    pub restore_handler_offset: usize,
    pub code_size: usize,
    pub status: String,
    pub detail: String,
}

impl TrampolineState {
    fn new() -> Self {
        Self {
            allocated: false,
            page_address: "0x0000000000000000".to_string(),
            page_size: 0,
            trampoline_offset: 0,
            restore_handler_offset: 0,
            code_size: 0,
            status: "not_allocated".to_string(),
            detail: "Trampoline page not yet allocated".to_string(),
        }
    }
}

#[derive(Debug, Clone, Copy)]
struct Arm64Context {
    x: [u64; 29],
    fp: u64,
    lr: u64,
    sp: u64,
    pc: u64,
    cpsr: u32,
}

impl Arm64Context {
    fn zeroed() -> Self {
        Self { x: [0u64; 29], fp: 0, lr: 0, sp: 0, pc: 0, cpsr: 0 }
    }
}

impl serde::Serialize for Arm64Context {
    fn serialize<S: serde::Serializer>(&self, s: S) -> Result<S::Ok, S::Error> {
        use serde::ser::SerializeStruct;
        let mut state = s.serialize_struct("Arm64Context", 6)?;
        state.serialize_field("x0", &format!("0x{:016X}", self.x[0]))?;
        state.serialize_field("x1", &format!("0x{:016X}", self.x[1]))?;
        state.serialize_field("sp", &format!("0x{:016X}", self.sp))?;
        state.serialize_field("pc", &format!("0x{:016X}", self.pc))?;
        state.serialize_field("lr", &format!("0x{:016X}", self.lr))?;
        state.serialize_field("cpsr", &format!("0x{:08X}", self.cpsr))?;
        state.end()
    }
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct SavedContext {
    pub thread_id: u64,
    pub context: Value,
    pub saved_at: u64,
    pub reason: String,
}

static SAVED_CONTEXTS: std::sync::LazyLock<std::sync::Mutex<BTreeMap<u64, SavedContext>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(BTreeMap::new()));

pub fn handle_queue_apc(body: &Map<String, Value>) -> Value {
    let thread_handle = match body.get("thread_handle").and_then(|v| v.as_u64()) {
        Some(h) => h,
        None => return json!({"ok": false, "error": "thread_handle (u64) required"}),
    };
    let target_tid = body.get("target_thread_id").and_then(|v| v.as_u64()).unwrap_or(thread_handle);
    let apc_routine = body.get("apc_routine").and_then(|v| v.as_str()).unwrap_or("0x0000000000000000").to_string();
    let apc_context = body.get("apc_context").and_then(|v| v.as_str()).unwrap_or("0x0000000000000000").to_string();
    let arg1 = body.get("arg1").and_then(|v| v.as_str()).unwrap_or("0x0000000000000000").to_string();
    let arg2 = body.get("arg2").and_then(|v| v.as_str()).unwrap_or("0x0000000000000000").to_string();
    let arg3 = body.get("arg3").and_then(|v| v.as_str()).unwrap_or("0x0000000000000000").to_string();
    let mode = if body.get("kernel_mode").and_then(|v| v.as_bool()).unwrap_or(false) {
        ApcMode::Kernel
    } else {
        ApcMode::User
    };

    let apc_id = NEXT_APC_ID.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    let entry = ApcEntry {
        id: apc_id,
        thread_handle,
        target_thread_id: target_tid,
        apc_routine,
        apc_context,
        arg1,
        arg2,
        arg3,
        mode,
        status: ApcStatus::Pending,
        enqueued_at: now_millis(),
        delivered_at: None,
    };

    let mut queues = APC_QUEUES.lock().unwrap();
    queues.entry(target_tid).or_default().push(entry.clone());

    json!({
        "ok": true,
        "ntApi": "NtQueueApcThread",
        "apcId": apc_id,
        "entry": entry,
        "queueDepth": queues.get(&target_tid).map(|q| q.len()).unwrap_or(0),
    })
}

pub fn handle_test_alert(body: &Map<String, Value>) -> Value {
    let thread_id = match body.get("thread_id").and_then(|v| v.as_u64()) {
        Some(t) => t,
        None => return json!({"ok": false, "error": "thread_id (u64) required"}),
    };

    let mut queues = APC_QUEUES.lock().unwrap();
    let queue = queues.entry(thread_id).or_default();

    let pending: Vec<&ApcEntry> = queue.iter().filter(|a| a.status == ApcStatus::Pending).collect();
    let deliver_count = pending.len();

    let now = now_millis();
    let mut delivered = Vec::new();
    for apc in queue.iter_mut() {
        if apc.status == ApcStatus::Pending {
            apc.status = ApcStatus::Delivered;
            apc.delivered_at = Some(now);
            delivered.push(apc.id);
        }
    }

    json!({
        "ok": true,
        "ntApi": "NtTestAlert",
        "threadId": thread_id,
        "pendingCount": deliver_count,
        "deliveredApcIds": delivered,
        "remainingInQueue": queue.iter().filter(|a| a.status == ApcStatus::Pending).count(),
        "note": "Delivered all pending user-mode APCs for this thread",
    })
}

pub fn handle_wait_alertable(body: &Map<String, Value>) -> Value {
    let thread_id = match body.get("thread_id").and_then(|v| v.as_u64()) {
        Some(t) => t,
        None => return json!({"ok": false, "error": "thread_id (u64) required"}),
    };
    let _handle = body.get("handle").and_then(|v| v.as_u64()).unwrap_or(0);

    let mut queues = APC_QUEUES.lock().unwrap();
    let queue = queues.entry(thread_id).or_default();

    let pending_count = queue.iter().filter(|a| a.status == ApcStatus::Pending).count();

    if pending_count == 0 {
        return json!({
            "ok": true,
            "ntApi": "NtWaitForSingleObject(alertable=TRUE)",
            "threadId": thread_id,
            "status": "STATUS_WAIT_0",
            "pendingApcs": 0,
            "action": "no APCs pending — normal wait return",
        });
    }

    let now = now_millis();
    let mut delivered_ids = Vec::new();
    for apc in queue.iter_mut() {
        if apc.status == ApcStatus::Pending {
            apc.status = ApcStatus::Delivered;
            apc.delivered_at = Some(now);
            delivered_ids.push(apc.id);
        }
    }

    json!({
        "ok": true,
        "ntApi": "NtWaitForSingleObject(alertable=TRUE)",
        "threadId": thread_id,
        "status": "STATUS_USER_APC",
        "pendingApcs": delivered_ids.len(),
        "deliveredApcIds": delivered_ids,
        "action": "APCs delivered before wait — return STATUS_USER_APC",
    })
}

pub fn handle_allocate_trampoline(body: &Map<String, Value>) -> Value {
    let page_size = body.get("page_size").and_then(|v| v.as_u64()).unwrap_or(4096) as usize;

    #[cfg(unix)]
    {
        unsafe {
            let ptr = libc::mmap(
                std::ptr::null_mut(),
                page_size,
                libc::PROT_READ | libc::PROT_WRITE | libc::PROT_EXEC,
                libc::MAP_PRIVATE | libc::MAP_ANON,
                -1,
                0,
            );

            if ptr == libc::MAP_FAILED {
                let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(-1);
                return json!({
                    "ok": false,
                    "error": format!("mmap RWX failed: errno {}", errno),
                    "note": "May be blocked by hardened runtime or PAC",
                });
            }

            let trampoline_code = build_trampoline_arm64(ptr as u64);
            std::ptr::copy_nonoverlapping(trampoline_code.as_ptr(), ptr as *mut u8, trampoline_code.len());

            let restore_offset = trampoline_code.len();
            let restore_code = build_restore_handler_arm64();
            std::ptr::copy_nonoverlapping(
                restore_code.as_ptr(),
                (ptr as *mut u8).add(restore_offset),
                restore_code.len(),
            );

            let total_code = trampoline_code.len() + restore_code.len();

            let mut state = TRAMPOLINE_STATE.lock().unwrap();
            state.allocated = true;
            state.page_address = format!("0x{:016X}", ptr as u64);
            state.page_size = page_size;
            state.trampoline_offset = 0;
            state.restore_handler_offset = restore_offset;
            state.code_size = total_code;
            state.status = "allocated".to_string();
            state.detail = format!(
                "Trampoline at +0x{:X}, restore handler at +0x{:X}, total {} bytes",
                0, restore_offset, total_code
            );

            json!({
                "ok": true,
                "pageAddress": state.page_address,
                "pageSize": page_size,
                "trampolineOffset": 0,
                "restoreHandlerOffset": restore_offset,
                "codeSize": total_code,
                "code": {
                    "trampoline": format!("{} bytes (save LR, load args, BLR to apc_routine, B to restore)", trampoline_code.len()),
                    "restoreHandler": format!("{} bytes (load saved context, MSR SP_el0, ERET)", restore_code.len()),
                },
                "trampolineAssembly": trampoline_asm(),
                "restoreAssembly": restore_asm(),
            })
        }
    }

    #[cfg(not(unix))]
    {
        let _ = page_size;
        json!({"ok": false, "error": "trampoline allocation requires macOS"})
    }
}

pub fn handle_suspend_thread(body: &Map<String, Value>) -> Value {
    #[cfg(unix)]
    {
        let thread_port = match body.get("thread_port").and_then(|v| v.as_u64()) {
            Some(t) => t as u32,
            None => return json!({"ok": false, "error": "thread_port (u32 Mach thread port) required"}),
        };

        let mut kr: i32 = 0;
        let result = unsafe { libc::syscall(0x20000000 + 340, thread_port, &mut kr) };

        json!({
            "ok": result >= 0,
            "ntApi": "NtSuspendThread",
            "threadPort": format!("0x{:08X}", thread_port),
            "kr": kr,
            "syscallResult": result,
            "note": if result >= 0 { "Thread suspended via thread_suspend" } else { "thread_suspend failed — may need task port" },
        })
    }

    #[cfg(not(unix))]
    {
        let _ = body;
        json!({"ok": false, "error": "thread_suspend requires macOS"})
    }
}

pub fn handle_get_thread_context(body: &Map<String, Value>) -> Value {
    let thread_id = match body.get("thread_id").and_then(|v| v.as_u64()) {
        Some(t) => t,
        None => return json!({"ok": false, "error": "thread_id required"}),
    };

    let saved = SAVED_CONTEXTS.lock().unwrap();
    if let Some(ctx) = saved.get(&thread_id) {
        return json!({
            "ok": true,
            "source": "saved",
            "context": ctx,
        });
    }

    let ctx = Arm64Context::zeroed();
    json!({
        "ok": true,
        "source": "simulated_zeroed",
        "threadId": thread_id,
        "context": ctx,
        "note": "Zeroed context — in production, thread_get_state(ARM_THREAD_STATE64) provides real registers",
    })
}

pub fn handle_set_thread_context(body: &Map<String, Value>) -> Value {
    let thread_id = match body.get("thread_id").and_then(|v| v.as_u64()) {
        Some(t) => t,
        None => return json!({"ok": false, "error": "thread_id required"}),
    };
    let pc = body.get("pc").and_then(|v| v.as_str()).unwrap_or("0x0000000000000000");
    let sp = body.get("sp").and_then(|v| v.as_str()).unwrap_or("0x0000000000000000");
    let x0 = body.get("x0").and_then(|v| v.as_str()).unwrap_or("0x0000000000000000");
    let x1 = body.get("x1").and_then(|v| v.as_str()).unwrap_or("0x0000000000000000");

    let pc_val = u64::from_str_radix(pc.trim_start_matches("0x"), 16).unwrap_or(0);
    let sp_val = u64::from_str_radix(sp.trim_start_matches("0x"), 16).unwrap_or(0);
    let x0_val = u64::from_str_radix(x0.trim_start_matches("0x"), 16).unwrap_or(0);

    let saved = SavedContext {
        thread_id,
        context: json!({
            "pc": format!("0x{:016X}", pc_val),
            "sp": format!("0x{:016X}", sp_val),
            "x0": format!("0x{:016X}", x0_val),
            "x1": x1.to_string(),
        }),
        saved_at: now_millis(),
        reason: "APC injection — saved for restore after delivery".to_string(),
    };

    SAVED_CONTEXTS.lock().unwrap().insert(thread_id, saved.clone());

    json!({
        "ok": true,
        "ntApi": "NtSetContextThread",
        "threadId": thread_id,
        "newContext": saved.context,
        "note": "Context set — in production, thread_set_state(ARM_THREAD_STATE64) writes to real thread",
    })
}

pub fn handle_inject_apc_sequence(body: &Map<String, Value>) -> Value {
    let thread_id = match body.get("thread_id").and_then(|v| v.as_u64()) {
        Some(t) => t,
        None => return json!({"ok": false, "error": "thread_id required"}),
    };
    let apc_routine = body.get("apc_routine").and_then(|v| v.as_str()).unwrap_or("0x0000000000000000").to_string();
    let trampoline_addr =
        body.get("trampoline_address").and_then(|v| v.as_str()).unwrap_or("0x0000000000000000").to_string();

    let mut steps = Vec::new();

    steps.push(json!({
        "step": 1,
        "action": "suspend",
        "ntApi": "NtSuspendThread",
        "machApi": "thread_suspend(thread_port)",
        "detail": "Suspend target thread to safely modify its context",
    }));

    steps.push(json!({
        "step": 2,
        "action": "save_context",
        "ntApi": "NtGetContextThread",
        "machApi": "thread_get_state(ARM_THREAD_STATE64)",
        "detail": "Save full ARM64 context (x0-x28, fp, lr, sp, pc, cpsr) for restoration",
        "fields": ["x[0..29]", "fp", "lr", "sp", "pc", "cpsr"],
    }));

    steps.push(json!({
        "step": 3,
        "action": "modify_context",
        "ntApi": "NtSetContextThread",
        "machApi": "thread_set_state(ARM_THREAD_STATE64)",
        "detail": "Set PC to trampoline, x0=ApcContext, x1=ApcRoutine, x2=arg1, x3=arg2, x4=arg3, LR=restore_handler",
        "registers": {
            "PC": trampoline_addr,
            "X0": "ApcContext",
            "X1": apc_routine,
            "X2": "arg1",
            "X3": "arg2",
            "X4": "arg3",
            "LR": "restore_handler_address",
        },
    }));

    steps.push(json!({
        "step": 4,
        "action": "resume",
        "ntApi": "NtResumeThread",
        "machApi": "thread_resume(thread_port)",
        "detail": "Resume thread — it begins executing at trampoline entry point",
    }));

    steps.push(json!({
        "step": 5,
        "action": "trampoline_executes",
        "detail": "Trampoline: save remaining regs → BLR x1 (call ApcRoutine) → B restore_handler",
        "execution": format!("trampoline@{} → ApcRoutine({}) → restore_handler", trampoline_addr, apc_routine),
    }));

    steps.push(json!({
        "step": 6,
        "action": "restore_context",
        "detail": "Restore handler: load saved context from step 2 → thread_set_state → original PC resumes",
        "machApi": "thread_set_state(ARM_THREAD_STATE64) with saved values",
    }));

    json!({
        "ok": true,
        "ntApi": "NtQueueApcThread (full injection sequence)",
        "threadId": thread_id,
        "steps": steps,
        "totalSteps": steps.len(),
        "sequence": "suspend → save → modify → resume → execute → restore",
    })
}

pub fn handle_apc_queue_status(body: &Map<String, Value>) -> Value {
    let queues = APC_QUEUES.lock().unwrap();

    let filter_tid = body.get("thread_id").and_then(|v| v.as_u64());
    let filter_status = body.get("status").and_then(|v| v.as_str());

    match filter_tid {
        Some(tid) => match queues.get(&tid) {
            Some(queue) => {
                let filtered: Vec<&ApcEntry> = queue
                    .iter()
                    .filter(|a| {
                        filter_status.is_none_or(|s| match s {
                            "pending" => a.status == ApcStatus::Pending,
                            "delivered" => a.status == ApcStatus::Delivered,
                            "cancelled" => a.status == ApcStatus::Cancelled,
                            _ => true,
                        })
                    })
                    .collect();
                json!({
                    "ok": true,
                    "threadId": tid,
                    "totalInQueue": queue.len(),
                    "filteredCount": filtered.len(),
                    "entries": filtered,
                })
            },
            None => json!({"ok": true, "threadId": tid, "totalInQueue": 0, "entries": []}),
        },
        None => {
            let summary: Vec<Value> = queues
                .iter()
                .map(|(tid, queue)| {
                    let pending = queue.iter().filter(|a| a.status == ApcStatus::Pending).count();
                    let delivered = queue.iter().filter(|a| a.status == ApcStatus::Delivered).count();
                    json!({
                        "threadId": tid,
                        "total": queue.len(),
                        "pending": pending,
                        "delivered": delivered,
                    })
                })
                .collect();
            json!({
                "ok": true,
                "totalThreads": queues.len(),
                "threads": summary,
            })
        },
    }
}

pub fn handle_trampoline_status(_body: &Map<String, Value>) -> Value {
    let state = TRAMPOLINE_STATE.lock().unwrap();
    json!({
        "ok": true,
        "trampoline": state.clone(),
    })
}

fn build_trampoline_arm64(_base_addr: u64) -> Vec<u8> {
    let mut code = Vec::new();

    code.extend_from_slice(&0xA9BF7BFDu32.to_le_bytes());
    code.extend_from_slice(&0xA9BF53F3u32.to_le_bytes());
    code.extend_from_slice(&0xA9BF4BF5u32.to_le_bytes());
    code.extend_from_slice(&0xAA1503E0u32.to_le_bytes());
    code.extend_from_slice(&0xD63F0060u32.to_le_bytes());
    code.extend_from_slice(&0x14000003u32.to_le_bytes());
    code.extend_from_slice(&0xA8C14BF5u32.to_le_bytes());
    code.extend_from_slice(&0xA8C153F3u32.to_le_bytes());
    code.extend_from_slice(&0xA8C17BFDu32.to_le_bytes());

    code
}

fn build_restore_handler_arm64() -> Vec<u8> {
    let mut code = Vec::new();

    code.extend_from_slice(&0x58000060u32.to_le_bytes());
    code.extend_from_slice(&0xD51B0020u32.to_le_bytes());
    code.extend_from_slice(&0x58000040u32.to_le_bytes());
    code.extend_from_slice(&0xD51B0040u32.to_le_bytes());
    code.extend_from_slice(&0xD69F03E0u32.to_le_bytes());

    code.extend_from_slice(&0x00000000u32.to_le_bytes());
    code.extend_from_slice(&0x00000000u32.to_le_bytes());
    code.extend_from_slice(&0x00000000u32.to_le_bytes());
    code.extend_from_slice(&0x00000000u32.to_le_bytes());

    code
}

fn trampoline_asm() -> Value {
    json!([
        "stp x29, x30, [sp, #-16]!   // save frame pointer and link register",
        "stp x19, x20, [sp, #-16]!   // save callee-saved registers",
        "stp x21, x22, [sp, #-16]!   // save more callee-saved",
        "mov x0, x21                  // x0 = ApcContext (passed in x21)",
        "blr x1                       // call ApcRoutine(x0=ApcContext)",
        "b restore_handler            // jump to restore handler",
        "// --- restore handler ---",
        "ldp x21, x22, [sp], #16     // restore callee-saved",
        "ldp x19, x20, [sp], #16     // restore callee-saved",
        "ldp x29, x30, [sp], #16     // restore fp and lr",
    ])
}

fn restore_asm() -> Value {
    json!([
        "ldr x0, [pc, #12]           // load saved_sp from literal pool",
        "msr sp_el0, x0              // restore user stack pointer",
        "ldr x0, [pc, #8]            // load saved_pc from literal pool",
        "msr elr_el1, x0             // restore return address",
        "eret                         // return to original code",
        ".quad saved_sp              // embedded saved stack pointer",
        ".quad saved_pc              // embedded saved program counter",
    ])
}

fn now_millis() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_millis() as u64
}

#[cfg(test)]
mod tests {
    use super::*;

    fn lock_queues() -> std::sync::MutexGuard<'static, BTreeMap<u64, Vec<ApcEntry>>> {
        match APC_QUEUES.lock() {
            Ok(g) => g,
            Err(poisoned) => poisoned.into_inner(),
        }
    }

    fn lock_contexts() -> std::sync::MutexGuard<'static, BTreeMap<u64, SavedContext>> {
        match SAVED_CONTEXTS.lock() {
            Ok(g) => g,
            Err(poisoned) => poisoned.into_inner(),
        }
    }

    #[test]
    fn test_queue_apc_creates_entry() {
        let tid: u64 = 100100;
        lock_queues().remove(&tid);
        let body: Map<String, Value> = serde_json::from_str(
            &format!(r#"{{"thread_handle": 256, "target_thread_id": {}, "apc_routine": "0xDEADBEEF", "apc_context": "0x12345678"}}"#, tid),
        )
        .unwrap();
        let result = handle_queue_apc(&body);
        assert_eq!(result["ok"], true);
        assert_eq!(result["queueDepth"], 1);

        let queues = lock_queues();
        let q = queues.get(&tid).unwrap();
        assert_eq!(q.len(), 1);
        assert_eq!(q[0].apc_routine, "0xDEADBEEF");
        assert_eq!(q[0].status, ApcStatus::Pending);
    }

    #[test]
    fn test_queue_multiple_apcs() {
        let tid: u64 = 100200;
        lock_queues().remove(&tid);
        let body1: Map<String, Value> =
            serde_json::from_str(&format!(r#"{{"thread_handle": 256, "target_thread_id": {}}}"#, tid)).unwrap();
        let body2: Map<String, Value> =
            serde_json::from_str(&format!(r#"{{"thread_handle": 256, "target_thread_id": {}}}"#, tid)).unwrap();
        handle_queue_apc(&body1);
        handle_queue_apc(&body2);

        let queues = lock_queues();
        assert_eq!(queues.get(&tid).unwrap().len(), 2);
    }

    #[test]
    fn test_test_alert_delivers_pending() {
        let tid: u64 = 100300;
        lock_queues().remove(&tid);
        let body: Map<String, Value> = serde_json::from_str(&format!(
            r#"{{"thread_handle": 256, "target_thread_id": {}, "apc_routine": "0xAAAA"}}"#,
            tid
        ))
        .unwrap();
        handle_queue_apc(&body);

        let alert_body: Map<String, Value> = serde_json::from_str(&format!(r#"{{"thread_id": {}}}"#, tid)).unwrap();
        let result = handle_test_alert(&alert_body);
        assert_eq!(result["ok"], true);
        assert_eq!(result["pendingCount"], 1);

        let queues = lock_queues();
        let q = queues.get(&tid).unwrap();
        assert_eq!(q[0].status, ApcStatus::Delivered);
        assert!(q[0].delivered_at.is_some());
    }

    #[test]
    fn test_wait_alertable_delivers_pending() {
        let tid: u64 = 100400;
        lock_queues().remove(&tid);
        let body: Map<String, Value> =
            serde_json::from_str(&format!(r#"{{"thread_handle": 256, "target_thread_id": {}}}"#, tid)).unwrap();
        handle_queue_apc(&body);

        let wait_body: Map<String, Value> =
            serde_json::from_str(&format!(r#"{{"thread_id": {}, "handle": 512}}"#, tid)).unwrap();
        let result = handle_wait_alertable(&wait_body);
        assert_eq!(result["status"], "STATUS_USER_APC");
        assert_eq!(result["pendingApcs"], 1);
    }

    #[test]
    fn test_wait_alertable_no_pending_returns_wait() {
        let tid: u64 = 100500;
        lock_queues().remove(&tid);
        let wait_body: Map<String, Value> =
            serde_json::from_str(&format!(r#"{{"thread_id": {}, "handle": 512}}"#, tid)).unwrap();
        let result = handle_wait_alertable(&wait_body);
        assert_eq!(result["status"], "STATUS_WAIT_0");
        assert_eq!(result["pendingApcs"], 0);
    }

    #[test]
    fn test_inject_apc_sequence_steps() {
        let body: Map<String, Value> =
            serde_json::from_str(r#"{"thread_id": 600, "apc_routine": "0xDEAD", "trampoline_address": "0xBEEF"}"#)
                .unwrap();
        let result = handle_inject_apc_sequence(&body);
        assert_eq!(result["ok"], true);
        let steps = result["steps"].as_array().unwrap();
        assert_eq!(steps.len(), 6);
        assert_eq!(steps[0]["step"], 1);
        assert_eq!(steps[5]["step"], 6);
    }

    #[test]
    fn test_set_and_get_thread_context() {
        let tid: u64 = 100700;
        lock_contexts().remove(&tid);
        let set_body: Map<String, Value> = serde_json::from_str(&format!(
            r#"{{"thread_id": {}, "pc": "0xDEADBEEF", "sp": "0x12340000", "x0": "0xAAAAAAAA", "x1": "0xBBBBBBBB"}}"#,
            tid
        ))
        .unwrap();
        let set_result = handle_set_thread_context(&set_body);
        assert_eq!(set_result["ok"], true);

        let get_body: Map<String, Value> = serde_json::from_str(&format!(r#"{{"thread_id": {}}}"#, tid)).unwrap();
        let get_result = handle_get_thread_context(&get_body);
        assert_eq!(get_result["ok"], true);
        assert_eq!(get_result["source"], "saved");
    }

    #[test]
    fn test_trampoline_arm64_code_not_empty() {
        let code = build_trampoline_arm64(0x1000);
        assert!(!code.is_empty());
        assert_eq!(code.len() % 4, 0, "ARM64 instructions should be 4 bytes each");

        let restore = build_restore_handler_arm64();
        assert!(!restore.is_empty());
        assert_eq!(restore.len() % 4, 0);
    }

    #[test]
    fn test_apc_queue_status_empty() {
        let tid: u64 = 100999;
        lock_queues().remove(&tid);
        let body: Map<String, Value> = serde_json::from_str(&format!(r#"{{"thread_id": {}}}"#, tid)).unwrap();
        let result = handle_apc_queue_status(&body);
        assert_eq!(result["totalInQueue"], 0);
    }
}
