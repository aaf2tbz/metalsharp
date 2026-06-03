use super::handle_table::{HandleBackend, NtObjectType, HANDLE_TABLES};
use serde_json::{json, Map, Value};
use std::collections::BTreeMap;

#[cfg(target_os = "macos")]
use libc::c_void;

const PROC_INFO_CALL_LISTPIDS: u32 = 1;
const PROC_INFO_CALL_PIDINFO: u32 = 2;
const PROC_INFO_CALL_PIDFDINFO: u32 = 3;
const PROC_ALL_PIDS: u32 = 1;
const PROC_PIDLISTFDS: u32 = 1;
const PROC_PIDFDINFO: u32 = 2;
const PROX_FDTYPE: u32 = 0;
const PROX_FDTYPE_VNODE: u32 = 1;
const PROX_FDTYPE_SOCKET: u32 = 2;
const PROX_FDTYPE_PSHX: u32 = 3;
const PROX_FDTYPE_PIPE: u32 = 4;
const PROX_FDTYPE_KQUEUE: u32 = 8;
const PROX_FDTYPE_ATALK: u32 = 9;

const SOCK_STREAM: i32 = 1;
const SOCK_DGRAM: i32 = 2;

#[repr(C)]
struct ProcFdInfo {
    fd: i32,
    fd_type: u32,
}

const FD_INFO_SIZE: usize = 8;

pub fn handle_enumerate_fds(body: &Map<String, Value>) -> Value {
    #[cfg(target_os = "macos")]
    {
        let pid = match body.get("pid").and_then(|v| v.as_u64()) {
            Some(p) if p <= u32::MAX as u64 => p as u32,
            _ => return json!({"ok": false, "error": "pid (u32) required"}),
        };
        let merge = body.get("merge").and_then(|v| v.as_bool()).unwrap_or(false);
        let fake_missing = body.get("fake_missing").and_then(|v| v.as_bool()).unwrap_or(true);

        let fd_list = enumerate_fds(pid);
        match fd_list {
            Ok(fds) => {
                let categorized = categorize_fds(&fds);
                let summary = fd_summary(&categorized);

                if merge {
                    let merged = merge_fds_into_table(pid, &categorized, fake_missing);
                    json!({
                        "ok": true,
                        "pid": pid,
                        "source": "proc_pidinfo",
                        "fdCount": fds.len(),
                        "summary": summary,
                        "mergedCount": merged,
                        "merged": true,
                    })
                } else {
                    json!({
                        "ok": true,
                        "pid": pid,
                        "source": "proc_pidinfo",
                        "fdCount": fds.len(),
                        "summary": summary,
                        "fds": categorized.iter().map(|(fd, info)| {
                            json!({
                                "fd": fd,
                                "fdType": info.fd_type_name(),
                                "inferredNtType": info.inferred_nt_type().type_name(),
                                "accessMask": format!("0x{:08X}", info.access_mask()),
                                "backend": info.backend_json(),
                            })
                        }).collect::<Vec<_>>(),
                    })
                }
            },
            Err(e) => json!({"ok": false, "error": format!("fd enumeration failed: {}", e)}),
        }
    }

    #[cfg(not(unix))]
    {
        let _ = body;
        json!({"ok": false, "error": "fd enumeration requires macOS"})
    }
}

