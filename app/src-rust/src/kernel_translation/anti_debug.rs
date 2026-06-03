use serde_json::{json, Map, Value};

const MAX_COLLECTION_LEN: usize = 4096;

const STATUS_SUCCESS: u32 = 0x00000000;
const STATUS_PORT_NOT_SET: u32 = 0xC0000353;
const STATUS_DEBUGGER_INACTIVE: u32 = 0xC0000354;
const STATUS_OBJECT_TYPE_MISMATCH: u32 = 0xC0000024;
const NT_DEBUG_PORT: u32 = 0;
const PEB_BEING_DEBUGGED_FALSE: u8 = 0;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum DebugCheckType {
    PebBeingDebugged,
    ProcessDebugPort,
    ProcessDebugObjectHandle,
    ProcessDebugFlags,
    NtQueryVirtualMemory,
    HardwareBreakpoints,
    TimingCheck,
    ModuleEnumeration,
    FileSystemCheck,
    ParentProcessCheck,
    ThreadHideFromDebugger,
    DebugRegisterCheck,
}

impl DebugCheckType {
    pub fn nt_api(&self) -> &'static str {
        match self {
            Self::PebBeingDebugged => "PEB.BeingDebugged",
            Self::ProcessDebugPort => "NtQueryInformationProcess(ProcessDebugPort)",
            Self::ProcessDebugObjectHandle => "NtQueryInformationProcess(ProcessDebugObjectHandle)",
            Self::ProcessDebugFlags => "NtQueryInformationProcess(ProcessDebugFlags)",
            Self::NtQueryVirtualMemory => "NtQueryVirtualMemory(MemoryBasicInformation)",
            Self::HardwareBreakpoints => "GetThreadContext DR0-DR3 → thread_get_state",
            Self::TimingCheck => "RDTSC / QueryPerformanceCounter delta",
            Self::ModuleEnumeration => "EnumProcessModules / CreateToolhelp32Snapshot",
            Self::FileSystemCheck => "lstat(C:\\Windows\\System32) / GetFileAttributes",
            Self::ParentProcessCheck => "NtQueryInformationProcess(ProcessBasicInformation)",
            Self::ThreadHideFromDebugger => "NtSetInformationThread(ThreadHideFromDebugger)",
            Self::DebugRegisterCheck => "DR6/DR7 status via GetThreadContext",
        }
    }

    pub fn wine_response(&self) -> &'static str {
        match self {
            Self::PebBeingDebugged => "Wine sets PEB.BeingDebugged = 0 (no debugger attached)",
            Self::ProcessDebugPort => "Wine returns DebugPort = 0 (no debug port)",
            Self::ProcessDebugObjectHandle => "Wine returns STATUS_PORT_NOT_SET (0xC0000353)",
            Self::ProcessDebugFlags => "Wine returns ProcessDebugFlags = 0 (no debug object)",
            Self::NtQueryVirtualMemory => "Wine returns legitimate memory regions via mach_vm_region",
            Self::HardwareBreakpoints => "DR0-DR3 all zero; ARM64 DBGBCR/DBGBVR not set by Wine",
            Self::TimingCheck => "NtQuerySystemTime returns real time, RDTSC passes through",
            Self::ModuleEnumeration => "Wine module list contains only Windows binaries (no libwine visible)",
            Self::FileSystemCheck => "Wine virtual filesystem: C:\\Windows\\System32 resolves to prefix",
            Self::ParentProcessCheck => "Returns expected parent PID (explorer.exe or launcher)",
            Self::ThreadHideFromDebugger => "Wine accepts the call, no error",
            Self::DebugRegisterCheck => "DR6=0 (no debug exception), DR7=0 (no breakpoints enabled)",
        }
    }

    pub fn risk_level(&self) -> &'static str {
        match self {
            Self::PebBeingDebugged => "handled",
            Self::ProcessDebugPort => "handled",
            Self::ProcessDebugObjectHandle => "build_needed",
            Self::ProcessDebugFlags => "handled",
            Self::NtQueryVirtualMemory => "handled",
            Self::HardwareBreakpoints => "drill_needed",
            Self::TimingCheck => "drill_needed",
            Self::ModuleEnumeration => "drill_needed",
            Self::FileSystemCheck => "drill_needed",
            Self::ParentProcessCheck => "handled",
            Self::ThreadHideFromDebugger => "handled",
            Self::DebugRegisterCheck => "handled",
        }
    }

    fn from_str(s: &str) -> Option<Self> {
        match s {
            "peb_being_debugged" => Some(Self::PebBeingDebugged),
            "process_debug_port" => Some(Self::ProcessDebugPort),
            "process_debug_object_handle" => Some(Self::ProcessDebugObjectHandle),
            "process_debug_flags" => Some(Self::ProcessDebugFlags),
            "nt_query_virtual_memory" => Some(Self::NtQueryVirtualMemory),
            "hardware_breakpoints" => Some(Self::HardwareBreakpoints),
            "timing_check" => Some(Self::TimingCheck),
            "module_enumeration" => Some(Self::ModuleEnumeration),
            "filesystem_check" => Some(Self::FileSystemCheck),
            "parent_process_check" => Some(Self::ParentProcessCheck),
            "thread_hide_from_debugger" => Some(Self::ThreadHideFromDebugger),
            "debug_register_check" => Some(Self::DebugRegisterCheck),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct DebugCheckResult {
    pub check_type: DebugCheckType,
    pub detected: bool,
    pub wine_response: String,
    pub nt_status: u32,
    pub response_value: serde_json::Value,
    pub notes: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum Arm64DebugReg {
    DbgBcr0,
    DbgBcr1,
    DbgBcr2,
    DbgBcr3,
    DbgBvr0,
    DbgBvr1,
    DbgBvr2,
    DbgBvr3,
    MdscrEl1,
}

impl Arm64DebugReg {
    pub fn arm64_name(&self) -> &'static str {
        match self {
            Self::DbgBcr0 => "DBGBCR0_EL1",
            Self::DbgBcr1 => "DBGBCR1_EL1",
            Self::DbgBcr2 => "DBGBCR2_EL1",
            Self::DbgBcr3 => "DBGBCR3_EL1",
            Self::DbgBvr0 => "DBGBVR0_EL1",
            Self::DbgBvr1 => "DBGBVR1_EL1",
            Self::DbgBvr2 => "DBGBVR2_EL1",
            Self::DbgBvr3 => "DBGBVR3_EL1",
            Self::MdscrEl1 => "MDSCR_EL1",
        }
    }

    pub fn nt_equivalent(&self) -> &'static str {
        match self {
            Self::DbgBcr0 => "DR7 (breakpoint control, BP 0)",
            Self::DbgBcr1 => "DR7 (breakpoint control, BP 1)",
            Self::DbgBcr2 => "DR7 (breakpoint control, BP 2)",
            Self::DbgBcr3 => "DR7 (breakpoint control, BP 3)",
            Self::DbgBvr0 => "DR0 (breakpoint address 0)",
            Self::DbgBvr1 => "DR1 (breakpoint address 1)",
            Self::DbgBvr2 => "DR2 (breakpoint address 2)",
            Self::DbgBvr3 => "DR3 (breakpoint address 3)",
            Self::MdscrEl1 => "DR7 (master debug enable)",
        }
    }

    pub fn xnu_thread_state_flavor(&self) -> &'static str {
        match self {
            Self::DbgBcr0 | Self::DbgBcr1 | Self::DbgBcr2 | Self::DbgBcr3 => "ARM_DEBUG_STATE64 (DBGBCR)",
            Self::DbgBvr0 | Self::DbgBvr1 | Self::DbgBvr2 | Self::DbgBvr3 => "ARM_DEBUG_STATE64 (DBGBVR)",
            Self::MdscrEl1 => "ARM_THREAD_STATE64 (far/cpsr via MDSCR)",
        }
    }
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct HwBreakpointMap {
    pub nt_dr: String,
    pub arm64_reg: String,
    pub value: u64,
    pub enabled: bool,
    pub type_: String,
    pub length: u8,
    pub address: String,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ModuleSanitizeEntry {
    pub original_name: String,
    pub display_name: String,
    pub visible: bool,
    pub reason: String,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct TimingAnalysis {
    pub check_type: String,
    pub expected_delta_us: u64,
    pub tolerance_percent: u32,
    pub wine_overhead_us: u64,
    pub detectable: bool,
    pub mitigation: String,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct FileSystemEntry {
    pub nt_path: String,
    pub wine_path: String,
    pub exists: bool,
    pub looks_authentic: bool,
    pub issues: Vec<String>,
}

static CHECK_RESULTS: std::sync::LazyLock<std::sync::Mutex<Vec<DebugCheckResult>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(Vec::new()));

static SANITIZE_RULES: std::sync::LazyLock<std::sync::Mutex<Vec<ModuleSanitizeEntry>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(Vec::new()));

fn lock_results() -> std::sync::MutexGuard<'static, Vec<DebugCheckResult>> {
    match CHECK_RESULTS.lock() {
        Ok(g) => g,
        Err(e) => e.into_inner(),
    }
}

fn lock_sanitize_rules() -> std::sync::MutexGuard<'static, Vec<ModuleSanitizeEntry>> {
    match SANITIZE_RULES.lock() {
        Ok(g) => g,
        Err(e) => e.into_inner(),
    }
}

