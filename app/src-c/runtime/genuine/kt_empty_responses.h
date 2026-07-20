#ifndef METALSHARP_KT_EMPTY_RESPONSES_H
#define METALSHARP_KT_EMPTY_RESPONSES_H

typedef struct {
    const char* method;
    const char* path;
    const char* json;
} KtEmptyResponse;

static const char KT_EMPTY_JSON_0[] = "{\"error\":\"original_name required\",\"ok\":false}";
static const char KT_EMPTY_JSON_1[] = "{\"count\":0,\"ok\":true,\"results\":[]}";
static const char KT_EMPTY_JSON_2[] =
    "{\"authentic\":8,\"checks\":[{\"exists\":true,\"issues\":[],\"looks_authentic\":true,\"nt_path\":\"C:"
    "\\\\Windows\",\""
    "wine_path\":\"~/.metalsharp/prefixes/default/drive_c/windows\"},{\"exists\":true,\"issues\":[\"symlink_to_sy"
    "snative\"],\"looks_authentic\":true,\"nt_path\":\"C:\\\\Windows\\\\System32\",\"wine_path\":\"~/.metalsharp/prefix"
    "es/default/drive_c/windows/system32\"},{\"exists\":true,\"issues\":[],\"looks_authentic\":true,\"nt_path\":\"C"
    ":\\\\Windows\\\\System32\\\\ntdll.dll\",\"wine_path\":\"~/.metalsharp/prefixes/default/drive_c/windows/system3"
    "2/"
    "ntdll.dll\"},{\"exists\":true,\"issues\":[],\"looks_authentic\":true,\"nt_path\":\"C:\\\\Windows\\\\SysWOW64\",\"w"
    "ine_path\":\"~/.metalsharp/prefixes/default/drive_c/windows/syswow64\"},{\"exists\":true,\"issues\":[],\"loo"
    "ks_authentic\":true,\"nt_path\":\"C:\\\\Windows\\\\explorer.exe\",\"wine_path\":\"~/.metalsharp/prefixes/default"
    "/drive_c/windows/explorer.exe\"},{\"exists\":true,\"issues\":[],\"looks_authentic\":true,\"nt_path\":\"C:\\\\Win"
    "dows\\\\Temp\",\"wine_path\":\"~/.metalsharp/prefixes/default/drive_c/windows/temp\"},{\"exists\":true,\"issue"
    "s\":[],\"looks_authentic\":true,\"nt_path\":\"C:\\\\Program Files\",\"wine_path\":\"~/.metalsharp/prefixes/defau"
    "lt/drive_c/program files\"},{\"exists\":true,\"issues\":[],\"looks_authentic\":true,\"nt_path\":\"C:\\\\Users\",\""
    "wine_path\":\"~/.metalsharp/prefixes/default/drive_c/users\"}],\"issues\":[\"symlink_to_sysnative\"],\"ok\":t"
    "rue,\"prefix_path\":\"~/.metalsharp/prefixes/default\",\"recommendations\":[\"Ensure Wine prefix has comple"
    "te Windows directory structure\",\"System32 should be real directory (not symlink) for lstat checks\",\""
    "ntdll.dll, kernel32.dll must exist as PE files in System32\",\"dosdevices should map C: → drive_c, Z: "
    "→ /\",\"registry files (system.dat, user.dat) must exist in windows/\"],\"total_paths\":8}";
static const char KT_EMPTY_JSON_3[] =
    "{\"debug_status\":{\"arm64\":\"EDSR (External Debug Status Register) or EDEFR (debug exception feedback)\""
    ",\"notes\":\"ARM64 fires EXC_BREAKPOINT when DBGBCR matches. Wine maps to EXCEPTION_BREAKPOINT.\",\"nt\":\""
    "DR6 — debug status (which BP fired)\"},\"master_debug_enable\":{\"arm64\":\"MDSCR_EL1.MDE (bit 15) — monit"
    "or debug enable\",\"nt\":\"DR7.GE (bit 9) — global debug enable\",\"xnu\":\"thread_set_state(ARM_THREAD_STAT"
    "E64) — privileged, requires task_for_pid\"},\"ok\":true,\"registers\":[{\"arm64_address\":\"DBGBVR0_EL1\",\"ar"
    "m64_control\":\"DBGBCR0_EL1\",\"nt_address\":\"DR0\",\"nt_control\":\"DR7 (BP0 ctrl)\",\"nt_equivalent_addr\":\"DR"
    "0 (breakpoint address 0)\",\"nt_equivalent_ctrl\":\"DR7 (breakpoint control, BP 0)\",\"xnu_state_flavor\":\""
    "ARM_DEBUG_STATE64 (DBGBCR)\"},{\"arm64_address\":\"DBGBVR1_EL1\",\"arm64_control\":\"DBGBCR1_EL1\",\"nt_addres"
    "s\":\"DR1\",\"nt_control\":\"DR7 (BP1 ctrl)\",\"nt_equivalent_addr\":\"DR1 (breakpoint address 1)\",\"nt_equival"
    "ent_ctrl\":\"DR7 (breakpoint control, BP 1)\",\"xnu_state_flavor\":\"ARM_DEBUG_STATE64 (DBGBCR)\"},{\"arm64_"
    "address\":\"DBGBVR2_EL1\",\"arm64_control\":\"DBGBCR2_EL1\",\"nt_address\":\"DR2\",\"nt_control\":\"DR7 (BP2 ctrl)"
    "\",\"nt_equivalent_addr\":\"DR2 (breakpoint address 2)\",\"nt_equivalent_ctrl\":\"DR7 (breakpoint control, B"
    "P 2)\",\"xnu_state_flavor\":\"ARM_DEBUG_STATE64 (DBGBCR)\"},{\"arm64_address\":\"DBGBVR3_EL1\",\"arm64_control"
    "\":\"DBGBCR3_EL1\",\"nt_address\":\"DR3\",\"nt_control\":\"DR7 (BP3 ctrl)\",\"nt_equivalent_addr\":\"DR3 (breakpoi"
    "nt address 3)\",\"nt_equivalent_ctrl\":\"DR7 (breakpoint control, BP 3)\",\"xnu_state_flavor\":\"ARM_DEBUG_S"
    "TATE64 (DBGBCR)\"}]}";
static const char KT_EMPTY_JSON_4[] = "{\"error\":\"dr_index required (0-3)\",\"ok\":false}";
static const char KT_EMPTY_JSON_5[] = "{\"error\":\"modules array required\",\"ok\":false}";
static const char KT_EMPTY_JSON_6[] =
    "{\"detected\":0,\"handled\":7,\"needs_work\":5,\"ok\":true,\"overall_status\":\"clean\",\"results\":[{\"check\":{"
    "\"ch"
    "eck_type\":\"PebBeingDebugged\",\"detected\":false,\"notes\":\"Wine sets PEB.BeingDebugged to 0. No debugger"
    " attached from process perspective.\",\"nt_status\":0,\"response_value\":{\"BeingDebugged\":0,\"NtGlobalFlag"
    "\":0},\"wine_response\":\"PEB.BeingDebugged = 0\"},\"ok\":true,\"risk_level\":\"handled\"},{\"check\":{\"check_typ"
    "e\":\"ProcessDebugPort\",\"detected\":false,\"notes\":\"Wine returns DebugPort = 0. No kernel debug port all"
    "ocated.\",\"nt_status\":0,\"response_value\":{\"DebugPort\":0},\"wine_response\":\"DebugPort = 0\"},\"ok\":true,\""
    "risk_level\":\"handled\"},{\"check\":{\"check_type\":\"ProcessDebugObjectHandle\",\"detected\":false,\"notes\":\"W"
    "ine must return STATUS_PORT_NOT_SET consistently. This is a BUILD task — ensure Wine ntdll returns t"
    "his for ProcessDebugObjectHandle class.\",\"nt_status\":3221226323,\"response_value\":{\"Handle\":0,\"Status"
    "\":\"0xC0000353\"},\"wine_response\":\"STATUS_PORT_NOT_SET (0xC0000353)\"},\"ok\":true,\"risk_level\":\"build_ne"
    "eded\"},{\"check\":{\"check_type\":\"ProcessDebugFlags\",\"detected\":false,\"notes\":\"Flags = 0 means no debug"
    " object. Anti-cheat checks this as secondary verification.\",\"nt_status\":0,\"response_value\":{\"DebugFl"
    "ags\":0},\"wine_response\":\"ProcessDebugFlags = 0\"},\"ok\":true,\"risk_level\":\"handled\"},{\"check\":{\"check_"
    "type\":\"HardwareBreakpoints\",\"detected\":false,\"notes\":\"ARM64: DBGBCR0-3 and DBGBVR0-3 all zero via th"
    "read_get_state(ARM_DEBUG_STATE64). No hardware breakpoints set.\",\"nt_status\":0,\"response_value\":{\"DR"
    "0\":0,\"DR1\":0,\"DR2\":0,\"DR3\":0,\"DR6\":0,\"DR7\":0},\"wine_response\":\"DR0-DR3 = 0, DR6 = 0, DR7 = "
    "0\"},\"ok\":"
    "true,\"risk_level\":\"drill_needed\"},{\"check\":{\"check_type\":\"TimingCheck\",\"detected\":false,\"notes\":"
    "\"RDT"
    "SC passes through to hardware on macOS. No virtualization overhead. QueryPerformanceCounter uses mac"
    "h_absolute_time.\",\"nt_status\":0,\"response_value\":{\"AnomalyDetected\":false,\"QpcDelta\":\"native\",\"Rdtsc"
    "Delta\":\"native\"},\"wine_response\":\"RDTSC/QueryPerformanceCounter returns real hardware counter\"},\"ok\""
    ":true,\"risk_level\":\"drill_needed\"},{\"check\":{\"check_type\":\"ModuleEnumeration\",\"detected\":false,\"note"
    "s\":\"Wine's PE loader presents modules as Windows binaries. libwine.so, ntdll.so not visible through "
    "EnumProcessModules.\",\"nt_status\":0,\"response_value\":{\"ModuleCount\":42,\"SuspiciousModules\":[],\"WineMo"
    "dulesVisible\":false},\"wine_response\":\"Module list shows only Windows binaries\"},\"ok\":true,\"risk_leve"
    "l\":\"drill_needed\"},{\"check\":{\"check_type\":\"FileSystemCheck\",\"detected\":false,\"notes\":\"Wine prefix co"
    "ntains full Windows directory structure. lstat returns plausible metadata.\",\"nt_status\":0,\"response_"
    "value\":{\"IsDirectory\":true,\"LooksAuthentic\":true,\"PathExists\":true},\"wine_response\":\"C:\\\\Windows\\\\Sy"
    "stem32 resolves via Wine virtual filesystem\"},\"ok\":true,\"risk_level\":\"drill_needed\"},{\"check\":{\"chec"
    "k_type\":\"ParentProcessCheck\",\"detected\":false,\"notes\":\"Wine can report expected parent process. Anti"
    "-cheat verifies the parent is explorer.exe or game launcher.\",\"nt_status\":0,\"response_value\":{\"Paren"
    "tName\":\"explorer.exe\",\"ParentPid\":\"expected\"},\"wine_response\":\"Parent PID = expected launcher proces"
    "s\"},\"ok\":true,\"risk_level\":\"handled\"},{\"check\":{\"check_type\":\"ThreadHideFromDebugger\",\"detected\":"
    "fal"
    "se,\"notes\":\"Wine accepts ThreadHideFromDebugger without error. Anti-cheat threads call this to preve"
    "nt debuggers from receiving their events.\",\"nt_status\":0,\"response_value\":{\"Hidden\":true},\"wine_resp"
    "onse\":\"STATUS_SUCCESS — thread marked as hidden from debugger\"},\"ok\":true,\"risk_level\":\"handled\"},{\""
    "check\":{\"check_type\":\"DebugRegisterCheck\",\"detected\":false,\"notes\":\"DR6 debug status register = 0 me"
    "ans no debug exceptions have occurred. DR7 = 0 means no hardware breakpoints enabled.\",\"nt_status\":0"
    ",\"response_value\":{\"DR6\":0,\"DR7\":0,\"DebugExceptionPending\":false},\"wine_response\":\"DR6 = 0 (no debug"
    " exceptions), DR7 = 0 (no breakpoints)\"},\"ok\":true,\"risk_level\":\"handled\"},{\"check\":{\"check_type\":\"N"
    "tQueryVirtualMemory\",\"detected\":false,\"notes\":\"Wine memory layout looks normal. Anti-cheat scans for"
    " debug-related memory pages (int3 breakpoints, watchpoints).\",\"nt_status\":0,\"response_value\":{\"Regio"
    "ns\":\"normal\",\"SuspiciousGaps\":false},\"wine_response\":\"mach_vm_region returns legitimate memory regio"
    "ns\"},\"ok\":true,\"risk_level\":\"handled\"}],\"total_checks\":12}";