pub fn handle_enumerate_ports(body: &Map<String, Value>) -> Value {
    #[cfg(target_os = "macos")]
    {
        let pid = match body.get("pid").and_then(|v| v.as_u64()) {
            Some(p) if p <= u32::MAX as u64 => p as u32,
            _ => return json!({"ok": false, "error": "pid (u32) required"}),
        };
        let merge = body.get("merge").and_then(|v| v.as_bool()).unwrap_or(false);

        let port_list = enumerate_mach_ports(pid);
        match port_list {
            Ok(ports) => {
                let categorized = categorize_ports(&ports);
                let summary = port_summary(&categorized);

                if merge {
                    let merged = merge_ports_into_table(pid, &categorized);
                    json!({
                        "ok": true,
                        "pid": pid,
                        "source": "mach_port_names",
                        "portCount": ports.len(),
                        "summary": summary,
                        "mergedCount": merged,
                        "merged": true,
                    })
                } else {
                    json!({
                        "ok": true,
                        "pid": pid,
                        "source": "mach_port_names",
                        "portCount": ports.len(),
                        "summary": summary,
                        "ports": categorized.iter().map(|(name, info)| {
                            json!({
                                "name": format!("0x{:08X}", name),
                                "rights": info.rights_str(),
                                "inferredNtType": info.inferred_nt_type().type_name(),
                                "backend": info.backend_json(),
                            })
                        }).collect::<Vec<_>>(),
                    })
                }
            },
            Err(e) => json!({"ok": false, "error": format!("port enumeration failed: {}", e)}),
        }
    }

    #[cfg(not(unix))]
    {
        let _ = body;
        json!({"ok": false, "error": "port enumeration requires macOS"})
    }
}

pub fn handle_unified_snapshot(body: &Map<String, Value>) -> Value {
    #[cfg(target_os = "macos")]
    {
        let pid = match body.get("pid").and_then(|v| v.as_u64()) {
            Some(p) if p <= u32::MAX as u64 => p as u32,
            _ => return json!({"ok": false, "error": "pid (u32) required"}),
        };
        let fake_missing = body.get("fake_missing").and_then(|v| v.as_bool()).unwrap_or(true);

        let fd_result = enumerate_fds(pid);
        let port_result = enumerate_mach_ports(pid);

        let fd_count = fd_result.as_ref().map(|f| f.len()).unwrap_or(0);
        let port_count = port_result.as_ref().map(|p| p.len()).unwrap_or(0);

        let mut merged_total = 0;

        if let Ok(fds) = &fd_result {
            let categorized = categorize_fds(fds);
            merged_total += merge_fds_into_table(pid, &categorized, fake_missing);
        }

        if let Ok(ports) = &port_result {
            let categorized = categorize_ports(ports);
            merged_total += merge_ports_into_table(pid, &categorized);
        }

        if fake_missing {
            merged_total += inject_fake_wine_handles(pid);
        }

        let tables = HANDLE_TABLES.lock().unwrap();
        let table = tables.get(&pid);
        let (virtual_count, type_counts) = match table {
            Some(t) => (t.handle_count(), t.type_counts()),
            None => (0, BTreeMap::new()),
        };

        json!({
            "ok": true,
            "pid": pid,
            "sources": {
                "fds": json!({"count": fd_count, "status": if fd_result.is_ok() { "ok" } else { "error" }, "error": fd_result.as_ref().err()}),
                "machPorts": json!({"count": port_count, "status": if port_result.is_ok() { "ok" } else { "error" }, "error": port_result.as_ref().err()}),
            },
            "mergedIntoVirtualTable": merged_total,
            "virtualHandleTable": {
                "totalHandles": virtual_count,
                "typeCounts": type_counts,
            },
            "systemHandleInformation": table.map(|t| t.to_system_handle_information()),
        })
    }

    #[cfg(not(unix))]
    {
        let _ = body;
        json!({"ok": false, "error": "unified snapshot requires macOS"})
    }
}

pub fn handle_snapshot_all(body: &Map<String, Value>) -> Value {
    #[cfg(target_os = "macos")]
    {
        let fake_missing = body.get("fake_missing").and_then(|v| v.as_bool()).unwrap_or(true);
        let pids = list_all_pids();
        match pids {
            Ok(pid_list) => {
                let mut results = Vec::new();
                for pid in &pid_list {
                    let fd_result = enumerate_fds(*pid);
                    let port_result = enumerate_mach_ports(*pid);

                    let fd_count = fd_result.as_ref().map(|f| f.len()).unwrap_or(0);
                    let port_count = port_result.as_ref().map(|p| p.len()).unwrap_or(0);

                    let mut merged = 0;
                    if let Ok(fds) = &fd_result {
                        let cat = categorize_fds(fds);
                        merged += merge_fds_into_table(*pid, &cat, fake_missing);
                    }
                    if let Ok(ports) = &port_result {
                        let cat = categorize_ports(ports);
                        merged += merge_ports_into_table(*pid, &cat);
                    }
                    if fake_missing {
                        merged += inject_fake_wine_handles(*pid);
                    }

                    results.push(json!({
                        "pid": pid,
                        "fds": fd_count,
                        "ports": port_count,
                        "merged": merged,
                    }));
                }
                json!({
                    "ok": true,
                    "totalProcesses": pid_list.len(),
                    "processes": results,
                })
            },
            Err(e) => json!({"ok": false, "error": format!("proc_listpids failed: {}", e)}),
        }
    }

    #[cfg(not(unix))]
    {
        let _ = body;
        json!({"ok": false, "error": "snapshot-all requires macOS"})
    }
}