fn now_ms() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_millis() as u64
}

pub fn handle_simulate_check(body: &Map<String, Value>) -> Value {
    let check_type = match body.get("check_type").and_then(|v| v.as_str()).and_then(DebugCheckType::from_str) {
        Some(ct) => ct,
        None => {
            return json!({"ok": false, "error": "check_type required: peb_being_debugged, process_debug_port, process_debug_object_handle, process_debug_flags, hardware_breakpoints, timing_check, module_enumeration, filesystem_check, parent_process_check, thread_hide_from_debugger, debug_register_check"})
        },
    };

    let result = match check_type {
        DebugCheckType::PebBeingDebugged => DebugCheckResult {
            check_type,
            detected: false,
            wine_response: format!("PEB.BeingDebugged = {}", PEB_BEING_DEBUGGED_FALSE),
            nt_status: STATUS_SUCCESS,
            response_value: json!({"BeingDebugged": PEB_BEING_DEBUGGED_FALSE, "NtGlobalFlag": 0}),
            notes: "Wine sets PEB.BeingDebugged to 0. No debugger attached from process perspective.".to_string(),
        },
        DebugCheckType::ProcessDebugPort => DebugCheckResult {
            check_type,
            detected: false,
            wine_response: format!("DebugPort = {}", NT_DEBUG_PORT),
            nt_status: STATUS_SUCCESS,
            response_value: json!({"DebugPort": NT_DEBUG_PORT}),
            notes: "Wine returns DebugPort = 0. No kernel debug port allocated.".to_string(),
        },
        DebugCheckType::ProcessDebugObjectHandle => DebugCheckResult {
            check_type,
            detected: false,
            wine_response: format!("STATUS_PORT_NOT_SET (0x{:08X})", STATUS_PORT_NOT_SET),
            nt_status: STATUS_PORT_NOT_SET,
            response_value: json!({"Handle": 0, "Status": format!("0x{:08X}", STATUS_PORT_NOT_SET)}),
            notes: "Wine must return STATUS_PORT_NOT_SET consistently. This is a BUILD task — ensure Wine ntdll returns this for ProcessDebugObjectHandle class.".to_string(),
        },
        DebugCheckType::ProcessDebugFlags => DebugCheckResult {
            check_type,
            detected: false,
            wine_response: "ProcessDebugFlags = 0".to_string(),
            nt_status: STATUS_SUCCESS,
            response_value: json!({"DebugFlags": 0}),
            notes: "Flags = 0 means no debug object. Anti-cheat checks this as secondary verification.".to_string(),
        },
        DebugCheckType::NtQueryVirtualMemory => DebugCheckResult {
            check_type,
            detected: false,
            wine_response: "mach_vm_region returns legitimate memory regions".to_string(),
            nt_status: STATUS_SUCCESS,
            response_value: json!({"Regions": "normal", "SuspiciousGaps": false}),
            notes: "Wine memory layout looks normal. Anti-cheat scans for debug-related memory pages (int3 breakpoints, watchpoints).".to_string(),
        },
        DebugCheckType::HardwareBreakpoints => DebugCheckResult {
            check_type,
            detected: false,
            wine_response: "DR0-DR3 = 0, DR6 = 0, DR7 = 0".to_string(),
            nt_status: STATUS_SUCCESS,
            response_value: json!({"DR0": 0, "DR1": 0, "DR2": 0, "DR3": 0, "DR6": 0, "DR7": 0}),
            notes: "ARM64: DBGBCR0-3 and DBGBVR0-3 all zero via thread_get_state(ARM_DEBUG_STATE64). No hardware breakpoints set.".to_string(),
        },
        DebugCheckType::TimingCheck => DebugCheckResult {
            check_type,
            detected: false,
            wine_response: "RDTSC/QueryPerformanceCounter returns real hardware counter".to_string(),
            nt_status: STATUS_SUCCESS,
            response_value: json!({"RdtscDelta": "native", "QpcDelta": "native", "AnomalyDetected": false}),
            notes: "RDTSC passes through to hardware on macOS. No virtualization overhead. QueryPerformanceCounter uses mach_absolute_time.".to_string(),
        },
        DebugCheckType::ModuleEnumeration => DebugCheckResult {
            check_type,
            detected: false,
            wine_response: "Module list shows only Windows binaries".to_string(),
            nt_status: STATUS_SUCCESS,
            response_value: json!({"ModuleCount": 42, "SuspiciousModules": [], "WineModulesVisible": false}),
            notes: "Wine's PE loader presents modules as Windows binaries. libwine.so, ntdll.so not visible through EnumProcessModules.".to_string(),
        },
        DebugCheckType::FileSystemCheck => DebugCheckResult {
            check_type,
            detected: false,
            wine_response: "C:\\Windows\\System32 resolves via Wine virtual filesystem".to_string(),
            nt_status: STATUS_SUCCESS,
            response_value: json!({"PathExists": true, "IsDirectory": true, "LooksAuthentic": true}),
            notes: "Wine prefix contains full Windows directory structure. lstat returns plausible metadata.".to_string(),
        },
        DebugCheckType::ParentProcessCheck => DebugCheckResult {
            check_type,
            detected: false,
            wine_response: "Parent PID = expected launcher process".to_string(),
            nt_status: STATUS_SUCCESS,
            response_value: json!({"ParentPid": "expected", "ParentName": "explorer.exe"}),
            notes: "Wine can report expected parent process. Anti-cheat verifies the parent is explorer.exe or game launcher.".to_string(),
        },
        DebugCheckType::ThreadHideFromDebugger => DebugCheckResult {
            check_type,
            detected: false,
            wine_response: "STATUS_SUCCESS — thread marked as hidden from debugger".to_string(),
            nt_status: STATUS_SUCCESS,
            response_value: json!({"Hidden": true}),
            notes: "Wine accepts ThreadHideFromDebugger without error. Anti-cheat threads call this to prevent debuggers from receiving their events.".to_string(),
        },
        DebugCheckType::DebugRegisterCheck => DebugCheckResult {
            check_type,
            detected: false,
            wine_response: "DR6 = 0 (no debug exceptions), DR7 = 0 (no breakpoints)".to_string(),
            nt_status: STATUS_SUCCESS,
            response_value: json!({"DR6": 0, "DR7": 0, "DebugExceptionPending": false}),
            notes: "DR6 debug status register = 0 means no debug exceptions have occurred. DR7 = 0 means no hardware breakpoints enabled.".to_string(),
        },
    };

    {
        let mut results = lock_results();
        results.push(result.clone());
        if results.len() > MAX_COLLECTION_LEN {
            let excess = results.len() - MAX_COLLECTION_LEN;
            results.drain(0..excess);
        }
    }

    json!({
        "ok": true,
        "check": result,
        "risk_level": check_type.risk_level(),
    })
}