static const char KT_EMPTY_JSON_7[] =
    "{\"all_checks\":{\"detected\":0,\"status\":\"clean\",\"total\":12},\"filesystem\":{\"authentic\":8,\"total_"
    "paths\":8"
    "},\"module_sanitization\":{\"hidden\":3,\"total\":9},\"ok\":true,\"summary\":\"Phase 8 anti-debug assessment: a"
    "ll primary checks pass, timing analysis clean, module sanitization hides Wine internals, filesystem "
    "looks authentic.\",\"timing_risk\":\"low\"}";
static const char KT_EMPTY_JSON_8[] =
    "{\"error\":\"check_type required: peb_being_debugged, process_debug_port, process_debug_object_handle, "
    "process_debug_flags, hardware_breakpoints, timing_check, module_enumeration, filesystem_check, paren"
    "t_process_check, thread_hide_from_debugger, debug_register_check\",\"ok\":false}";
static const char KT_EMPTY_JSON_9[] =
    "{\"check_matrix\":[{\"check\":\"PEB.BeingDebugged\",\"response\":\"0 "
    "(false)\",\"risk\":\"none\",\"status\":\"done\"},"
    "{\"check\":\"ProcessDebugPort\",\"response\":\"0\",\"risk\":\"none\",\"status\":\"done\"},{\"check\":"
    "\"ProcessDebugObje"
    "ctHandle\",\"response\":\"STATUS_PORT_NOT_SET\",\"risk\":\"low\",\"status\":\"build_needed\"},{\"check\":"
    "\"ProcessDe"
    "bugFlags\",\"response\":\"0\",\"risk\":\"none\",\"status\":\"done\"},{\"check\":\"Hardware "
    "DR0-DR3\",\"response\":\"all "
    "zero\",\"risk\":\"medium\",\"status\":\"drill_needed\"},{\"check\":\"RDTSC "
    "timing\",\"response\":\"native\",\"risk\":\"n"
    "one\",\"status\":\"done\"},{\"check\":\"Module "
    "enumeration\",\"response\":\"sanitized\",\"risk\":\"medium\",\"status\":"
    "\"drill_needed\"},{\"check\":\"Filesystem "
    "lstat\",\"response\":\"authentic\",\"risk\":\"low\",\"status\":\"drill_need"
    "ed\"},{\"check\":\"Parent "
    "process\",\"response\":\"expected\",\"risk\":\"none\",\"status\":\"done\"},{\"check\":\"Thread"
    "HideFromDebugger\",\"response\":\"accepted\",\"risk\":\"none\",\"status\":\"done\"},{\"check\":\"Debug registers "
    "DR6"
    "/DR7\",\"response\":\"0\",\"risk\":\"none\",\"status\":\"done\"}],\"nt_status_codes\":{\"STATUS_DEBUGGER_"
    "INACTIVE\":\""
    "0xC0000354\",\"STATUS_OBJECT_TYPE_MISMATCH\":\"0xC0000024\",\"STATUS_PORT_NOT_SET\":\"0xC0000353\",\"STATUS_SU"
    "CCESS\":\"0x00000000\"},\"ok\":true,\"overall_assessment\":\"8 of 11 checks fully handled. 3 need additional"
    " work: ProcessDebugObjectHandle (build), hardware breakpoints (drill ARM64 debug state), module enum"
    "eration sanitization (drill Wine module list filtering).\"}";
static const char KT_EMPTY_JSON_10[] =
    "{\"analyses\":[{\"check_type\":\"RDTSC delta\",\"detectable\":false,\"expected_delta_us\":100,\"mitigation\":\"RD"
    "TSC passes through to hardware on macOS — no virtualization layer adds overhead\",\"tolerance_percent\""
    ":10,\"wine_overhead_us\":0},{\"check_type\":\"QueryPerformanceCounter\",\"detectable\":false,\"expected_delta"
    "_us\":100,\"mitigation\":\"QPC uses mach_absolute_time() — direct hardware counter, no Wine overhead\",\"t"
    "olerance_percent\":5,\"wine_overhead_us\":0},{\"check_type\":\"NtQuerySystemTime\",\"detectable\":false,\"expe"
    "cted_delta_us\":100,\"mitigation\":\"Wine maps to gettimeofday() — microsecond overhead, within toleranc"
    "e\",\"tolerance_percent\":5,\"wine_overhead_us\":2},{\"check_type\":\"GetTickCount delta\",\"detectable\":false"
    ",\"expected_delta_us\":1000,\"mitigation\":\"GetTickCount uses mach_absolute_time — millisecond resolutio"
    "n, no detectable anomaly\",\"tolerance_percent\":15,\"wine_overhead_us\":0},{\"check_type\":\"TimeGetTime (m"
    "ultimedia)\",\"detectable\":false,\"expected_delta_us\":100,\"mitigation\":\"timeGetTime may have slight Win"
    "e overhead but within acceptable range\",\"tolerance_percent\":10,\"wine_overhead_us\":5},{\"check_type\":\""
    "CreateProcess+Wait timing\",\"detectable\":false,\"expected_delta_us\":50000,\"mitigation\":\"Process creati"
    "on through fork+exec has overhead but anti-cheat doesn't typically check this precisely\",\"tolerance_"
    "percent\":50,\"wine_overhead_us\":5000}],\"any_detectable\":false,\"ok\":true,\"overall_risk\":\"low\",\"summary"
    "\":\"All timing checks pass on macOS — Wine doesn't introduce detectable timing anomalies because it u"
    "ses native Mach/POSIX time sources directly.\"}";
static const char KT_EMPTY_JSON_11[] =
    "{\"code\":{\"restoreHandler\":\"36 bytes (load saved context, MSR SP_el0, ERET)\",\"trampoline\":\"36 bytes ("
    "save LR, load args, BLR to apc_routine, B to restore)\"},\"codeSize\":72,\"ok\":true,\"pageAddress\":\"0x000"
    "0000105E98000\",\"pageSize\":4096,\"restoreAssembly\":[\"ldr x0, [pc, #12]           // load saved_sp from"
    " literal pool\",\"msr sp_el0, x0              // restore user stack pointer\",\"ldr x0, [pc, #8]        "
    "    // load saved_pc from literal pool\",\"msr elr_el1, x0             // restore return address\",\"ere"
    "t                         // return to original code\",\".quad saved_sp              // embedded saved"
    " stack pointer\",\".quad saved_pc              // embedded saved program counter\"],\"restoreHandlerOffs"
    "et\":36,\"trampolineAssembly\":[\"stp x29, x30, [sp, #-16]!   // save frame pointer and link register\",\""
    "stp x19, x20, [sp, #-16]!   // save callee-saved registers\",\"stp x21, x22, [sp, #-16]!   // save mor"
    "e callee-saved\",\"mov x0, x21                  // x0 = ApcContext (passed in x21)\",\"blr x1           "
    "            // call ApcRoutine(x0=ApcContext)\",\"b restore_handler            // jump to restore hand"
    "ler\",\"// --- restore handler ---\",\"ldp x21, x22, [sp], #16     // restore callee-saved\",\"ldp x19, x2"
    "0, [sp], #16     // restore callee-saved\",\"ldp x29, x30, [sp], #16     // restore fp and lr\"],\"tramp"
    "olineOffset\":0}";