#[derive(Debug)]
struct FdInfo {
    fd: i32,
    fd_type: u32,
}

impl FdInfo {
    fn fd_type_name(&self) -> &'static str {
        match self.fd_type {
            PROX_FDTYPE_VNODE => "vnode",
            PROX_FDTYPE_SOCKET => "socket",
            PROX_FDTYPE_PSHX => "pshm",
            PROX_FDTYPE_PIPE => "pipe",
            PROX_FDTYPE_KQUEUE => "kqueue",
            PROX_FDTYPE_ATALK => "atalk",
            _ => "unknown",
        }
    }

    fn inferred_nt_type(&self) -> NtObjectType {
        match self.fd_type {
            PROX_FDTYPE_VNODE => NtObjectType::File,
            PROX_FDTYPE_SOCKET => NtObjectType::Device,
            PROX_FDTYPE_PIPE => NtObjectType::File,
            PROX_FDTYPE_KQUEUE => NtObjectType::IoCompletion,
            _ => NtObjectType::Unknown,
        }
    }

    fn access_mask(&self) -> u32 {
        match self.fd_type {
            PROX_FDTYPE_VNODE => 0x00100089,
            PROX_FDTYPE_SOCKET => 0x001F003F,
            PROX_FDTYPE_PIPE => 0x00100001,
            _ => 0x00000001,
        }
    }

    fn backend_json(&self) -> Value {
        json!({"fd": self.fd})
    }
}

#[derive(Debug)]
struct PortInfo {
    name: u32,
    rights: u32,
}

impl PortInfo {
    fn rights_str(&self) -> &'static str {
        if self.rights & 0x01 != 0 && self.rights & 0x02 != 0 {
            "send+receive"
        } else if self.rights & 0x01 != 0 {
            "send"
        } else if self.rights & 0x02 != 0 {
            "receive"
        } else if self.rights & 0x04 != 0 {
            "send-once"
        } else if self.rights & 0x08 != 0 {
            "port-set"
        } else if self.rights & 0x10 != 0 {
            "dead-name"
        } else {
            "unknown"
        }
    }

    fn inferred_nt_type(&self) -> NtObjectType {
        if self.rights & 0x02 != 0 {
            NtObjectType::Port
        } else if self.rights & 0x08 != 0 {
            NtObjectType::IoCompletion
        } else if self.rights & 0x10 != 0 {
            NtObjectType::Unknown
        } else {
            NtObjectType::Port
        }
    }

    fn backend_json(&self) -> Value {
        json!({"mach_port": self.name, "right": self.rights_str()})
    }
}