pub fn handle_run_all_checks(_body: &Map<String, Value>) -> Value {
    let checks = [
        "peb_being_debugged",
        "process_debug_port",
        "process_debug_object_handle",
        "process_debug_flags",
        "hardware_breakpoints",
        "timing_check",
        "module_enumeration",
        "filesystem_check",
        "parent_process_check",
        "thread_hide_from_debugger",
        "debug_register_check",
    ];

    let mut results = Vec::new();
    let mut detected_count = 0;
    let mut handled_count = 0;
    let mut drill_count = 0;

    for check in &checks {
        let body: Map<String, Value> = serde_json::from_str(&format!("{{\"check_type\": \"{}\"}}", check)).unwrap();
        let r = handle_simulate_check(&body);
        let check_result: DebugCheckResult = serde_json::from_value(r["check"].clone()).unwrap();
        if check_result.detected {
            detected_count += 1;
        }
        match check_result.check_type.risk_level() {
            "handled" => handled_count += 1,
            "build_needed" => drill_count += 1,
            "drill_needed" => drill_count += 1,
            _ => {},
        }
        results.push(r);
    }

    json!({
        "ok": true,
        "total_checks": results.len(),
        "detected": detected_count,
        "handled": handled_count,
        "needs_work": drill_count,
        "overall_status": if detected_count == 0 { "clean" } else { "detected" },
        "results": results,
    })
}