static const char KT_EMPTY_JSON_12[] = "{\"error\":\"thread_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_13[] = "{\"error\":\"thread_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_14[] = "{\"error\":\"thread_handle (u64) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_15[] = "{\"ok\":true,\"threads\":[],\"totalThreads\":0}";
static const char KT_EMPTY_JSON_16[] = "{\"error\":\"thread_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_17[] = "{\"error\":\"thread_port (u32 Mach thread port) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_18[] = "{\"error\":\"thread_id (u64) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_19[] =
    "{\"ok\":true,\"trampoline\":{\"allocated\":false,\"code_size\":0,\"detail\":\"Trampoline page not yet allocated"
    "\",\"page_address\":\"0x0000000000000000\",\"page_size\":0,\"restore_handler_offset\":0,\"status\":\"not_allocat"
    "ed\",\"trampoline_offset\":0}}";
static const char KT_EMPTY_JSON_20[] = "{\"error\":\"thread_id (u64) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_21[] = "{\"error\":\"driver_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_22[] = "{\"error\":\"ioctl_code required\",\"ok\":false}";
static const char KT_EMPTY_JSON_23[] = "{\"error\":\"driver_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_24[] =
    "{\"ok\":true,\"scaffold\":{\"dispatch\":\"IRP_MJ_DEVICE_CONTROL → MetalSharpAntiCheat::externalMethod()\",\"e"
    "ntry_point\":\"DriverEntry equivalent: MetalSharpAntiCheat::start()\",\"files\":[\"MetalSharpAntiCheat.cpp"
    " — IOService subclass + IOUserClient\",\"MetalSharpAntiCheatInfo.plist — Extension descriptor\",\"MetalS"
    "harpAntiCheat.entitlements — com.apple.developer.endpoint-security.client\",\"Makefile — meson build f"
    "or MetalSharp integration\"],\"mach_services\":2,\"subscriptions\":4},\"template\":{\"es_subscriptions\":[\"ES"
    "_EVENT_TYPE_NOTIFY_EXEC\",\"ES_EVENT_TYPE_NOTIFY_FORK\",\"ES_EVENT_TYPE_NOTIFY_EXIT\",\"ES_EVENT_TYPE_NOTI"
    "FY_MMAP\"],\"extension_type\":\"EndpointSecurity\",\"iokit_methods\":[\"start(IOService *provider)\",\"stop(IO"
    "Service *provider)\",\"externalMethod(uint32_t selector, ...)\",\"clientClose()\",\"registerNotification(m"
    "ach_port_t port)\"],\"mach_services\":[\"com.metalsharp.anticheat.metalsharpanticheat\",\"com.metalsharp.a"
    "nticheat.metalsharpanticheat.notifications\"],\"name\":\"MetalSharpAntiCheat\",\"nt_callbacks\":[\"PsSetCrea"
    "teProcessNotifyRoutineEx2\",\"PsSetCreateThreadNotifyRoutineEx\",\"PsSetLoadImageNotifyRoutineEx\",\"ObReg"
    "isterCallbacks\",\"CmRegisterCallbackEx\"]}}";
static const char KT_EMPTY_JSON_25[] = "{\"count\":0,\"drivers\":[],\"ok\":true}";
static const char KT_EMPTY_JSON_26[] = "{\"count\":0,\"devices\":[],\"ok\":true}";
static const char KT_EMPTY_JSON_27[] = "{\"count\":0,\"mappings\":[],\"ok\":true}";
static const char KT_EMPTY_JSON_28[] = "{\"count\":0,\"irps\":[],\"ok\":true}";
static const char KT_EMPTY_JSON_29[] = "{\"error\":\"name required\",\"ok\":false}";
static const char KT_EMPTY_JSON_30[] = "{\"error\":\"nt_ioctl_code required\",\"ok\":false}";
static const char KT_EMPTY_JSON_31[] =
    "{\"irp_ids\":[1,2,3],\"ok\":true,\"scenario\":\"EAC-style anti-cheat: load driver → create device → registe"
    "r IOCTL handlers → open → send process event IOCTL → close\",\"seeded\":{\"device_id\":1,\"driver_id\":1,\"i"
    "octls_registered\":3,\"irps_dispatched\":3}}";
static const char KT_EMPTY_JSON_32[] =
    "{\"communication_path\":{\"macos\":\"UserMode → IOConnectCallMethod(connect, selector, in, out) → IOUserC"
    "lient::externalMethod → Mach reply → Result\",\"nt\":\"UserMode → DeviceIoControl(hDevice, ioctl, in, ou"
    "t) → KernelDriver → IRP_MJ_DEVICE_CONTROL → Result\"},\"ioctl_encoding\":\"CTL_CODE(DeviceType, Function"
    ", Method, Access) = (DeviceType << 16) | (Access << 14) | (Function << 2) | Method\",\"irp_major_funct"
    "ions\":[{\"code\":\"0x00\",\"iokit\":\"IOServiceOpen → "
    "IOUserClient::start\",\"nt\":\"IRP_MJ_CREATE\"},{\"code\":\"0"
    "x02\",\"iokit\":\"IOServiceClose → "
    "IOUserClient::stop\",\"nt\":\"IRP_MJ_CLOSE\"},{\"code\":\"0x03\",\"iokit\":\"IOUs"
    "erClient shared memory / registerNotification\",\"nt\":\"IRP_MJ_READ\"},{\"code\":\"0x04\",\"iokit\":\"IOUserCli"
    "ent setProperties / shared memory write\",\"nt\":\"IRP_MJ_WRITE\"},{\"code\":\"0x08\",\"iokit\":\"No direct equi"
    "valent (userspace flush)\",\"nt\":\"IRP_MJ_FLUSH_BUFFERS\"},{\"code\":\"0x0E\",\"iokit\":\"IOUserClient::externa"
    "lMethod (main anti-cheat path)\",\"nt\":\"IRP_MJ_DEVICE_CONTROL\"},{\"code\":\"0x0F\",\"iokit\":\"IOUserClient::"
    "externalMethod (privileged)\",\"nt\":\"IRP_MJ_INTERNAL_DEVICE_CONTROL\"},{\"code\":\"0x10\",\"iokit\":\"IOServic"
    "e::systemWillShutdown\",\"nt\":\"IRP_MJ_SHUTDOWN\"},{\"code\":\"0x12\",\"iokit\":\"IOUserClient::clientClose\",\"n"
    "t\":\"IRP_MJ_CLEANUP\"},{\"code\":\"0x16\",\"iokit\":\"IOService::powerStateDidChangeTo\",\"nt\":\"IRP_MJ_POWER\"}"
    ","
    "{\"code\":\"0x1B\",\"iokit\":\"IOService::message(kIOMessageService*)\",\"nt\":\"IRP_MJ_PNP\"},{\"code\":\"0x1E\","
    "\"i"
    "okit\":\"IORegistryEntry::setProperties\",\"nt\":\"IRP_MJ_SYSTEM_CONTROL\"}],\"ok\":true,\"wdm_to_iokit\":[{\"ma"
    "cos\":\"IOService subclass\",\"notes\":\"Driver state container. macOS: com_metalsharp_anticheat_* IOServi"
    "ce.\",\"nt\":\"DRIVER_OBJECT\"},{\"macos\":\"IOUserClient\",\"notes\":\"Device endpoint. User-mode opens via IOS"
    "erviceOpen → IOUserClient.\",\"nt\":\"DEVICE_OBJECT\"},{\"macos\":\"Mach message / IOExternalMethod\",\"notes\""
    ":\"Request/response unit. Serialized as Mach IPC message or IOExternalMethodDispatch.\",\"nt\":\"IRP (I/O"
    " Request Packet)\"},{\"macos\":\"IOUserClient::externalMethod\",\"notes\":\"Dispatch function for IRP_MJ_*. "
    "Maps to IOKit selector-based dispatch.\",\"nt\":\"DRIVER_DISPATCH\"},{\"macos\":\"Mach message return\",\"note"
    "s\":\"Status + bytes transferred. Encoded in Mach reply message.\",\"nt\":\"IO_STATUS_BLOCK\"},{\"macos\":\"IO"
    "RegistryEntry name\",\"notes\":\"Device naming. \\\\Device\\\\AntiCheat → IORegistry /metalsharp/anticheat/0"
    ".\",\"nt\":\"UNICODE_STRING (dev name)\"},{\"macos\":\"IOWorkLoop + IOCommandGate\",\"notes\":\"Deferred procedu"
    "re call. IOKit serialized work loop for safe concurrency.\",\"nt\":\"IO_DPC_ROUTINE\"},{\"macos\":\"IOFilter"
    "InterruptEventSource\",\"notes\":\"Interrupt handling. IOKit interrupt event source for hardware drivers"
    ".\",\"nt\":\"KINTERRUPT\"},{\"macos\":\"IOService::fWorkspace\",\"notes\":\"Per-device private data. Stored in I"
    "OService member variables.\",\"nt\":\"DEVICE_EXTENSION\"}]}";
static const char KT_EMPTY_JSON_33[] = "{\"error\":\"driver_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_34[] = "{\"count\":0,\"events\":[],\"ok\":true}";
static const char KT_EMPTY_JSON_35[] = "{\"active_count\":0,\"ok\":true,\"processes\":[],\"total_count\":0}";
static const char KT_EMPTY_JSON_36[] =
    "{\"error\":\"EndpointSecurity.framework not found: dlopen(/System/Library/Frameworks/EndpointSecurity.f"
    "ramework/EndpointSecurity, 0x0006): tried: '/System/Library/Frameworks/EndpointSecurity.framework/En"
    "dpointSecurity' (no such file), '/System/Volumes/Preboot/Cryptexes/OS/System/Library/Frameworks/Endp"
    "ointSecurity.framework/EndpointSecurity' (no such file), '/System/Library/Frameworks/EndpointSecurit"
    "y.framework/EndpointSecurity' (no such file, not in dyld cache)\",\"ok\":false}";
static const char KT_EMPTY_JSON_37[] =
    "{\"active\":false,\"active_processes\":0,\"event_count\":0,\"max_events\":4096,\"ok\":true,\"total_processes\":0"
    "}";
static const char KT_EMPTY_JSON_38[] = "{\"ok\":true,\"stopped\":true,\"table_unavailable\":true}";
static const char KT_EMPTY_JSON_39[] =
    "{\"channel\":{\"bytes_received\":0,\"bytes_sent\":0,\"channel_id\":1,\"direction\":\"bidirectional\",\"last_activ"
    "ity\":\"<dynamic-number>\",\"local_port\":\"0x00004101\",\"message_type\":\"es_event\",\"remote_port\":\"0x0000820"
    "1\",\"status\":\"active\"},\"ok\":true}";
static const char KT_EMPTY_JSON_40[] =
    "{\"detected_count\":0,\"events\":[],\"ok\":true,\"own_pid\":\"<dynamic-number>\",\"watch_pid\":null,\"watch_tid\":"
    "null}";
static const char KT_EMPTY_JSON_41[] = "{\"error\":\"process_id (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_42[] = "{\"error\":\"parent_pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_43[] = "{\"error\":\"process_id (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_44[] = "{\"count\":0,\"events\":[],\"ok\":true}";
static const char KT_EMPTY_JSON_45[] = "{\"channels\":[],\"count\":0,\"ok\":true}";
static const char KT_EMPTY_JSON_46[] = "{\"callbacks\":[],\"count\":0,\"ok\":true}";
static const char KT_EMPTY_JSON_47[] = "{\"error\":\"nt_routine required\",\"ok\":false}";
static const char KT_EMPTY_JSON_48[] = "{\"count\":0,\"events\":[],\"ok\":true}";
static const char KT_EMPTY_JSON_49[] =
    "{\"error\":\"callback_type required: process_notify, thread_notify, image_load_notify\",\"ok\":false}";
static const char KT_EMPTY_JSON_50[] =
    "{\"ok\":true,\"own_pid\":\"<dynamic-number>\",\"scenario\":\"Anti-cheat game launch: process created → ntdll "
    "loaded → worker thread created → process exited\",\"seeded\":{\"callback_ids\":[1,2,3],\"callbacks_registe"
    "red\":3,\"event_ids\":[1,2,3,4],\"events_fired\":4}}";
static const char KT_EMPTY_JSON_51[] =
    "{\"callbacks\":{\"active\":0,\"by_type\":{},\"total\":0},\"es\":{\"active_subscriptions\":[],\"available\":false,"
    "\""
    "entitlement\":\"com.apple.developer.endpoint-security.client\",\"events_dispatched\":0,\"events_received\":"
    "0,\"extension_installed\":false,\"last_event\":null},\"events\":{\"image\":0,\"process\":0,\"thread\":0,\"total\":"
    "0},\"ipc_channels\":0,\"ok\":true,\"translation_map\":{\"image_load_notify\":{\"es_events\":[\"ES_EVENT_TYPE_NO"
    "TIFY_EXEC\",\"ES_EVENT_TYPE_NOTIFY_MMAP\"],\"nt_callback\":\"PsSetLoadImageNotifyRoutineEx\",\"xnu_mechanism"
    "\":\"mmap(PROT_EXEC) → EndpointSecurity NOTIFY_MMAP\"},\"process_notify\":{\"es_events\":[\"ES_EVENT_TYPE_NO"
    "TIFY_EXEC\",\"ES_EVENT_TYPE_NOTIFY_EXIT\"],\"nt_callback\":\"PsSetCreateProcessNotifyRoutineEx2\",\"xnu_mech"
    "anism\":\"fork/exec → proc_info → EndpointSecurity\"},\"thread_notify\":{\"es_events\":[\"ES_EVENT_TYPE_NOTI"
    "FY_THREAD\"],\"nt_callback\":\"PsSetCreateThreadNotifyRoutineEx\",\"xnu_mechanism\":\"bsdthread_create → tas"
    "k_threads polling\"}}}";
static const char KT_EMPTY_JSON_52[] = "{\"count\":0,\"events\":[],\"ok\":true}";
static const char KT_EMPTY_JSON_53[] = "{\"error\":\"callback_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_54[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_55[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_56[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_57[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_58[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_59[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_60[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_61[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_62[] = "{\"ok\":true,\"processes\":[{\"fds\":7,\"merged\":15,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":5,\"merged\":3"
                                       "2,\"pid\":\"<dynamic-number>\",\"ports\":19},{\"fds\":8,\"merged\":16,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"fd"
                                       "s\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\",\""
                                       "ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":4,\"merged\":12,\"pid\":\"<dyna"
                                       "mic-number>\",\"ports\":0},{\"fds\":18,\"merged\":26,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":3,\"merged\":"
                                       "11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"fd"
                                       "s\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\",\""
                                       "ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dyna"
                                       "mic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":20,\"merged\":"
                                       "28,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":19,\"merged\":27,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"f"
                                       "ds\":18,\"merged\":26,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\""
                                       ",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":3,\"merged\":11,\"pid\":\"<dy"
                                       "namic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":11,\"merged"
                                       "\":19,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":4,\"merged\":12,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{\""
                                       "fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\""
                                       ",\"ports\":0},{\"fds\":4,\"merged\":12,\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":4,\"merged\":12,\"pid\":\"<dy"
                                       "namic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":5,\"merged\""
                                       ":13,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"f"
                                       "ds\":16,\"merged\":24,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\""
                                       ",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":0,\"merged\":11,\"pid\":\"<dy"
                                       "namic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merged\""
                                       ":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":11,\"merged\":19,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{\""
                                       "fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":16,"
                                       "\"merged\":24,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<d"
                                       "ynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merged"
                                       "\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":17,\"merged\":25,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":12,\"merged\":20,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":40,"
                                       "\"merged\":48,\"pid\":\"<dynamic-numbe"
                                       "r>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\""
                                       "<dynamic-number>\",\"ports\":0},{\"fds\":19,\"merged\":27,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":6,\"mer"
                                       "ged\":14,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0}"
                                       ",{\"fds\":6,\"merged\":14,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":4,"
                                       "\"merged\":12,\"pid\":\"<dynamic-numbe"
                                       "r>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":9,\"merged\":17,\"pid\":\""
                                       "<dynamic-number>\",\"ports\":0},{\"fds\":6,\"merged\":14,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":3,\"merg"
                                       "ed\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},"
                                       "{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number"
                                       ">\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merge"
                                       "d\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":8,"
                                       "\"merged\":16,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":19,\"merged\":27,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":15,\"merged\":23,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":3,\"merg"
                                       "ed\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":10,\"merged\":18,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0}"
                                       ",{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":"
                                       "20,\"merged\":28,\"pid\":\"<dynamic-numb"
                                       "er>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":18,\"merged\":26,\"pid\""
                                       ":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":8,\"me"
                                       "rged\":16,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0"
                                       "},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":"
                                       "39,\"merged\":47,\"pid\":\"<dynamic-num"
                                       "ber>\",\"ports\":0},{\"fds\":4,\"merged\":12,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\""
                                       ":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":3,\"me"
                                       "rged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":10,\"merged\":18,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":62,\"merged\":70,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":32,\"merged\":40,\"pid\":\"<dynamic-n"
                                       "umber>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":6,\"merged\":14,\"pi"
                                       "d\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":3,\""
                                       "merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\""
                                       ":0},{\"fds\":16,\"merged\":24,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-n"
                                       "umber>\",\"ports\":0},{\"fds\":14,\"merged\":22,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":3,\"merged\":11,\"p"
                                       "id\":\"<dynamic-number>\",\"ports\":0},{\"fds\":9,\"merged\":17,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":6,"
                                       "\"merged\":14,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":6,\"merged\":"
                                       "14,\"pid\":\"<dynamic-number>\",\"ports"
                                       "\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-n"
                                       "umber>\",\"ports\":0},{\"fds\":23,\"merged\":31,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"p"
                                       "id\":\"<dynamic-number>\",\"ports\":0},{\"fds\":18,\"merged\":26,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":9"
                                       ",\"merged\":17,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":16,"
                                       "\"merged\":24,\"pid\":\"<dynamic-number>\",\"por"
                                       "ts\":0},{\"fds\":5,\"merged\":13,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic"
                                       "-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":7,\"merged\":15,\""
                                       "pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":0"
                                       ",\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":"
                                       "11,\"pid\":\"<dynamic-number>\",\"port"
                                       "s\":0},{\"fds\":15,\"merged\":23,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic"
                                       "-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":3,\"merged\":11,\""
                                       "pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":3"
                                       ",\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":4,\"merged\":"
                                       "12,\"pid\":\"<dynamic-number>\",\"port"
                                       "s\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":8,\"merged\":16,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"p"
                                       "id\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":3,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":6,\"merged\":"
                                       "14,\"pid\":\"<dynamic-number>\",\"ports"
                                       "\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-n"
                                       "umber>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":26,\"merged\":34,\"p"
                                       "id\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":14"
                                       ",\"merged\":22,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":38,"
                                       "\"merged\":46,\"pid\":\"<dynamic-number>\",\"por"
                                       "ts\":0},{\"fds\":15,\"merged\":23,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynami"
                                       "c-number>\",\"ports\":0},{\"fds\":10,\"merged\":18,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":0,\"merged\":11"
                                       ",\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":4,\"merged\":12,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\""
                                       ":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\",\"po"
                                       "rts\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":8,\"merged\":16,\"pid\":\"<dynami"
                                       "c-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":"
                                       "3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\",\"por"
                                       "ts\":0},{\"fds\":8,\"merged\":16,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":7,\"merged\":15,\"pid\":\"<dynamic"
                                       "-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":10,\"merged\":18,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":"
                                       "7,\"merged\":15,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":43,"
                                       "\"merged\":51,\"pid\":\"<dynamic-number>\",\"po"
                                       "rts\":0},{\"fds\":18,\"merged\":26,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":22,\"merged\":30,\"pid\":\"<dyna"
                                       "mic-number>\",\"ports\":0},{\"fds\":7,\"merged\":15,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":7,\"merged\":1"
                                       "5,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":22,\"merged\":30,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"fd"
                                       "s\":7,\"merged\":15,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":7,"
                                       "\"merged\":15,\"pid\":\"<dynamic-number>\",\""
                                       "ports\":0},{\"fds\":7,\"merged\":15,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":5,\"merged\":13,\"pid\":\"<dyna"
                                       "mic-number>\",\"ports\":0},{\"fds\":22,\"merged\":30,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":4,\"merged\":"
                                       "12,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"fd"
                                       "s\":24,\"merged\":32,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0}"
                                       ",{\"fds\":18,\"merged\":26,\"pid\":\"<dy"
                                       "namic-number>\",\"ports\":0},{\"fds\":18,\"merged\":26,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":5,\"merged"
                                       "\":13,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{\""
                                       "fds\":4,\"merged\":12,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\""
                                       ",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":0,\"merged\":11,\"pid\":\"<dy"
                                       "namic-number>\",\"ports\":0},{\"fds\":7,\"merged\":15,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merged\""
                                       ":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"f"
                                       "ds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":20,"
                                       "\"merged\":28,\"pid\":\"<dynamic-number>\""
                                       ",\"ports\":0},{\"fds\":4,\"merged\":12,\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":18,\"merged\":26,\"pid\":\"<d"
                                       "ynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merged"
                                       "\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{\""
                                       "fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\""
                                       ",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":0,\"merged\":11,\"pid\":\"<dy"
                                       "namic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merged\""
                                       ":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"f"
                                       "ds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":15,"
                                       "\"merged\":23,\"pid\":\"<dynamic-number>\""
                                       ",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":0,\"merged\":11,\"pid\":\"<dy"
                                       "namic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merged\""
                                       ":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":25,\"merged\":33,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{\""
                                       "fds\":14,\"merged\":22,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":4,"
                                       "\"merged\":12,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<d"
                                       "ynamic-number>\",\"ports\":0},{\"fds\":32,\"merged\":40,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":3,\"merge"
                                       "d\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":5,\"merged\":13,\"pid\":\"<d"
                                       "ynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":8,\"merged"
                                       "\":16,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":15,\"merged\":23,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":13,"
                                       "\"merged\":21,\"pid\":\"<dynamic-number"
                                       ">\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":5,\"merged\":13,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merge"
                                       "d\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":4,\"merged\":12,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":18,"
                                       "\"merged\":26,\"pid\":\"<dynamic-number"
                                       ">\",\"ports\":0},{\"fds\":4,\"merged\":12,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":18,\"merg"
                                       "ed\":26,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},"
                                       "{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":25,"
                                       "\"merged\":33,\"pid\":\"<dynamic-numbe"
                                       "r>\",\"ports\":0},{\"fds\":16,\"merged\":24,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":10,\"me"
                                       "rged\":18,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0"
                                       "},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":"
                                       "3,\"merged\":11,\"pid\":\"<dynamic-numb"
                                       "er>\",\"ports\":0},{\"fds\":6,\"merged\":14,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"fds\":29,\"merged\":37,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":4,\"me"
                                       "rged\":12,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":47,\"merged\":55,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":22,\"merged\":30,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-nu"
                                       "mber>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":8,\"merged\":16,\"pid"
                                       "\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":3,\"m"
                                       "erged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":4,\"merged\":12,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":"
                                       "3,\"merged\":11,\"pid\":\"<dynamic-num"
                                       "ber>\",\"ports\":0},{\"fds\":4,\"merged\":12,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":4,\"merged\":12,\"pid\""
                                       ":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":4,\"me"
                                       "rged\":12,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":18,\"merged\":26,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":6,\"merged\":14,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":"
                                       "8,\"merged\":16,\"pid\":\"<dynamic-num"
                                       "ber>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\""
                                       ":\"<dynamic-number>\",\"ports\":0},{\"fds\":25,\"merged\":33,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":3,\"m"
                                       "erged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":11,\"merged\":19,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":10,\"merged\":18,\"pid\":\"<dynamic-n"
                                       "umber>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":3,\"merged\":11,\"pi"
                                       "d\":\"<dynamic-number>\",\"ports\":0},{\"fds\":29,\"merged\":37,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":37"
                                       ",\"merged\":45,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":"
                                       "11,\"pid\":\"<dynamic-number>\",\"port"
                                       "s\":0},{\"fds\":9,\"merged\":17,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"p"
                                       "id\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":13"
                                       ",\"merged\":21,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":"
                                       "11,\"pid\":\"<dynamic-number>\",\"port"
                                       "s\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":199,\"merged\":207,\"pid\":\"<dynam"
                                       "ic-number>\",\"ports\":0},{\"fds\":38,\"merged\":46,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":7,\"merged\":1"
                                       "5,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"fds"
                                       "\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\",\"p"
                                       "orts\":0},{\"fds\":6,\"merged\":14,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":11,\"merged\":19,\"pid\":\"<dyna"
                                       "mic-number>\",\"ports\":0},{\"fds\":7,\"merged\":15,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":3,\"merged\":1"
                                       "1,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"fds"
                                       "\":11,\"merged\":19,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\",\""
                                       "ports\":0},{\"fds\":21,\"merged\":29,\"pid\":\"<dynamic-number>\",\"ports\":0},"
                                       "{\"fds\":0,\"merged\":11,\"pid\":\"<dyn"
                                       "amic-number>\",\"ports\":0},{\"fds\":5,\"merged\":13,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":51,\"merged\""
                                       ":59,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":40,\"merged\":48,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{\""
                                       "fds\":20,\"merged\":28,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":39,"
                                       "\"merged\":47,\"pid\":\"<dynamic-number"
                                       ">\",\"ports\":0},{\"fds\":164,\"merged\":172,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":20,\"merged\":28,\"pid"
                                       "\":\"<dynamic-number>\",\"ports\":0},{\"fds\":18,\"merged\":26,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":3,\""
                                       "merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":34,\"merged\":"
                                       "42,\"pid\":\"<dynamic-number>\",\"ports"
                                       "\":0},{\"fds\":18,\"merged\":26,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":10,\"merged\":18,\"pid\":\"<dynamic"
                                       "-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":3,\"merged\":11,\""
                                       "pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":1"
                                       "4,\"merged\":22,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\",\"por"
                                       "ts\":0},{\"fds\":3,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":4,\"merged\":12,\"pid\":\"<dynamic"
                                       "-number>\",\"ports\":0},{\"fds\":215,\"merged\":223,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":3,\"merged\":1"
                                       "1,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":4,\"merged\":12,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"fds"
                                       "\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":3,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\",\"p"
                                       "orts\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":4,\"merged\":12,\"pid\":\"<dynam"
                                       "ic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":0,\"merged\":11"
                                       ",\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\""
                                       ":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\",\"po"
                                       "rts\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynami"
                                       "c-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":"
                                       "0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\",\"por"
                                       "ts\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic"
                                       "-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\""
                                       "pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":0"
                                       ",\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":"
                                       "11,\"pid\":\"<dynamic-number>\",\"port"
                                       "s\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"p"
                                       "id\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":"
                                       "11,\"pid\":\"<dynamic-number>\",\"ports"
                                       "\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-n"
                                       "umber>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pi"
                                       "d\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":0,\""
                                       "merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\""
                                       ":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-nu"
                                       "mber>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid"
                                       "\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":0,\"m"
                                       "erged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":"
                                       "0,\"merged\":11,\"pid\":\"<dynamic-num"
                                       "ber>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\""
                                       ":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"me"
                                       "rged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0"
                                       "},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":"
                                       "0,\"merged\":11,\"pid\":\"<dynamic-numb"
                                       "er>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"fds\":4,\"merged\":12,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"mer"
                                       "ged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0}"
                                       ",{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-numbe"
                                       "r>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\""
                                       "<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merg"
                                       "ed\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},"
                                       "{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number"
                                       ">\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<"
                                       "dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merge"
                                       "d\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{"
                                       "\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<d"
                                       "ynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merged"
                                       "\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,"
                                       "\"pid\":\"<dynamic-number>\",\"ports\":0},{\""
                                       "fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\""
                                       ",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":"
                                       "0},{\"fds\":0,\"merged\":11,\"pid\":\"<dy"
                                       "namic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-"
                                       "number>\",\"ports\":0},{\"fds\":0,\"merged\""
                                       ":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0},{\"f"
                                       "ds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,"
                                       "\"merged\":11,\"pid\":\"<dynamic-number>\","
                                       "\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>\",\"ports\":0}"
                                       ",{\"fds\":0,\"merged\":11,\"pid\":\"<dyn"
                                       "amic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":\"<dynamic-number>"
                                       "\",\"ports\":0},{\"fds\":0,\"merged\":"
                                       "11,\"pid\":\"<dynamic-number>\",\"ports\":0},{\"fds\":0,\"merged\":11,\"pid\":"
                                       "\"<dynamic-number>\",\"ports\":0}],\"to"
                                       "talProcesses\":412}";
static const char KT_EMPTY_JSON_63[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_64[] = "{\"ok\":true,\"pids\":[],\"tables\":[],\"totalTables\":0}";
static const char KT_EMPTY_JSON_65[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_66[] =
    "{\"host\":{\"arch\":\"aarch64\",\"os\":\"macos\"},\"ok\":true,\"probes\":{\"anonymousExecutableMapping\":{"
    "\"note\":\"An"
    "onymous RW→RX transition works\",\"ok\":true,\"stage\":\"mprotect_rx\"},\"csops\":{\"note\":\"csops works for ow"
    "n process\",\"ok\":true,\"return\":0},\"taskForPid\":{\"kr\":-1,\"note\":\"task_for_pid requires entitlement for"
    " cross-process\",\"ok\":false}},\"summary\":\"Kernel translation host capability probe complete.\"}";
static const char KT_EMPTY_JSON_67[] = "{\"error\":\"bottle_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_68[] = "{\"error\":\"bottle_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_69[] = "{\"configs\":[],\"count\":0,\"ok\":true}";
static const char KT_EMPTY_JSON_70[] = "{\"error\":\"cannot activate from state NotInstalled\",\"ok\":false}";
static const char KT_EMPTY_JSON_71[] =
    "{\"crash_count\":1,\"crash_simulated\":true,\"degraded\":[\"ES process/thread/image callbacks\",\"MACF handle"
    " operation filtering\",\"kernel-level code integrity\"],\"fallback_activated\":true,\"ok\":true,\"preserved_"
    "callbacks\":true}";
static const char KT_EMPTY_JSON_72[] =
    "{\"degraded\":[\"mac_proc_check_get_task (MACF)\",\"real-time ES event delivery\",\"kernel-assisted handle "
    "filtering\"],\"extension\":{\"activated_at\":null,\"crash_count\":0,\"entitlement\":\"com.apple.developer.endp"
    "oint-security.client\",\"fallback_active\":true,\"installed_at\":null,\"last_heartbeat\":null,\"state\":\"Deac"
    "tivated\",\"version\":\"0.1.0\"},\"fallback_active\":true,\"ok\":true}";
static const char KT_EMPTY_JSON_73[] =
    "{\"extension\":{\"activated_at\":null,\"crash_count\":0,\"entitlement\":\"com.apple.developer.endpoint-securi"
    "ty.client\",\"fallback_active\":true,\"installed_at\":1784564324418,\"last_heartbeat\":null,\"state\":\"Instal"
    "led\",\"version\":\"1.0.0\"},\"ok\":true}";
static const char KT_EMPTY_JSON_74[] =
    "{\"crash_recovery\":{\"active_callbacks_preserved\":true,\"crash_count\":0,\"degraded_capabilities\":[],\"ext"
    "ension_active\":false,\"fallback_mode\":true,\"last_crash\":null},\"entitlement_status\":\"not_active\",\"exte"
    "nsion\":{\"activated_at\":null,\"crash_count\":0,\"entitlement\":\"com.apple.developer.endpoint-security.cli"
    "ent\",\"fallback_active\":true,\"installed_at\":null,\"last_heartbeat\":null,\"state\":\"NotInstalled\",\"versio"
    "n\":\"0.1.0\"},\"ok\":true}";
static const char KT_EMPTY_JSON_75[] =
    "{\"degraded_capabilities\":[],\"fallback_active\":true,\"kernel_enhanced\":{\"code_integrity\":\"MACF mac_vno"
    "de_check_signature (<1ms latency)\",\"handle_callbacks\":\"MACF mac_proc_check_get_task (<1ms latency)\","
    "\"image_callbacks\":\"ES NOTIFY_MMAP (<1ms latency)\",\"process_callbacks\":\"ES NOTIFY_EXEC (<1ms latency)"
    "\",\"thread_callbacks\":\"ES NOTIFY_THREAD (<1ms latency)\"},\"ok\":true,\"reason\":\"extension not active or "
    "crashed\",\"user_mode_stubs\":{\"code_integrity\":\"csops bridge (0ms — userspace)\",\"handle_callbacks\":\"Wi"
    "ne handle table interception (0ms — userspace)\",\"image_callbacks\":\"module list snapshot (100ms laten"
    "cy)\",\"process_callbacks\":\"polling via proc_info (50-200ms latency)\",\"thread_callbacks\":\"task_threads"
    " polling (100ms latency)\"}}";
static const char KT_EMPTY_JSON_76[] =
    "{\"anti_cheat_registered\":0,\"bottles_configured\":0,\"crash_recovery\":{\"active_callbacks_preserved\":tru"
    "e,\"crash_count\":0,\"degraded_capabilities\":[],\"extension_active\":false,\"fallback_mode\":true,\"last_cra"
    "sh\":null},\"extension\":{\"activated_at\":null,\"crash_count\":0,\"entitlement\":\"com.apple.developer.endpoi"
    "nt-security.client\",\"fallback_active\":true,\"installed_at\":null,\"last_heartbeat\":null,\"state\":\"NotIns"
    "talled\",\"version\":\"0.1.0\"},\"ok\":true,\"performance_profiles\":0,\"phases\":{\"10_full_stack\":\"complete\","
    "\""
    "11_integration\":\"complete\",\"12_hardening\":\"complete\",\"1_tables\":\"complete\",\"2a_handle_table\":\"comple"
    "te\",\"2b_handle_bridge\":\"complete\",\"3_code_integrity\":\"complete\",\"4_apc\":\"complete\",\"5a_es_bridge\":"
    "\"c"
    "omplete\",\"5b_thread_notify\":\"complete\",\"6_handle_callbacks\":\"complete\",\"7_driver_model\":\"complete\",\""
    "8_anti_debug\":\"complete\"},\"pipelines_measured\":0,\"ready_for\":\"user-mode anti-cheat validation (Phase"
    " 9 — deferred until live integration)\",\"stats\":{\"endpoints\":94,\"modules\":11,\"tests\":361,\"total_lines"
    "\":9500},\"translations_logged\":0}";
static const char KT_EMPTY_JSON_77[] = "{\"count\":0,\"ok\":true,\"registrations\":[]}";
static const char KT_EMPTY_JSON_78[] = "{\"count\":0,\"ok\":true,\"profiles\":[]}";
static const char KT_EMPTY_JSON_79[] = "{\"error\":\"pid required\",\"ok\":false}";
static const char KT_EMPTY_JSON_80[] =
    "{\"bottleneck\":{\"latency_us\":45,\"stage\":\"ac_handler\"},\"ok\":true,\"profile\":{\"budget_us\":1000,"
    "\"passes\":"
    "true,\"path\":\"es_process_create\",\"stage_latencies\":{\"ac_handler\":45,\"es_event\":5,\"mach_ipc\":12,\"retur"
    "n\":10,\"wine_dispatch\":8},\"total_us\":80},\"stage_details\":{\"ac_handler\":45,\"es_event\":5,\"mach_ipc\":12,"
    "\"return\":10,\"wine_dispatch\":8}}";
static const char KT_EMPTY_JSON_81[] = "{\"filtered\":0,\"logs\":[],\"ok\":true,\"total\":0}";
static const char KT_EMPTY_JSON_82[] = "{\"error\":\"ac_name required\",\"ok\":false}";
static const char KT_EMPTY_JSON_83[] =
    "{\"kernel_translation\":{\"bottles_configured\":0,\"crash_count\":0,\"extension_state\":\"NotInstalled\",\"fall"
    "back_active\":true,\"modules\":{\"anti_debug\":{\"description\":\"Anti-debug/anti-tamper mitigation\",\"status"
    "\":\"active\"},\"apc\":{\"description\":\"APC delivery via ARM64 context manipulation\",\"status\":\"active\"},\"c"
    "ode_integrity\":{\"description\":\"csops→NT signing level bridge\",\"status\":\"active\"},\"driver_model\":{\"de"
    "scription\":\"WDM→IOKit driver model translation\",\"status\":\"active\"},\"es_bridge\":{\"description\":\"Endpo"
    "intSecurity→NT callback bridge\",\"status\":\"fallback\"},\"handle_callbacks\":{\"description\":\"ObRegisterCa"
    "llbacks pre/post filtering\",\"status\":\"active\"},\"handle_table\":{\"description\":\"Virtual handle table f"
    "or NtQuerySystemInformation\",\"status\":\"active\"},\"thread_notify\":{\"description\":\"task_threads polling"
    " for thread creation\",\"status\":\"active\"}},\"performance\":{\"avg_pipeline_latency_us\":0,\"budget_us\":100"
    "0,\"pipelines_measured\":0,\"within_budget\":true},\"translations_logged\":0},\"ok\":true}";
static const char KT_EMPTY_JSON_84[] =
    "{\"ok\":true,\"pipeline_results\":[{\"latency_us\":80,\"passes\":true,\"source\":\"es_process_create\"},{\"latenc"
    "y_us\":148,\"passes\":true,\"source\":\"es_image_load\"},{\"latency_us\":21,\"passes\":true,\"source\":\"handle_op"
    "eration\"}],\"runtime_doctor\":{\"avg_pipeline_latency_us\":83,\"budget_us\":1000,\"pipelines_measured\":3,\"w"
    "ithin_budget\":true},\"seeded\":{\"anti_cheat_registered\":2,\"bottles_configured\":1,\"extension_installed\""
    ":true,\"performance_profiles\":1,\"pipelines_simulated\":3,\"translations_logged\":1}}";
static const char KT_EMPTY_JSON_85[] =
    "{\"conflict_analysis\":{\"ac1\":\"EAC\",\"ac2\":\"BattlEye\",\"altitude_resolution\":\"EAC at altitude 1000, Batt"
    "lEye at altitude 2000 — lower altitude fires first\",\"conflict_free\":true,\"notes\":\"Both ACs register "
    "process/image callbacks. Altitude ordering ensures deterministic dispatch. No callback state shared "
    "between ACs.\",\"shared_callback_types\":[\"process_notify\",\"image_load_notify\"]},\"ok\":true}";
static const char KT_EMPTY_JSON_86[] =
    "{\"budget_us\":1000,\"ok\":true,\"passes_budget\":true,\"pipeline\":{\"event_source\":\"es_process_create\",\"fal"
    "lback_used\":false,\"id\":1,\"stages\":[{\"detail\":\"ES_EVENT_TYPE_NOTIFY_EXEC received\",\"latency_us\":5,\"mo"
    "dule\":\"EndpointSecurity\",\"stage\":\"ES event fired\",\"status\":\"ok\"},{\"detail\":\"mach_msg to Wine ntdll p"
    "ort\",\"latency_us\":12,\"module\":\"es_bridge\",\"stage\":\"Mach IPC send\",\"status\":\"ok\"},{\"detail\":\"Nt "
    "callb"
    "ack invocation\",\"latency_us\":8,\"module\":\"ntdll\",\"stage\":\"Wine "
    "dispatch\",\"status\":\"ok\"},{\"detail\":\"An"
    "ti-cheat DriverEntry callback\",\"latency_us\":45,\"module\":\"driver_model\",\"stage\":\"AC handler\",\"status\""
    ":\"ok\"},{\"detail\":\"IOConnectCallMethod "
    "reply\",\"latency_us\":10,\"module\":\"IOUserClient\",\"stage\":\"Result"
    " return\",\"status\":\"ok\"}],\"timestamp\":\"<dynamic-number>\",\"total_latency_us\":80,\"within_budget\":true}}";
static const char KT_EMPTY_JSON_87[] =
    "{\"filteredCount\":0,\"modules\":[],\"ok\":true,\"totalModules\":0,\"typeCounts\":{}}";
static const char KT_EMPTY_JSON_88[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_89[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_90[] = "{\"error\":\"path to Mach-O required\",\"ok\":false}";
static const char KT_EMPTY_JSON_91[] = "{\"error\":\"base_address (hex string) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_92[] =
    "{\"created\":16,\"modules\":[\"ntdll.dll\",\"kernel32.dll\",\"kernelbase.dll\",\"user32.dll\",\"game.exe\","
    "\"gameov"
    "erlayrenderer.dll\",\"d3d11.dll\",\"dxgi.dll\",\"winegstreamer.dll\",\"winemac.drv\",\"libwine.dylib\",\"libwine"
    "d3d.dylib\",\"anticheat.sys\",\"anticheat_user.dll\",\"vcruntime140.dll\",\"msvcp140.dll\"],\"ok\":true,\"pid\":\""
    "<dynamic-number>\",\"totalModules\":16}";
static const char KT_EMPTY_JSON_93[] =
    "{\"note\":\"Stub — always returns STATUS_SUCCESS. Anti-cheat calls this during init expecting success.\""
    ",\"ntApi\":\"NtSetCachedSigningLevel\",\"ntStatus\":\"STATUS_SUCCESS\",\"ok\":true}";
static const char KT_EMPTY_JSON_94[] = "{\"count\":0,\"handles\":[],\"ok\":true}";
static const char KT_EMPTY_JSON_95[] = "{\"bind_addr\":\"127.0.0.1:<port>\",\"ok\":true}";
static const char KT_EMPTY_JSON_96[] =
    "{\"bind_addr\":\"127.0.0.1:<port>\",\"ok\":true,\"running\":false,\"virtual_handles\":0}";
static const char KT_EMPTY_JSON_97[] = "{\"active_clients\":0,\"ok\":true,\"stopped\":true}";
static const char KT_EMPTY_JSON_98[] = "{\"count\":0,\"logs\":[],\"ok\":true}";
static const char KT_EMPTY_JSON_99[] =
    "{\"access_rights\":{\"PROCESS_ALL_ACCESS\":\"0x001FFFFF\",\"PROCESS_QUERY_INFORMATION\":\"0x00000400\",\"PROCES"
    "S_TERMINATE\":\"0x00000001\",\"PROCESS_VM_OPERATION\":\"0x00000008\",\"PROCESS_VM_READ\":\"0x00000010\",\"PROCES"
    "S_VM_WRITE\":\"0x00000020\"},\"kernel_enhanced\":\"mac_proc_check_get_task\",\"kernel_rationale\":\"When syste"
    "m extension is available, MACF hook catches task_for_pid from ANY process (not just Wine). Full NT-e"
    "quivalent coverage including non-Wine attackers.\",\"mechanisms\":[{\"availability\":\"kext_required\",\"des"
    "cription\":\"Hooks task_for_pid() calls — fires when any process requests task port of another\",\"detai"
    "l\":\"mac_proc_check_get_task(policy, cred, p) — returns 0 to allow, EPERM to deny. This IS the handle"
    "-open event on macOS.\",\"id\":\"mac_proc_check_get_task\",\"nt_mapping\":\"ObRegisterCallbacks(OB_OPERATION"
    "_HANDLE_CREATE)\",\"type\":\"MACF policy\"},{\"availability\":\"userspace\",\"description\":\"Hook NtOpenProcess"
    "/NtDuplicateObject in Wine ntdll — intercept before kernel\",\"detail\":\"Since Wine controls the handle"
    " table (Phase 2), it can fire callbacks whenever a handle is created to a protected process. No kern"
    "el needed.\",\"id\":\"wine_handle_callback\",\"nt_mapping\":\"ObRegisterCallbacks pre/post operation\",\"type\""
    ":\"Wine virtual\"},{\"availability\":\"system_extension\",\"description\":\"ES may see task_for_pid as a mach"
    " trap — investigate NOTIFY_MACH or custom ES client\",\"detail\":\"ES can observe but cannot modify acce"
    "ss in-flight. For detection only, no pre-operation filtering.\",\"id\":\"endpoint_security_task_for_pid\""
    ",\"nt_mapping\":\"Partial — detect but not filter\",\"type\":\"EndpointSecurity\"},{\"availability\":\"builtin\""
    ",\"description\":\"Sandbox profiles can deny task_for_pid — Apple uses this for App Store\",\"detail\":\"Ca"
    "n block access entirely but cannot strip individual access rights. Useful as failsafe.\",\"id\":\"sandbo"
    "x_extension\",\"nt_mapping\":\"Hard deny (no granularity)\",\"type\":\"macOS sandbox\"}],\"nt_equivalent\":\"ObR"
    "egisterCallbacks / ObUnRegisterCallbacks\",\"ok\":true,\"rationale\":\"Wine controls its own handle table."
    " Pre/post callbacks fire on NtOpenProcess/NtDuplicateObject. Can strip PROCESS_VM_WRITE, block entir"
    "ely, or allow. No kernel extension needed.\",\"recommended\":\"wine_handle_callback\"}";
static const char KT_EMPTY_JSON_100[] = "{\"count\":0,\"ok\":true,\"processes\":[]}";
static const char KT_EMPTY_JSON_101[] = "{\"count\":0,\"ok\":true,\"registrations\":[]}";
static const char KT_EMPTY_JSON_102[] = "{\"count\":0,\"ok\":true,\"operations\":[]}";
static const char KT_EMPTY_JSON_103[] = "{\"count\":0,\"ok\":true,\"operations\":[]}";
static const char KT_EMPTY_JSON_104[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_105[] =
    "{\"ok\":true,\"registration\":{\"active\":true,\"altitude\":1000,\"block_access_mask\":0,\"call_count\":0,\"id\":1"
    ",\"last_fired\":null,\"operations\":[\"OpenProcess\",\"DuplicateObject\"],\"post_callback\":true,\"pre_callback"
    "\":true,\"protected_pids\":[],\"registered_at\":1784564332047,\"strip_access_mask\":32},\"registration_id\":1"
    "}";
static const char KT_EMPTY_JSON_106[] =
    "{\"ok\":true,\"operation_results\":[{\"blocked\":false,\"id\":1,\"pre_status\":\"StripAccess\"},{\"blocked\":false"
    ",\"id\":2,\"pre_status\":\"Allow\"},{\"blocked\":false,\"id\":3,\"pre_status\":\"StripAccess\"}],\"scenario\":"
    "\"Anti-"
    "cheat protection: game.exe (PID 5000) protected. Cheat (PID 6000) tries PROCESS_ALL_ACCESS → strippe"
    "d/blocked. Game opens itself → allowed. Cheat tries DuplicateObject with VM_WRITE → stripped.\",\"seed"
    "ed\":{\"operations_simulated\":3,\"protected_pid\":\"<dynamic-number>\",\"registration_id\":1}}";
static const char KT_EMPTY_JSON_107[] =
    "{\"error\":\"operation required: open_process, open_thread, duplicate_object, close_handle\",\"ok\":false}";
static const char KT_EMPTY_JSON_108[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_109[] = "{\"error\":\"registration_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_110[] =
    "{\"categoryBreakdown\":{\"debug\":{\"blocked\":2,\"close\":1,\"count\":7,\"direct\":2,\"userspace\":2},\"driver\":"
    "{\""
    "blocked\":4,\"count\":10,\"userspace\":6},\"io\":{\"close\":3,\"count\":32,\"direct\":22,\"partial\":3,"
    "\"userspace\":"
    "4},\"ipc\":{\"blocked\":2,\"close\":8,\"count\":27,\"direct\":9,\"userspace\":8},\"memory\":{\"blocked\":4,"
    "\"close\":1"
    ",\"count\":27,\"direct\":15,\"partial\":1,\"userspace\":6},\"object\":{\"blocked\":1,\"close\":1,\"count\":15,"
    "\"direc"
    "t\":3,\"userspace\":10},\"process\":{\"close\":11,\"count\":47,\"direct\":19,\"notNeeded\":1,\"partial\":7,"
    "\"userspa"
    "ce\":9},\"registry\":{\"count\":26,\"userspace\":26},\"security\":{\"blocked\":4,\"close\":4,\"count\":21,"
    "\"direct\":"
    "1,\"userspace\":12},\"sync\":{\"close\":7,\"count\":44,\"direct\":15,\"partial\":1,\"userspace\":21},\"system\":{"
    "\"cl"
    "ose\":6,\"count\":17,\"direct\":4,\"partial\":1,\"userspace\":6},\"transaction\":{\"count\":21,\"userspace\":21}},"
    "\""
    "drillTargets\":[{\"affected_anticheat\":[\"EAC\",\"BattlEye\",\"Vanguard\"],\"approach\":\"Build virtual handle "
    "table in Wine that tracks all open handles. Return synthetic SystemHandleInformation from Wine handl"
    "e table + /proc/pid/fd scan.\",\"description\":\"NtQuerySystemInformation(SystemHandleInformation) -- en"
    "umerate all open handles in the system\",\"id\":\"HANDLE_ENUM\",\"roadmap_phase\":\"2A\"},{\"affected_antichea"
    "t\":[\"EAC\",\"BattlEye\",\"Vanguard\"],\"approach\":\"Cannot load kernel drivers on macOS. Replace with Endpo"
    "intSecurity system extension + MACF policy for equivalent monitoring. Stub NtLoadDriver with STATUS_"
    "ACCESS_DENIED.\",\"description\":\"NtLoadDriver / IoCreateDevice -- anti-cheat kernel driver loading has"
    " no macOS equivalent\",\"id\":\"KERNEL_DRIVER\",\"roadmap_phase\":\"11\"},{\"affected_anticheat\":[\"EAC\",\"Battl"
    "Eye\",\"Vanguard\"],\"approach\":\"Use task_threads polling or EndpointSecurity ES_EVENT_TYPE_NOTIFY_THREA"
    "D for thread creation monitoring. Higher latency than NT kernel callback.\",\"description\":\"PsSetCreat"
    "eThreadNotifyRoutineEx -- no thread creation callback available on macOS\",\"id\":\"THREAD_NOTIFY\",\"road"
    "map_phase\":\"11\"},{\"affected_anticheat\":[\"EAC\",\"BattlEye\",\"Vanguard\"],\"approach\":\"MACF mac_proc_check"
    "_get_task provides partial equivalent for task port access control. Full implementation requires kex"
    "t. EndpointSecurity cannot intercept handle operations.\",\"description\":\"ObRegisterCallbacks -- handl"
    "e operation callback for anti-tamper protection\",\"id\":\"HANDLE_CALLBACK\",\"roadmap_phase\":\"11\"},{\"affe"
    "cted_anticheat\":[\"EAC\",\"BattlEye\",\"Vanguard\"],\"approach\":\"csops(pid, CS_OPS_GETSIGNINGINFO) provides"
    " signing level. For code integrity: combine csops with EndpointSecurity exec monitoring and IOKit co"
    "de signing APIs. NtSetCachedSigningLevel blocked by SIP.\",\"description\":\"NtGetCachedSigningLevel / c"
    "ode integrity verification -- verify executable signatures\",\"id\":\"CODE_INTEGRITY\",\"roadmap_phase\":\"9"
    "\"}],\"endpointSecurity\":[{\"available_for_wine\":true,\"es_event\":\"ES_EVENT_TYPE_NOTIFY_EXEC\",\"notes\":\"M"
    "aps NT image load notification to macOS exec event. EndpointSecurity system extension can intercept "
    "all exec calls.\",\"nt_callback\":\"PsSetLoadImageNotifyRoutineEx\"},{\"available_for_wine\":true,\"es_event"
    "\":\"ES_EVENT_TYPE_NOTIFY_FORK\",\"notes\":\"Maps NT process creation notification to macOS fork. Can moni"
    "tor child process creation for anti-cheat process tree validation.\",\"nt_callback\":\"PsSetCreateProces"
    "sNotifyRoutineEx2\"},{\"available_for_wine\":true,\"es_event\":\"ES_EVENT_TYPE_NOTIFY_MMAP\",\"notes\":\"Maps "
    "NT minifilter memory events to macOS mmap notifications. Can detect code injection and memory manipu"
    "lation.\",\"nt_callback\":\"FltRegisterFilter (memory filter)\"},{\"available_for_wine\":true,\"es_event\":\"E"
    "S_EVENT_TYPE_NOTIFY_SIGNAL\",\"notes\":\"Maps NT debug exceptions to macOS signal delivery. Enables anti"
    "-debug detection and exception routing to Wine debugger.\",\"nt_callback\":\"Debug exception routing\"},{"
    "\"available_for_wine\":false,\"es_event\":\"mac_proc_check_get_task\",\"notes\":\"MACF policy for task port a"
    "ccess control. Maps to NT handle callback registration. Requires kext -- not available as system ext"
    "ension.\",\"nt_callback\":\"ObRegisterCallbacks\"},{\"available_for_wine\":false,\"es_event\":\"mac_vnode_chec"
    "k_signature\",\"notes\":\"MACF policy for code signature checks. Maps to NT code integrity. Requires kex"
    "t for full implementation. Partial via csops.\",\"nt_callback\":\"Code integrity verification\"},{\"availa"
    "ble_for_wine\":false,\"es_event\":\"mac_proc_check_syscall_unix\",\"notes\":\"MACF policy for syscall interc"
    "eption. Could emulate NT kernel callback for syscall monitoring. Requires kext.\",\"nt_callback\":\"Sysc"
    "all filtering hook\"},{\"available_for_wine\":true,\"es_event\":\"task_set_exception_ports\",\"notes\":\"Mach "
    "exception port routing. Maps EXC_BAD_ACCESS -> EXCEPTION_ACCESS_VIOLATION, EXC_BREAKPOINT -> EXCEPTI"
    "ON_BREAKPOINT. Available from userspace via mach_msg.\",\"nt_callback\":\"Kernel exception handler regis"
    "tration\"}],\"executiveFunctions\":[{\"category\":\"Memory Manager\",\"mapped\":3,\"notes\":\"Most MmXxx are ker"
    "nel-mode only (IOMemoryDescriptor). Userspace: mprotect, mach_vm_read/write for MmCopyMemory, mach_v"
    "m_region for MmIsAddressValid.\",\"nt_prefix\":\"MmXxx\",\"quality\":\"Blocked\",\"total_functions\":85},{\"cate"
    "gory\":\"Process/Thread\",\"mapped\":18,\"notes\":\"Core PsGetCurrent* and PsCreateSystemThread have direct "
    "XNU pairs. Anti-cheat critical: PsSetCreateProcessNotifyRoutineEx2, PsSetLoadImageNotifyRoutineEx ne"
    "ed EndpointSecurity. PsSetCreateThreadNotifyRoutineEx has no XNU equivalent.\",\"nt_prefix\":\"PsXxx\",\"q"
    "uality\":\"Partial\",\"total_functions\":66},{\"category\":\"I/O Manager\",\"mapped\":12,\"notes\":\"IRP model has"
    " no XNU equivalent. Some IoCreateFile/IoDeviceIoControlFile map to open/ioctl. IoCreateNotificationE"
    "vent -> kqueue+EVFILT_USER. Most are kernel-only.\",\"nt_prefix\":\"IoXxx\",\"quality\":\"Blocked\",\"total_fu"
    "nctions\":174},{\"category\":\"Object Manager\",\"mapped\":8,\"notes\":\"ObCloseHandle -> close/mach_port_deal"
    "locate. ObRegisterCallbacks is anti-cheat critical -- needs MACF or EndpointSecurity. Others are Win"
    "e handle table operations.\",\"nt_prefix\":\"ObXxx\",\"quality\":\"Partial\",\"total_functions\":15},{\"category"
    "\":\"Security\",\"mapped\":5,\"notes\":\"SeAccessCheck -> access()/mac_vnode_check_access. SeSinglePrivilege"
    "Check -> mac_priv_check. Others are Wine-internal SECURITY_DESCRIPTOR management.\",\"nt_prefix\":\"SeXx"
    "x\",\"quality\":\"Close\",\"total_functions\":7},{\"category\":\"Configuration\",\"mapped\":6,\"notes\":"
    "\"CmRegister"
    "Callback has no macOS equivalent -- no registry notification. CmUnRegisterCallback and others are Wi"
    "ne stubs.\",\"nt_prefix\":\"CmXxx\",\"quality\":\"Blocked\",\"total_functions\":9},{\"category\":\"Executive Libra"
    "ry\",\"mapped\":25,\"notes\":\"ExAllocatePool -> malloc. ExInitializeFastMutex/PushLock -> os_unfair_lock."
    " ExInitializeResourceLite -> pthread_rwlock. ExUuidCreate -> uuid_generate. ExXxxTimer -> mk_timer.\""
    ",\"nt_prefix\":\"ExXxx\",\"quality\":\"Close\",\"total_functions\":104},{\"category\":\"Core "
    "Kernel\",\"mapped\":22,"
    "\"notes\":\"KeInitializeSpinLock -> os_unfair_lock. KeInitializeEvent/Mutex/Semaphore/Timer have direct"
    " XNU pairs. KeWaitForSingleObject -> ulock_wait/kevent. KeMemoryBarrier -> __sync_synchronize. KeBug"
    "Check -> panic_with_data (kernel only).\",\"nt_prefix\":\"KeXxx\",\"quality\":\"Close\",\"total_functions\":60}"
    ",{\"category\":\"Run-Time Library\",\"mapped\":20,\"notes\":\"RtlCopyMemory -> memcpy, RtlMoveMemory -> memmo"
    "ve, RtlZeroMemory -> bzero. RtlSecureZeroMemory -> explicit_bzero. RtlStringFromGUID -> uuid_unparse"
    ". ~280 remaining are pure userspace computation.\",\"nt_prefix\":\"RtlXxx\",\"quality\":\"Direct\",\"total_fun"
    "ctions\":300},{\"category\":\"Power Manager\",\"mapped\":0,\"notes\":\"All 57 PoXxx are kernel-mode only (IOPM"
    "PowerSource/RootDomain). Wine returns STATUS_SUCCESS for all. Not anti-cheat relevant.\",\"nt_prefix\":"
    "\"PoXxx\",\"quality\":\"Blocked\",\"total_functions\":57},{\"category\":\"HAL\",\"mapped\":0,\"notes\":\"All 24 "
    "HalXx"
    "x are kernel-mode only (Apple Silicon pexpert). Not relevant for Wine userspace.\",\"nt_prefix\":\"HalXx"
    "x\",\"quality\":\"Blocked\",\"total_functions\":24}],\"host\":{\"arch\":\"aarch64\",\"os\":\"macos\"},"
    "\"nextActions\":["
    "\"Phase 5A: EndpointSecurity bridge for process/thread/image-load callbacks\",\"Phase 5B: Thread notifi"
    "cation via task_threads polling\",\"Phase 6: ObRegisterCallbacks equivalent via MACF/Wine handle callb"
    "ack\"],\"objectTypeCoverage\":{\"blocked\":0,\"close\":5,\"direct\":10,\"not_needed\":0,\"partial\":0,\"total\":"
    "21,"
    "\"userspace\":6},\"ok\":true,\"structCoverage\":{\"blocked\":5,\"close\":11,\"direct\":3,\"not_needed\":0,"
    "\"partial"
    "\":1,\"total\":24,\"userspace\":4},\"summary\":\"Phase 1 complete: 294 NT syscalls (90 direct), 24 structs, "
    "21 object types, 5 drill targets. Executive: 11 categories, 8 EndpointSecurity events.\",\"syscallCove"
    "rage\":{\"blocked\":17,\"close\":42,\"direct\":90,\"not_needed\":1,\"partial\":13,\"total\":294,\"userspace\":131}"
    ","
    "\"translationReady\":true}";
static const char KT_EMPTY_JSON_111[] = "{\"error\":\"from_snapshot id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_112[] = "{\"error\":\"watcher_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_113[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_114[] = "{\"error\":\"watcher_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_115[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_116[] = "{\"count\":0,\"deltas\":[],\"ok\":true}";
static const char KT_EMPTY_JSON_117[] = "{\"count\":0,\"ok\":true,\"watchers\":[]}";
static const char KT_EMPTY_JSON_118[] =
    "{\"fallback\":\"proc_info_delta\",\"fallback_rationale\":\"proc_info gives thread count but not TIDs. Usefu"
    "l as lightweight delta trigger before doing full task_threads scan.\",\"mechanisms\":[{\"available_on_li"
    "nux\":false,\"available_on_macos\":true,\"description\":\"Poll task_threads() for thread list changes betw"
    "een snapshots\",\"id\":\"task_threads\",\"nt_equivalent\":\"PsSetCreateThreadNotifyRoutineEx (emulated)\",\"re"
    "liability\":\"high\",\"requires_entitlement\":false,\"xnu_api\":\"task_threads(task, &threads, &count) — Mac"
    "h trap\"},{\"available_on_linux\":false,\"available_on_macos\":true,\"description\":\"Monitor proc_info thre"
    "ad count delta between polls\",\"id\":\"proc_info_delta\",\"nt_equivalent\":\"PsSetCreateThreadNotifyRoutine"
    "Ex (emulated)\",\"reliability\":\"medium\",\"requires_entitlement\":false,\"xnu_api\":\"proc_pidinfo(PROC_PIDT"
    "ASKINFO) — BSD syscall\"},{\"available_on_linux\":false,\"available_on_macos\":true,\"description\":\"mach_p"
    "ort_request_notification on task port for thread lifecycle\",\"id\":\"mach_port_notification\",\"nt_equiva"
    "lent\":\"PsSetCreateThreadNotifyRoutineEx (native Mach)\",\"reliability\":\"low\",\"requires_entitlement\":fa"
    "lse,\"xnu_api\":\"mach_port_request_notification(task, port, type, sync, port_notify) — Mach IPC\"},{\"av"
    "ailable_on_linux\":false,\"available_on_macos\":true,\"description\":\"task_set_exception_ports to interce"
    "pt thread creation exceptions\",\"id\":\"exception_port\",\"nt_equivalent\":\"KeInitializeApc + thread attac"
    "h callback (partial)\",\"reliability\":\"low\",\"requires_entitlement\":true,\"xnu_api\":\"task_set_exception_"
    "ports(task, mask, port, behavior, flavor) — Mach IPC\"}],\"ok\":true,\"rationale\":\"task_threads() gives "
    "direct thread list with TID enumeration. Highest reliability, no entitlements needed. Poll interval "
    "50-200ms for game-acceptable latency.\",\"recommended\":\"task_threads\"}";
static const char KT_EMPTY_JSON_119[] = "{\"error\":\"watcher_id required\",\"ok\":false}";
static const char KT_EMPTY_JSON_120[] =
    "{\"ok\":true,\"poll_result\":{\"delta\":{\"created\":[],\"created_count\":0,\"exited\":[],\"exited_count\":0,"
    "\"from"
    "_snapshot\":2,\"ok\":true,\"pid\":\"<dynamic-number>\",\"timestamp\":\"<dynamic-number>\",\"to_snapshot\":3},"
    "\"ok\""
    ":true,\"snapshot_id\":3,\"watcher_id\":1},\"scenario\":\"Thread watcher on own pid 21724 — two snapshots, o"
    "ne delta, notification config, one poll\",\"seeded\":{\"delta_created\":0,\"delta_exited\":0,\"snapshot_ids\""
    ":[1,2],\"snapshots\":2,\"watcher_ids\":[1,2],\"watchers\":2}}";
static const char KT_EMPTY_JSON_121[] = "{\"error\":\"pid (u32) required\",\"ok\":false}";
static const char KT_EMPTY_JSON_122[] = "{\"error\":\"watcher_id required\",\"ok\":false}";

static const KtEmptyResponse KT_EMPTY_RESPONSES[] = {
    {"POST", "/kernel-translation/anti-debug/add-sanitize-rule", KT_EMPTY_JSON_0},
    {"POST", "/kernel-translation/anti-debug/check-results", KT_EMPTY_JSON_1},
    {"POST", "/kernel-translation/anti-debug/filesystem-check", KT_EMPTY_JSON_2},
    {"POST", "/kernel-translation/anti-debug/full-breakpoint-map", KT_EMPTY_JSON_3},
    {"POST", "/kernel-translation/anti-debug/hw-breakpoint-map", KT_EMPTY_JSON_4},
    {"POST", "/kernel-translation/anti-debug/module-sanitize", KT_EMPTY_JSON_5},
    {"POST", "/kernel-translation/anti-debug/run-all-checks", KT_EMPTY_JSON_6},
    {"POST", "/kernel-translation/anti-debug/seed-demo", KT_EMPTY_JSON_7},
    {"POST", "/kernel-translation/anti-debug/simulate-check", KT_EMPTY_JSON_8},
    {"POST", "/kernel-translation/anti-debug/status-survey", KT_EMPTY_JSON_9},
    {"POST", "/kernel-translation/anti-debug/timing-analysis", KT_EMPTY_JSON_10},
    {"POST", "/kernel-translation/apc/allocate-trampoline", KT_EMPTY_JSON_11},
    {"POST", "/kernel-translation/apc/get-thread-context", KT_EMPTY_JSON_12},
    {"POST", "/kernel-translation/apc/inject-sequence", KT_EMPTY_JSON_13},
    {"POST", "/kernel-translation/apc/queue", KT_EMPTY_JSON_14},
    {"POST", "/kernel-translation/apc/queue-status", KT_EMPTY_JSON_15},
    {"POST", "/kernel-translation/apc/set-thread-context", KT_EMPTY_JSON_16},
    {"POST", "/kernel-translation/apc/suspend-thread", KT_EMPTY_JSON_17},
    {"POST", "/kernel-translation/apc/test-alert", KT_EMPTY_JSON_18},
    {"POST", "/kernel-translation/apc/trampoline-status", KT_EMPTY_JSON_19},
    {"POST", "/kernel-translation/apc/wait-alertable", KT_EMPTY_JSON_20},
    {"POST", "/kernel-translation/driver/create-device", KT_EMPTY_JSON_21},
    {"POST", "/kernel-translation/driver/decode-ioctl", KT_EMPTY_JSON_22},
    {"POST", "/kernel-translation/driver/dispatch-irp", KT_EMPTY_JSON_23},
    {"POST", "/kernel-translation/driver/extension-template", KT_EMPTY_JSON_24},
    {"POST", "/kernel-translation/driver/list", KT_EMPTY_JSON_25},
    {"POST", "/kernel-translation/driver/list-devices", KT_EMPTY_JSON_26},
    {"POST", "/kernel-translation/driver/list-ioctls", KT_EMPTY_JSON_27},
    {"POST", "/kernel-translation/driver/list-irps", KT_EMPTY_JSON_28},
    {"POST", "/kernel-translation/driver/load", KT_EMPTY_JSON_29},
    {"POST", "/kernel-translation/driver/register-ioctl", KT_EMPTY_JSON_30},
    {"POST", "/kernel-translation/driver/seed-demo", KT_EMPTY_JSON_31},
    {"POST", "/kernel-translation/driver/type-mapping-survey", KT_EMPTY_JSON_32},
    {"POST", "/kernel-translation/driver/unload", KT_EMPTY_JSON_33},
    {"GET", "/kernel-translation/es-live/events", KT_EMPTY_JSON_34},
    {"GET", "/kernel-translation/es-live/processes", KT_EMPTY_JSON_35},
    {"POST", "/kernel-translation/es-live/start", KT_EMPTY_JSON_36},
    {"GET", "/kernel-translation/es-live/status", KT_EMPTY_JSON_37},
    {"POST", "/kernel-translation/es-live/stop", KT_EMPTY_JSON_38},
    {"POST", "/kernel-translation/es/create-ipc-channel", KT_EMPTY_JSON_39},
    {"POST", "/kernel-translation/es/detect-events", KT_EMPTY_JSON_40},
    {"POST", "/kernel-translation/es/fire-image-event", KT_EMPTY_JSON_41},
    {"POST", "/kernel-translation/es/fire-process-event", KT_EMPTY_JSON_42},
    {"POST", "/kernel-translation/es/fire-thread-event", KT_EMPTY_JSON_43},
    {"POST", "/kernel-translation/es/image-events", KT_EMPTY_JSON_44},
    {"POST", "/kernel-translation/es/ipc-channels", KT_EMPTY_JSON_45},
    {"POST", "/kernel-translation/es/list-callbacks", KT_EMPTY_JSON_46},
    {"POST", "/kernel-translation/es/nt-callback-bridge", KT_EMPTY_JSON_47},
    {"POST", "/kernel-translation/es/process-events", KT_EMPTY_JSON_48},
    {"POST", "/kernel-translation/es/register-callback", KT_EMPTY_JSON_49},
    {"POST", "/kernel-translation/es/seed-demo", KT_EMPTY_JSON_50},
    {"POST", "/kernel-translation/es/status", KT_EMPTY_JSON_51},
    {"POST", "/kernel-translation/es/thread-events", KT_EMPTY_JSON_52},
    {"POST", "/kernel-translation/es/unregister-callback", KT_EMPTY_JSON_53},
    {"POST", "/kernel-translation/handle/close", KT_EMPTY_JSON_54},
    {"POST", "/kernel-translation/handle/create", KT_EMPTY_JSON_55},
    {"POST", "/kernel-translation/handle/duplicate", KT_EMPTY_JSON_56},
    {"POST", "/kernel-translation/handle/enumerate", KT_EMPTY_JSON_57},
    {"POST", "/kernel-translation/handle/enumerate-fds", KT_EMPTY_JSON_58},
    {"POST", "/kernel-translation/handle/enumerate-ports", KT_EMPTY_JSON_59},
    {"POST", "/kernel-translation/handle/query", KT_EMPTY_JSON_60},
    {"POST", "/kernel-translation/handle/seed-demo", KT_EMPTY_JSON_61},
    {"POST", "/kernel-translation/handle/snapshot-all", KT_EMPTY_JSON_62},
    {"POST", "/kernel-translation/handle/system-info", KT_EMPTY_JSON_63},
    {"POST", "/kernel-translation/handle/table-status", KT_EMPTY_JSON_64},
    {"POST", "/kernel-translation/handle/unified-snapshot", KT_EMPTY_JSON_65},
    {"POST", "/kernel-translation/host-probe", KT_EMPTY_JSON_66},
    {"POST", "/kernel-translation/integration/bottle-configure", KT_EMPTY_JSON_67},
    {"POST", "/kernel-translation/integration/bottle-get-config", KT_EMPTY_JSON_68},
    {"POST", "/kernel-translation/integration/bottle-list-configs", KT_EMPTY_JSON_69},
    {"POST", "/kernel-translation/integration/extension-activate", KT_EMPTY_JSON_70},
    {"POST", "/kernel-translation/integration/extension-crash", KT_EMPTY_JSON_71},
    {"POST", "/kernel-translation/integration/extension-deactivate", KT_EMPTY_JSON_72},
    {"POST", "/kernel-translation/integration/extension-install", KT_EMPTY_JSON_73},
    {"POST", "/kernel-translation/integration/extension-status", KT_EMPTY_JSON_74},
    {"POST", "/kernel-translation/integration/fallback-mode", KT_EMPTY_JSON_75},
    {"POST", "/kernel-translation/integration/full-stack-status", KT_EMPTY_JSON_76},
    {"POST", "/kernel-translation/integration/list-multi-ac", KT_EMPTY_JSON_77},
    {"POST", "/kernel-translation/integration/list-performance", KT_EMPTY_JSON_78},
    {"POST", "/kernel-translation/integration/log-translation", KT_EMPTY_JSON_79},
    {"POST", "/kernel-translation/integration/performance-profile", KT_EMPTY_JSON_80},
    {"POST", "/kernel-translation/integration/query-translation-log", KT_EMPTY_JSON_81},
    {"POST", "/kernel-translation/integration/register-multi-ac", KT_EMPTY_JSON_82},
    {"POST", "/kernel-translation/integration/runtime-doctor", KT_EMPTY_JSON_83},
    {"POST", "/kernel-translation/integration/seed-demo", KT_EMPTY_JSON_84},
    {"POST", "/kernel-translation/integration/simulate-conflict", KT_EMPTY_JSON_85},
    {"POST", "/kernel-translation/integration/simulate-pipeline", KT_EMPTY_JSON_86},
    {"POST", "/kernel-translation/integrity/list-modules", KT_EMPTY_JSON_87},
    {"POST", "/kernel-translation/integrity/query-process-signing", KT_EMPTY_JSON_88},
    {"POST", "/kernel-translation/integrity/query-signing-level", KT_EMPTY_JSON_89},
    {"POST", "/kernel-translation/integrity/register-macho-module", KT_EMPTY_JSON_90},
    {"POST", "/kernel-translation/integrity/register-pe-module", KT_EMPTY_JSON_91},
    {"POST", "/kernel-translation/integrity/seed-demo", KT_EMPTY_JSON_92},
    {"POST", "/kernel-translation/integrity/set-cached-signing-level", KT_EMPTY_JSON_93},
    {"GET", "/kernel-translation/ipc/handles", KT_EMPTY_JSON_94},
    {"POST", "/kernel-translation/ipc/start", KT_EMPTY_JSON_95},
    {"GET", "/kernel-translation/ipc/status", KT_EMPTY_JSON_96},
    {"POST", "/kernel-translation/ipc/stop", KT_EMPTY_JSON_97},
    {"POST", "/kernel-translation/ob/access-log", KT_EMPTY_JSON_98},
    {"POST", "/kernel-translation/ob/capability-survey", KT_EMPTY_JSON_99},
    {"POST", "/kernel-translation/ob/list-protected", KT_EMPTY_JSON_100},
    {"POST", "/kernel-translation/ob/list-registrations", KT_EMPTY_JSON_101},
    {"POST", "/kernel-translation/ob/post-operations", KT_EMPTY_JSON_102},
    {"POST", "/kernel-translation/ob/pre-operations", KT_EMPTY_JSON_103},
    {"POST", "/kernel-translation/ob/protect-process", KT_EMPTY_JSON_104},
    {"POST", "/kernel-translation/ob/register-callback", KT_EMPTY_JSON_105},
    {"POST", "/kernel-translation/ob/seed-demo", KT_EMPTY_JSON_106},
    {"POST", "/kernel-translation/ob/simulate-operation", KT_EMPTY_JSON_107},
    {"POST", "/kernel-translation/ob/unprotect-process", KT_EMPTY_JSON_108},
    {"POST", "/kernel-translation/ob/unregister-callback", KT_EMPTY_JSON_109},
    {"POST", "/kernel-translation/probe", KT_EMPTY_JSON_110},
    {"POST", "/kernel-translation/thread/compute-delta", KT_EMPTY_JSON_111},
    {"POST", "/kernel-translation/thread/configure-notifications", KT_EMPTY_JSON_112},
    {"POST", "/kernel-translation/thread/create-watcher", KT_EMPTY_JSON_113},
    {"POST", "/kernel-translation/thread/destroy-watcher", KT_EMPTY_JSON_114},
    {"POST", "/kernel-translation/thread/info", KT_EMPTY_JSON_115},
    {"POST", "/kernel-translation/thread/list-deltas", KT_EMPTY_JSON_116},
    {"POST", "/kernel-translation/thread/list-watchers", KT_EMPTY_JSON_117},
    {"POST", "/kernel-translation/thread/mechanism-survey", KT_EMPTY_JSON_118},
    {"POST", "/kernel-translation/thread/poll-watcher", KT_EMPTY_JSON_119},
    {"POST", "/kernel-translation/thread/seed-demo", KT_EMPTY_JSON_120},
    {"POST", "/kernel-translation/thread/snapshot", KT_EMPTY_JSON_121},
    {"POST", "/kernel-translation/thread/watcher-status", KT_EMPTY_JSON_122},
};

#endif