#[cfg(target_os = "macos")]
fn enumerate_fds(pid: u32) -> Result<Vec<FdInfo>, String> {
    extern "C" {
        fn proc_pidinfo(pid: i32, flavor: u32, arg: u64, buffer: *mut c_void, buffersize: i32) -> i32;
    }
    unsafe {
        let buf_size: usize = 4096;
        let mut buffer: Vec<u8> = vec![0u8; buf_size];

        let mut needed =
            proc_pidinfo(pid as i32, PROC_PIDLISTFDS, 0u64, buffer.as_mut_ptr() as *mut c_void, buf_size as i32);

        if needed < 0 {
            return Err(format!(
                "proc_pidinfo(PIDLISTFDS) failed for pid {}: errno {}",
                pid,
                std::io::Error::last_os_error().raw_os_error().unwrap_or(-1)
            ));
        }

        if needed == 0 {
            return Ok(Vec::new());
        }

        if (needed as usize) > buf_size {
            buffer.resize(needed as usize, 0);
            needed = proc_pidinfo(pid as i32, PROC_PIDLISTFDS, 0u64, buffer.as_mut_ptr() as *mut c_void, needed);
            if needed < 0 {
                return Err(format!("proc_pidinfo(PIDLISTFDS) retry failed for pid {}", pid));
            }
        }

        let fd_count = (needed as usize) / FD_INFO_SIZE;
        let ptr = buffer.as_ptr() as *const ProcFdInfo;
        let mut fds = Vec::with_capacity(fd_count);
        for i in 0..fd_count {
            let info = &*ptr.add(i);
            fds.push(FdInfo { fd: info.fd, fd_type: info.fd_type });
        }
        Ok(fds)
    }
}

#[cfg(target_os = "macos")]
fn enumerate_mach_ports(pid: u32) -> Result<Vec<PortInfo>, String> {
    unsafe {
        let self_pid = std::process::id();
        if pid != self_pid {
            return Err(format!("mach_port_names only works for own process (requested {}, self {})", pid, self_pid));
        }

        let mut names_ptr: *mut u32 = std::ptr::null_mut();
        let mut name_count: usize = 0;
        let mut types_ptr: *mut u32 = std::ptr::null_mut();
        let mut types_count: usize = 0;

        #[allow(deprecated)]
        let self_task = libc::mach_task_self();

        let kr = mach_port_names_call(self_task, &mut names_ptr, &mut name_count, &mut types_ptr, &mut types_count);
        if kr != 0 {
            return Err(format!("mach_port_names failed with kr={}", kr));
        }

        if name_count == 0 || names_ptr.is_null() {
            return Ok(Vec::new());
        }

        let mut ports = Vec::with_capacity(name_count);
        for i in 0..name_count {
            let name = *names_ptr.add(i);
            let rights = if i < types_count && !types_ptr.is_null() { *types_ptr.add(i) } else { 0x01 };
            ports.push(PortInfo { name, rights });
        }

        {
            extern "C" {
                fn vm_deallocate(target_task: u32, address: u64, size: u64) -> i32;
            }
            let names_size = name_count as u64 * std::mem::size_of::<u32>() as u64;
            #[allow(deprecated)]
            let task = libc::mach_task_self();
            vm_deallocate(task, names_ptr as u64, names_size);
            if !types_ptr.is_null() && types_count > 0 {
                let types_size = types_count as u64 * std::mem::size_of::<u32>() as u64;
                vm_deallocate(task, types_ptr as u64, types_size);
            }
        }

        Ok(ports)
    }
}

#[cfg(target_os = "macos")]
fn mach_port_names_call(
    task: u32,
    names: &mut *mut u32,
    names_count: &mut usize,
    types: &mut *mut u32,
    types_count: &mut usize,
) -> i32 {
    unsafe {
        let sym = libc::dlsym(libc::RTLD_DEFAULT, c"mach_port_names".as_ptr());
        if sym.is_null() {
            return -1;
        }
        let func: extern "C" fn(u32, *mut *mut u32, *mut u32, *mut *mut u32, *mut u32) -> i32 =
            std::mem::transmute(sym);
        let mut local_names_count: u32 = 0;
        let mut local_types_count: u32 = 0;
        let kr = func(task, names, &mut local_names_count, types, &mut local_types_count);
        *names_count = local_names_count as usize;
        *types_count = local_types_count as usize;
        kr
    }
}

#[cfg(target_os = "macos")]
fn categorize_fds(fds: &[FdInfo]) -> BTreeMap<i32, FdInfo> {
    let mut map = BTreeMap::new();
    for fd in fds {
        map.insert(fd.fd, FdInfo { fd: fd.fd, fd_type: fd.fd_type });
    }
    map
}