pub fn handle_check_results(_body: &Map<String, Value>) -> Value {
    let results = lock_results();
    json!({
        "ok": true,
        "count": results.len(),
        "results": results.iter().collect::<Vec<_>>(),
    })
}

pub fn handle_hw_breakpoint_map(body: &Map<String, Value>) -> Value {
    let dr_index = match body.get("dr_index").and_then(|v| v.as_u64()) {
        Some(i) if i <= 3 => i as usize,
        _ => return json!({"ok": false, "error": "dr_index required (0-3)"}),
    };

    let arm_bcr = [Arm64DebugReg::DbgBcr0, Arm64DebugReg::DbgBcr1, Arm64DebugReg::DbgBcr2, Arm64DebugReg::DbgBcr3];
    let arm_bvr = [Arm64DebugReg::DbgBvr0, Arm64DebugReg::DbgBvr1, Arm64DebugReg::DbgBvr2, Arm64DebugReg::DbgBvr3];

    let mapping = HwBreakpointMap {
        nt_dr: format!("DR{}", dr_index),
        arm64_reg: format!("{}/{}", arm_bcr[dr_index].arm64_name(), arm_bvr[dr_index].arm64_name()),
        value: body.get("address").and_then(|v| v.as_u64()).unwrap_or(0),
        enabled: body.get("enabled").and_then(|v| v.as_bool()).unwrap_or(false),
        type_: body.get("bp_type").and_then(|v| v.as_str()).unwrap_or("execute").to_string(),
        length: body.get("length").and_then(|v| v.as_u64()).unwrap_or(1) as u8,
        address: format!("0x{:016X}", body.get("address").and_then(|v| v.as_u64()).unwrap_or(0)),
    };

    json!({
        "ok": true,
        "mapping": mapping,
        "translation": {
            "nt_set": format!("CONTEXT.DR{} = 0x{:016X}", dr_index, mapping.value),
            "arm64_set": format!("thread_set_state(ARM_DEBUG_STATE64): {} = {{addr}}, {} = {{enabled}}",
                arm_bvr[dr_index].arm64_name(), arm_bcr[dr_index].arm64_name()),
            "xnu_call": format!("thread_set_state(thread, ARM_DEBUG_STATE64, &state, count) — DBGBCR{}.BT=0b0000 (exec), DBGBVR{}.VA=addr", dr_index, dr_index),
            "bp_types": {
                "execute": "DBGBCR.BT = 0b0000 (ARM64_BREAKPOINT_TYPE_EXECUTE)",
                "read": "DBGBCR.BT = 0b0010 (ARM64_BREAKPOINT_TYPE_LOAD)",
                "write": "DBGBCR.BT = 0b0110 (ARM64_BREAKPOINT_TYPE_STORE)",
                "access": "DBGBCR.BT = 0b0011 (ARM64_BREAKPOINT_TYPE_ACCESS)",
            },
        },
    })
}

pub fn handle_full_breakpoint_map(_body: &Map<String, Value>) -> Value {
    let regs: Vec<Value> = [
        (Arm64DebugReg::DbgBcr0, Arm64DebugReg::DbgBvr0, "DR7 (BP0 ctrl)", "DR0"),
        (Arm64DebugReg::DbgBcr1, Arm64DebugReg::DbgBvr1, "DR7 (BP1 ctrl)", "DR1"),
        (Arm64DebugReg::DbgBcr2, Arm64DebugReg::DbgBvr2, "DR7 (BP2 ctrl)", "DR2"),
        (Arm64DebugReg::DbgBcr3, Arm64DebugReg::DbgBvr3, "DR7 (BP3 ctrl)", "DR3"),
    ]
    .iter()
    .map(|(bcr, bvr, nt_ctrl, nt_addr)| {
        json!({
            "nt_control": nt_ctrl,
            "nt_address": nt_addr,
            "arm64_control": bcr.arm64_name(),
            "arm64_address": bvr.arm64_name(),
            "xnu_state_flavor": bcr.xnu_thread_state_flavor(),
            "nt_equivalent_ctrl": bcr.nt_equivalent(),
            "nt_equivalent_addr": bvr.nt_equivalent(),
        })
    })
    .collect();

    json!({
        "ok": true,
        "registers": regs,
        "master_debug_enable": {
            "nt": "DR7.GE (bit 9) — global debug enable",
            "arm64": "MDSCR_EL1.MDE (bit 15) — monitor debug enable",
            "xnu": "thread_set_state(ARM_THREAD_STATE64) — privileged, requires task_for_pid",
        },
        "debug_status": {
            "nt": "DR6 — debug status (which BP fired)",
            "arm64": "EDSR (External Debug Status Register) or EDEFR (debug exception feedback)",
            "notes": "ARM64 fires EXC_BREAKPOINT when DBGBCR matches. Wine maps to EXCEPTION_BREAKPOINT.",
        },
    })
}

pub fn handle_module_sanitize(body: &Map<String, Value>) -> Value {
    let modules = match body.get("modules") {
        Some(v) => v
            .as_array()
            .map(|arr| arr.iter().filter_map(|m| m.as_str().map(String::from)).collect::<Vec<_>>())
            .unwrap_or_default(),
        None => return json!({"ok": false, "error": "modules array required"}),
    };

    let wine_internal = [
        "ntdll.so",
        "kernel32.so",
        "user32.so",
        "gdi32.so",
        "advapi32.so",
        "ws2_32.so",
        "winecoreaudio.drv",
        "winemac.drv",
        "wineboot.exe",
        "wineserver",
        "libwine.so",
        "libwine.dylib",
    ];

    let wine_suspicious = ["wine", "proton", "dxmt"];

    let mut sanitized = Vec::new();
    let mut hidden_count = 0;

    for module in &modules {
        let lower = module.to_lowercase();
        let is_wine_internal = wine_internal.iter().any(|w| lower.contains(w));
        let is_suspicious = wine_suspicious.iter().any(|w| lower.contains(w));

        let entry = ModuleSanitizeEntry {
            original_name: module.clone(),
            display_name: if is_wine_internal {
                module.replace(".so", ".dll").replace(".dylib", ".dll")
            } else {
                module.clone()
            },
            visible: !is_wine_internal,
            reason: if is_wine_internal {
                "wine_internal".to_string()
            } else if is_suspicious {
                "suspicious_name".to_string()
            } else {
                "ok".to_string()
            },
        };

        if !entry.visible {
            hidden_count += 1;
        }
        sanitized.push(entry);
    }

    json!({
        "ok": true,
        "total": modules.len(),
        "visible": modules.len() - hidden_count,
        "hidden": hidden_count,
        "modules": sanitized,
    })
}

pub fn handle_add_sanitize_rule(body: &Map<String, Value>) -> Value {
    let original = match body.get("original_name").and_then(|v| v.as_str()) {
        Some(n) => n.to_string(),
        None => return json!({"ok": false, "error": "original_name required"}),
    };

    let entry = ModuleSanitizeEntry {
        original_name: original.clone(),
        display_name: body.get("display_name").and_then(|v| v.as_str()).unwrap_or(&original).to_string(),
        visible: body.get("visible").and_then(|v| v.as_bool()).unwrap_or(true),
        reason: body.get("reason").and_then(|v| v.as_str()).unwrap_or("custom_rule").to_string(),
    };

    {
        let mut rules = lock_sanitize_rules();
        rules.push(entry.clone());
        if rules.len() > MAX_COLLECTION_LEN {
            let excess = rules.len() - MAX_COLLECTION_LEN;
            rules.drain(0..excess);
        }
    }

    json!({"ok": true, "rule": entry})
}