#[cfg(target_os = "macos")]
fn categorize_ports(ports: &[PortInfo]) -> BTreeMap<u32, PortInfo> {
    let mut map = BTreeMap::new();
    for p in ports {
        map.insert(p.name, PortInfo { name: p.name, rights: p.rights });
    }
    map
}

#[cfg(target_os = "macos")]
fn merge_fds_into_table(pid: u32, fds: &BTreeMap<i32, FdInfo>, fake_missing: bool) -> usize {
    let mut tables = HANDLE_TABLES.lock().unwrap();
    let table = tables.entry(pid).or_insert_with(|| super::handle_table::VirtualHandleTable::new(pid));

    let mut count = 0;
    for (fd_num, _info) in fds {
        let fd_val = *fd_num;
        let already_exists = table.handles.values().any(|e| match &e.backend {
            HandleBackend::Fd(f) => f == &fd_val,
            _ => false,
        });
        if already_exists {
            continue;
        }

        let info = _info;
        let nt_type = info.inferred_nt_type();
        let access_mask = info.access_mask();
        let name = fd_to_nt_name(*fd_num, info);
        let backend = HandleBackend::Fd(*fd_num);

        table.alloc_handle(nt_type, access_mask, name, backend);
        count += 1;
    }

    if fake_missing {
        count += inject_missing_fd_types(pid, table, fds);
    }

    count
}

#[cfg(target_os = "macos")]
fn merge_ports_into_table(pid: u32, ports: &BTreeMap<u32, PortInfo>) -> usize {
    let mut tables = HANDLE_TABLES.lock().unwrap();
    let table = tables.entry(pid).or_insert_with(|| super::handle_table::VirtualHandleTable::new(pid));

    let mut count = 0;
    for (name, info) in ports {
        let port_val = *name;
        let already_exists = table.handles.values().any(|e| match &e.backend {
            HandleBackend::MachPort { name: n, .. } => n == &port_val,
            _ => false,
        });
        if already_exists {
            continue;
        }

        let nt_type = info.inferred_nt_type();
        let access_mask = 0x000F003Fu32;
        let port_name = format!("\\RPC Control\\port_0x{:08X}", name);
        let right = info.rights_str().to_string();
        let backend = HandleBackend::MachPort { name: *name, right };

        table.alloc_handle(nt_type, access_mask, port_name, backend);
        count += 1;
    }

    count
}

fn fd_to_nt_name(fd: i32, info: &FdInfo) -> String {
    match info.fd_type {
        PROX_FDTYPE_VNODE => format!("\\Device\\HarddiskVolume\\fd_{}", fd),
        PROX_FDTYPE_SOCKET => format!("\\Device\\Afd\\fd_{}", fd),
        PROX_FDTYPE_PIPE => format!("\\Device\\NamedPipe\\fd_{}", fd),
        PROX_FDTYPE_KQUEUE => format!("\\KernelObjects\\kqueue_fd_{}", fd),
        _ => format!("\\??\\fd_{}", fd),
    }
}

#[cfg(target_os = "macos")]
fn inject_missing_fd_types(
    _pid: u32,
    _table: &mut super::handle_table::VirtualHandleTable,
    existing_fds: &BTreeMap<i32, FdInfo>,
) -> usize {
    let mut count = 0;

    let has_stdin = existing_fds.contains_key(&0);
    let has_stdout = existing_fds.contains_key(&1);
    let has_stderr = existing_fds.contains_key(&2);

    if !has_stdin {
        _table.alloc_handle(NtObjectType::File, 0x00100089, "\\Device\\Console\\stdin".into(), HandleBackend::Fd(0));
        count += 1;
    }
    if !has_stdout {
        _table.alloc_handle(NtObjectType::File, 0x00100089, "\\Device\\Console\\stdout".into(), HandleBackend::Fd(1));
        count += 1;
    }
    if !has_stderr {
        _table.alloc_handle(NtObjectType::File, 0x00100089, "\\Device\\Console\\stderr".into(), HandleBackend::Fd(2));
        count += 1;
    }

    count
}

#[cfg(target_os = "macos")]
fn inject_fake_wine_handles(pid: u32) -> usize {
    let mut tables = HANDLE_TABLES.lock().unwrap();
    let table = tables.entry(pid).or_insert_with(|| super::handle_table::VirtualHandleTable::new(pid));

    let existing_types: std::collections::HashSet<String> =
        table.handles.values().map(|e| e.object_type.type_name().to_string()).collect();

    let mut count = 0;

    let fake_handles: [(NtObjectType, u32, &str, HandleBackend); 8] = [
        (NtObjectType::Process, 0x001F0FFF, "", HandleBackend::Virtual("current process".into())),
        (NtObjectType::Thread, 0x001F0FFF, "", HandleBackend::Virtual("main thread".into())),
        (NtObjectType::Token, 0x00020008, "", HandleBackend::Virtual("primary token".into())),
        (
            NtObjectType::Section,
            0x00000005,
            "\\Windows\\System32\\ntdll.dll",
            HandleBackend::Virtual("mapped image".into()),
        ),
        (
            NtObjectType::Event,
            0x001F0003,
            "\\BaseNamedObjects\\Global\\WineInitComplete",
            HandleBackend::Virtual("event".into()),
        ),
        (
            NtObjectType::Mutant,
            0x001F0001,
            "\\BaseNamedObjects\\Global\\WineStartupMutex",
            HandleBackend::Virtual("mutex".into()),
        ),
        (
            NtObjectType::Key,
            0x00020019,
            "\\REGISTRY\\MACHINE\\SOFTWARE\\Wine",
            HandleBackend::Virtual("registry".into()),
        ),
        (NtObjectType::Job, 0x001F0FFF, "\\BaseNamedObjects\\WineJob", HandleBackend::Virtual("job".into())),
    ];

    for (nt_type, access, name, backend) in &fake_handles {
        if existing_types.contains(nt_type.type_name()) {
            let has_this_kind = table
                .handles
                .values()
                .any(|e| e.object_type == *nt_type && matches!(e.backend, HandleBackend::Virtual(_)));
            if has_this_kind {
                continue;
            }
        }
        table.alloc_handle(*nt_type, *access, name.to_string(), backend.clone());
        count += 1;
    }

    count
}

#[cfg(target_os = "macos")]
fn list_all_pids() -> Result<Vec<u32>, String> {
    extern "C" {
        fn proc_listpids(dtype: u32, typetype: u32, buffer: *mut c_void, buffersize: i32) -> i32;
    }
    unsafe {
        let buf_size: usize = 65536;
        let mut buffer: Vec<u8> = vec![0u8; buf_size];

        let needed = proc_listpids(PROC_ALL_PIDS, 0, buffer.as_mut_ptr() as *mut c_void, buf_size as i32);

        if needed < 0 {
            return Err(format!(
                "proc_listpids failed: errno {}",
                std::io::Error::last_os_error().raw_os_error().unwrap_or(-1)
            ));
        }

        if needed == 0 {
            return Ok(Vec::new());
        }

        let count = (needed as usize) / 4;
        let ptr = buffer.as_ptr() as *const u32;
        let mut pids = Vec::with_capacity(count);
        for i in 0..count {
            pids.push(*ptr.add(i));
        }
        Ok(pids)
    }
}

#[cfg(target_os = "macos")]
fn fd_summary(fds: &BTreeMap<i32, FdInfo>) -> Value {
    let mut type_counts: BTreeMap<String, usize> = BTreeMap::new();
    for info in fds.values() {
        *type_counts.entry(info.fd_type_name().to_string()).or_insert(0) += 1;
    }
    let mut nt_counts: BTreeMap<String, usize> = BTreeMap::new();
    for info in fds.values() {
        *nt_counts.entry(info.inferred_nt_type().type_name().to_string()).or_insert(0) += 1;
    }
    json!({
        "total": fds.len(),
        "byFdType": type_counts,
        "byInferredNtType": nt_counts,
    })
}