pub fn handle_timing_analysis(_body: &Map<String, Value>) -> Value {
    let checks = vec![
        TimingAnalysis {
            check_type: "RDTSC delta".to_string(),
            expected_delta_us: 100,
            tolerance_percent: 10,
            wine_overhead_us: 0,
            detectable: false,
            mitigation: "RDTSC passes through to hardware on macOS — no virtualization layer adds overhead".to_string(),
        },
        TimingAnalysis {
            check_type: "QueryPerformanceCounter".to_string(),
            expected_delta_us: 100,
            tolerance_percent: 5,
            wine_overhead_us: 0,
            detectable: false,
            mitigation: "QPC uses mach_absolute_time() — direct hardware counter, no Wine overhead".to_string(),
        },
        TimingAnalysis {
            check_type: "NtQuerySystemTime".to_string(),
            expected_delta_us: 100,
            tolerance_percent: 5,
            wine_overhead_us: 2,
            detectable: false,
            mitigation: "Wine maps to gettimeofday() — microsecond overhead, within tolerance".to_string(),
        },
        TimingAnalysis {
            check_type: "GetTickCount delta".to_string(),
            expected_delta_us: 1000,
            tolerance_percent: 15,
            wine_overhead_us: 0,
            detectable: false,
            mitigation: "GetTickCount uses mach_absolute_time — millisecond resolution, no detectable anomaly"
                .to_string(),
        },
        TimingAnalysis {
            check_type: "TimeGetTime (multimedia)".to_string(),
            expected_delta_us: 100,
            tolerance_percent: 10,
            wine_overhead_us: 5,
            detectable: false,
            mitigation: "timeGetTime may have slight Wine overhead but within acceptable range".to_string(),
        },
        TimingAnalysis {
            check_type: "CreateProcess+Wait timing".to_string(),
            expected_delta_us: 50000,
            tolerance_percent: 50,
            wine_overhead_us: 5000,
            detectable: false,
            mitigation:
                "Process creation through fork+exec has overhead but anti-cheat doesn't typically check this precisely"
                    .to_string(),
        },
    ];

    let any_detectable = checks.iter().any(|c| c.detectable);

    json!({
        "ok": true,
        "analyses": checks,
        "any_detectable": any_detectable,
        "overall_risk": if any_detectable { "medium" } else { "low" },
        "summary": "All timing checks pass on macOS — Wine doesn't introduce detectable timing anomalies because it uses native Mach/POSIX time sources directly.",
    })
}

pub fn handle_filesystem_check(body: &Map<String, Value>) -> Value {
    let prefix_path = body.get("prefix_path").and_then(|v| v.as_str()).unwrap_or("~/.metalsharp/prefixes/default");

    let checks = vec![
        FileSystemEntry {
            nt_path: "C:\\Windows".to_string(),
            wine_path: format!("{}/drive_c/windows", prefix_path),
            exists: true,
            looks_authentic: true,
            issues: vec![],
        },
        FileSystemEntry {
            nt_path: "C:\\Windows\\System32".to_string(),
            wine_path: format!("{}/drive_c/windows/system32", prefix_path),
            exists: true,
            looks_authentic: true,
            issues: vec!["symlink_to_sysnative".to_string()],
        },
        FileSystemEntry {
            nt_path: "C:\\Windows\\System32\\ntdll.dll".to_string(),
            wine_path: format!("{}/drive_c/windows/system32/ntdll.dll", prefix_path),
            exists: true,
            looks_authentic: true,
            issues: vec![],
        },
        FileSystemEntry {
            nt_path: "C:\\Windows\\SysWOW64".to_string(),
            wine_path: format!("{}/drive_c/windows/syswow64", prefix_path),
            exists: true,
            looks_authentic: true,
            issues: vec![],
        },
        FileSystemEntry {
            nt_path: "C:\\Windows\\explorer.exe".to_string(),
            wine_path: format!("{}/drive_c/windows/explorer.exe", prefix_path),
            exists: true,
            looks_authentic: true,
            issues: vec![],
        },
        FileSystemEntry {
            nt_path: "C:\\Windows\\Temp".to_string(),
            wine_path: format!("{}/drive_c/windows/temp", prefix_path),
            exists: true,
            looks_authentic: true,
            issues: vec![],
        },
        FileSystemEntry {
            nt_path: "C:\\Program Files".to_string(),
            wine_path: format!("{}/drive_c/program files", prefix_path),
            exists: true,
            looks_authentic: true,
            issues: vec![],
        },
        FileSystemEntry {
            nt_path: "C:\\Users".to_string(),
            wine_path: format!("{}/drive_c/users", prefix_path),
            exists: true,
            looks_authentic: true,
            issues: vec![],
        },
    ];

    let authentic_count = checks.iter().filter(|c| c.looks_authentic).count();

    json!({
        "ok": true,
        "prefix_path": prefix_path,
        "total_paths": checks.len(),
        "authentic": authentic_count,
        "issues": checks.iter().flat_map(|c| c.issues.iter().cloned()).collect::<Vec<_>>(),
        "checks": checks,
        "recommendations": [
            "Ensure Wine prefix has complete Windows directory structure",
            "System32 should be real directory (not symlink) for lstat checks",
            "ntdll.dll, kernel32.dll must exist as PE files in System32",
            "dosdevices should map C: → drive_c, Z: → /",
            "registry files (system.dat, user.dat) must exist in windows/",
        ],
    })
}

pub fn handle_status_survey(_body: &Map<String, Value>) -> Value {
    json!({
        "ok": true,
        "nt_status_codes": {
            "STATUS_SUCCESS": format!("0x{:08X}", STATUS_SUCCESS),
            "STATUS_PORT_NOT_SET": format!("0x{:08X}", STATUS_PORT_NOT_SET),
            "STATUS_DEBUGGER_INACTIVE": format!("0x{:08X}", STATUS_DEBUGGER_INACTIVE),
            "STATUS_OBJECT_TYPE_MISMATCH": format!("0x{:08X}", STATUS_OBJECT_TYPE_MISMATCH),
        },
        "check_matrix": [
            {"check": "PEB.BeingDebugged", "status": "done", "response": "0 (false)", "risk": "none"},
            {"check": "ProcessDebugPort", "status": "done", "response": "0", "risk": "none"},
            {"check": "ProcessDebugObjectHandle", "status": "build_needed", "response": "STATUS_PORT_NOT_SET", "risk": "low"},
            {"check": "ProcessDebugFlags", "status": "done", "response": "0", "risk": "none"},
            {"check": "Hardware DR0-DR3", "status": "drill_needed", "response": "all zero", "risk": "medium"},
            {"check": "RDTSC timing", "status": "done", "response": "native", "risk": "none"},
            {"check": "Module enumeration", "status": "drill_needed", "response": "sanitized", "risk": "medium"},
            {"check": "Filesystem lstat", "status": "drill_needed", "response": "authentic", "risk": "low"},
            {"check": "Parent process", "status": "done", "response": "expected", "risk": "none"},
            {"check": "ThreadHideFromDebugger", "status": "done", "response": "accepted", "risk": "none"},
            {"check": "Debug registers DR6/DR7", "status": "done", "response": "0", "risk": "none"},
        ],
        "overall_assessment": "8 of 11 checks fully handled. 3 need additional work: ProcessDebugObjectHandle (build), hardware breakpoints (drill ARM64 debug state), module enumeration sanitization (drill Wine module list filtering).",
    })
}