#[cfg(target_os = "macos")]
fn port_summary(ports: &BTreeMap<u32, PortInfo>) -> Value {
    let mut right_counts: BTreeMap<String, usize> = BTreeMap::new();
    for info in ports.values() {
        *right_counts.entry(info.rights_str().to_string()).or_insert(0) += 1;
    }
    let mut nt_counts: BTreeMap<String, usize> = BTreeMap::new();
    for info in ports.values() {
        *nt_counts.entry(info.inferred_nt_type().type_name().to_string()).or_insert(0) += 1;
    }
    json!({
        "total": ports.len(),
        "byRights": right_counts,
        "byInferredNtType": nt_counts,
    })
}

#[cfg(test)]
mod tests {
    #[cfg(target_os = "macos")]
    #[test]
    fn test_enumerate_own_fds() {
        let result = super::enumerate_fds(std::process::id());
        match &result {
            Ok(fds) => {
                assert!(fds.len() >= 3, "process should have at least stdin/stdout/stderr, got {}", fds.len());
            },
            Err(e) => panic!("enumerate_fds failed: {}", e),
        }
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn test_enumerate_own_ports() {
        let result = super::enumerate_mach_ports(std::process::id());
        assert!(result.is_ok());
        let ports = result.unwrap();
        assert!(!ports.is_empty(), "process should have at least one mach port");
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn test_enumerate_other_pid_ports_fails() {
        let result = super::enumerate_mach_ports(1);
        assert!(result.is_err());
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn test_enumerate_invalid_pid_fds() {
        let result = super::enumerate_fds(999999);
        match result {
            Ok(fds) => assert!(fds.is_empty(), "invalid pid should return empty or error"),
            Err(_) => {},
        }
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn test_list_all_pids() {
        let result = super::list_all_pids();
        match &result {
            Ok(pids) => {
                assert!(pids.len() > 1, "should have multiple processes, got {}", pids.len());
                assert!(pids.contains(&std::process::id()), "should contain own pid {}", std::process::id());
            },
            Err(e) => panic!("list_all_pids failed: {}", e),
        }
    }

    #[test]
    fn test_fd_info_type_names() {
        use super::*;
        let info = FdInfo { fd: 3, fd_type: PROX_FDTYPE_VNODE };
        assert_eq!(info.fd_type_name(), "vnode");
        assert_eq!(info.inferred_nt_type(), NtObjectType::File);

        let info = FdInfo { fd: 4, fd_type: PROX_FDTYPE_SOCKET };
        assert_eq!(info.fd_type_name(), "socket");
        assert_eq!(info.inferred_nt_type(), NtObjectType::Device);

        let info = FdInfo { fd: 5, fd_type: PROX_FDTYPE_KQUEUE };
        assert_eq!(info.fd_type_name(), "kqueue");
        assert_eq!(info.inferred_nt_type(), NtObjectType::IoCompletion);
    }

    #[test]
    fn test_port_info_rights() {
        use super::*;
        let info = PortInfo { name: 0x1234, rights: 0x01 };
        assert_eq!(info.rights_str(), "send");

        let info = PortInfo { name: 0x1234, rights: 0x03 };
        assert_eq!(info.rights_str(), "send+receive");

        let info = PortInfo { name: 0x1234, rights: 0x04 };
        assert_eq!(info.rights_str(), "send-once");

        let info = PortInfo { name: 0x1234, rights: 0x08 };
        assert_eq!(info.rights_str(), "port-set");

        let info = PortInfo { name: 0x1234, rights: 0x10 };
        assert_eq!(info.rights_str(), "dead-name");
    }

    #[test]
    fn test_fd_to_nt_name() {
        use super::*;
        let info = FdInfo { fd: 3, fd_type: PROX_FDTYPE_VNODE };
        assert_eq!(fd_to_nt_name(3, &info), "\\Device\\HarddiskVolume\\fd_3");

        let info = FdInfo { fd: 5, fd_type: PROX_FDTYPE_SOCKET };
        assert_eq!(fd_to_nt_name(5, &info), "\\Device\\Afd\\fd_5");

        let info = FdInfo { fd: 7, fd_type: PROX_FDTYPE_PIPE };
        assert_eq!(fd_to_nt_name(7, &info), "\\Device\\NamedPipe\\fd_7");
    }
}