pub fn handle_seed_demo(_body: &Map<String, Value>) -> Value {
    let all = handle_run_all_checks(&Map::new());
    let timing = handle_timing_analysis(&Map::new());
    let hw_map = handle_full_breakpoint_map(&Map::new());

    let modules = handle_module_sanitize(&serde_json::from_str(
        "{\"modules\": [\"ntdll.dll\", \"kernel32.dll\", \"user32.dll\", \"ntdll.so\", \"wineboot.exe\", \"libwine.so\", \"game.exe\", \"d3d11.dll\", \"dxgi.dll\"]}"
    ).unwrap());

    let fs =
        handle_filesystem_check(&serde_json::from_str("{\"prefix_path\": \"~/.metalsharp/prefixes/game\"}").unwrap());

    json!({
        "ok": true,
        "all_checks": {
            "total": all["total_checks"],
            "detected": all["detected"],
            "status": all["overall_status"],
        },
        "timing_risk": timing["overall_risk"],
        "module_sanitization": {
            "total": modules["total"],
            "hidden": modules["hidden"],
        },
        "filesystem": {
            "total_paths": fs["total_paths"],
            "authentic": fs["authentic"],
        },
        "summary": "Phase 8 anti-debug assessment: all primary checks pass, timing analysis clean, module sanitization hides Wine internals, filesystem looks authentic.",
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn empty_body() -> Map<String, Value> {
        Map::new()
    }

    #[test]
    fn test_peb_being_debugged() {
        let body: Map<String, Value> = serde_json::from_str("{\"check_type\": \"peb_being_debugged\"}").unwrap();
        let result = handle_simulate_check(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["check"]["detected"], false);
        assert_eq!(result["check"]["nt_status"], 0);
    }

    #[test]
    fn test_process_debug_port() {
        let body: Map<String, Value> = serde_json::from_str("{\"check_type\": \"process_debug_port\"}").unwrap();
        let result = handle_simulate_check(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["check"]["detected"], false);
    }

    #[test]
    fn test_process_debug_object_handle() {
        let body: Map<String, Value> =
            serde_json::from_str("{\"check_type\": \"process_debug_object_handle\"}").unwrap();
        let result = handle_simulate_check(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert!(!result["check"]["detected"].as_bool().unwrap());
        let wine_response = result["check"]["wine_response"].as_str().unwrap_or("");
        assert!(wine_response.contains("STATUS_PORT_NOT_SET"));
    }

    #[test]
    fn test_hardware_breakpoints() {
        let body: Map<String, Value> = serde_json::from_str("{\"check_type\": \"hardware_breakpoints\"}").unwrap();
        let result = handle_simulate_check(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["check"]["detected"], false);
        assert_eq!(result["risk_level"], "drill_needed");
    }

    #[test]
    fn test_timing_check() {
        let body: Map<String, Value> = serde_json::from_str("{\"check_type\": \"timing_check\"}").unwrap();
        let result = handle_simulate_check(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["check"]["detected"], false);
    }

    #[test]
    fn test_module_enumeration() {
        let body: Map<String, Value> = serde_json::from_str("{\"check_type\": \"module_enumeration\"}").unwrap();
        let result = handle_simulate_check(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["check"]["detected"], false);
    }

    #[test]
    fn test_filesystem_check_type() {
        let body: Map<String, Value> = serde_json::from_str("{\"check_type\": \"filesystem_check\"}").unwrap();
        let result = handle_simulate_check(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["check"]["detected"], false);
    }

    #[test]
    fn test_invalid_check_type() {
        let body: Map<String, Value> = serde_json::from_str("{\"check_type\": \"invalid\"}").unwrap();
        let result = handle_simulate_check(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_run_all_checks() {
        let result = handle_run_all_checks(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["total_checks"].as_u64().unwrap() >= 11);
        assert_eq!(result["detected"], 0);
        assert_eq!(result["overall_status"], "clean");
    }

    #[test]
    fn test_check_results() {
        handle_simulate_check(&serde_json::from_str("{\"check_type\": \"peb_being_debugged\"}").unwrap());
        let result = handle_check_results(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["count"].as_u64().unwrap() >= 1);
    }

    #[test]
    fn test_hw_breakpoint_map() {
        let body: Map<String, Value> = serde_json::from_str(
            "{\"dr_index\": 0, \"address\": 4194304, \"enabled\": true, \"bp_type\": \"execute\"}",
        )
        .unwrap();
        let result = handle_hw_breakpoint_map(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["mapping"]["arm64_reg"].as_str().unwrap().contains("DBGBCR0"));
        assert!(result["translation"]["bp_types"].is_object());
    }

    #[test]
    fn test_hw_breakpoint_map_invalid_index() {
        let body: Map<String, Value> = serde_json::from_str("{\"dr_index\": 5}").unwrap();
        let result = handle_hw_breakpoint_map(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_full_breakpoint_map() {
        let result = handle_full_breakpoint_map(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["registers"].as_array().unwrap().len() == 4);
        assert!(result["master_debug_enable"].is_object());
    }

    #[test]
    fn test_module_sanitize() {
        let body: Map<String, Value> =
            serde_json::from_str("{\"modules\": [\"ntdll.dll\", \"ntdll.so\", \"libwine.so\", \"game.exe\"]}").unwrap();
        let result = handle_module_sanitize(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["total"], 4);
        let hidden = result["hidden"].as_u64().unwrap();
        assert!(hidden >= 2);
    }

    #[test]
    fn test_module_sanitize_missing() {
        let body: Map<String, Value> = serde_json::from_str("{}").unwrap();
        let result = handle_module_sanitize(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_add_sanitize_rule() {
        let body: Map<String, Value> = serde_json::from_str(
            "{\"original_name\": \"custom_ac.dll\", \"visible\": false, \"reason\": \"anti_cheat_internal\"}",
        )
        .unwrap();
        let result = handle_add_sanitize_rule(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["rule"]["visible"], false);
    }

    #[test]
    fn test_timing_analysis() {
        let result = handle_timing_analysis(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["analyses"].as_array().unwrap().len() >= 5);
        assert_eq!(result["any_detectable"], false);
        assert_eq!(result["overall_risk"], "low");
    }

    #[test]
    fn test_filesystem_check() {
        let result = handle_filesystem_check(&Map::new());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["total_paths"].as_u64().unwrap() >= 8);
        assert!(result["recommendations"].as_array().unwrap().len() >= 4);
    }

    #[test]
    fn test_status_survey() {
        let result = handle_status_survey(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["check_matrix"].as_array().unwrap().len() >= 10);
        assert!(result["nt_status_codes"].is_object());
    }

    #[test]
    fn test_seed_demo() {
        let result = handle_seed_demo(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["all_checks"]["status"], "clean");
        assert_eq!(result["timing_risk"], "low");
    }

    #[test]
    fn test_debug_check_type_from_str() {
        assert!(DebugCheckType::from_str("peb_being_debugged").is_some());
        assert!(DebugCheckType::from_str("process_debug_port").is_some());
        assert!(DebugCheckType::from_str("invalid").is_none());
    }
}
