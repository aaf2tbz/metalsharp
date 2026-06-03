use serde_json::{json, Map, Value};
use std::collections::BTreeMap;
use std::sync::LazyLock;
use std::sync::Mutex;

pub static HANDLE_TABLES: LazyLock<Mutex<BTreeMap<u32, VirtualHandleTable>>> =
    LazyLock::new(|| Mutex::new(BTreeMap::new()));

pub static NEXT_HANDLE: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0x00000100);

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct VirtualHandleEntry {
    pub handle: u64,
    pub object_type: NtObjectType,
    pub access_mask: u32,
    pub name: String,
    pub backend: HandleBackend,
    pub created_at: u64,
    pub process_id: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum NtObjectType {
    Process,
    Thread,
    File,
    Device,
    Key,
    Event,
    Mutant,
    Semaphore,
    Timer,
    Port,
    IoCompletion,
    Section,
    Directory,
    SymbolicLink,
    Token,
    Job,
    Debug,
    KeyedEvent,
    WaitablePort,
    WorkerFactory,
    Profile,
    Transaction,
    Unknown,
}

impl NtObjectType {
    pub fn type_name(&self) -> &'static str {
        match self {
            Self::Process => "Process",
            Self::Thread => "Thread",
            Self::File => "File",
            Self::Device => "Device",
            Self::Key => "Key",
            Self::Event => "Event",
            Self::Mutant => "Mutant",
            Self::Semaphore => "Semaphore",
            Self::Timer => "Timer",
            Self::Port => "Port",
            Self::IoCompletion => "IoCompletion",
            Self::Section => "Section",
            Self::Directory => "Directory",
            Self::SymbolicLink => "SymbolicLink",
            Self::Token => "Token",
            Self::Job => "Job",
            Self::Debug => "Debug",
            Self::KeyedEvent => "KeyedEvent",
            Self::WaitablePort => "WaitablePort",
            Self::WorkerFactory => "WorkerFactory",
            Self::Profile => "Profile",
            Self::Transaction => "Transaction",
            Self::Unknown => "Unknown",
        }
    }

    pub fn from_type_name(name: &str) -> Option<Self> {
        match name {
            "Process" => Some(Self::Process),
            "Thread" => Some(Self::Thread),
            "File" => Some(Self::File),
            "Device" => Some(Self::Device),
            "Key" => Some(Self::Key),
            "Event" => Some(Self::Event),
            "Mutant" => Some(Self::Mutant),
            "Semaphore" => Some(Self::Semaphore),
            "Timer" => Some(Self::Timer),
            "Port" => Some(Self::Port),
            "IoCompletion" => Some(Self::IoCompletion),
            "Section" => Some(Self::Section),
            "Directory" => Some(Self::Directory),
            "SymbolicLink" => Some(Self::SymbolicLink),
            "Token" => Some(Self::Token),
            "Job" => Some(Self::Job),
            "Debug" => Some(Self::Debug),
            "KeyedEvent" => Some(Self::KeyedEvent),
            "WaitablePort" => Some(Self::WaitablePort),
            "WorkerFactory" => Some(Self::WorkerFactory),
            "Profile" => Some(Self::Profile),
            "Transaction" => Some(Self::Transaction),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub enum HandleBackend {
    Fd(i32),
    MachPort { name: u32, right: String },
    Virtual(String),
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct VirtualHandleTable {
    pub pid: u32,
    pub handles: BTreeMap<u64, VirtualHandleEntry>,
}

impl VirtualHandleTable {
    pub fn new(pid: u32) -> Self {
        Self { pid, handles: BTreeMap::new() }
    }

    pub fn alloc_handle(
        &mut self,
        object_type: NtObjectType,
        access_mask: u32,
        name: String,
        backend: HandleBackend,
    ) -> u64 {
        let handle = NEXT_HANDLE.fetch_add(4, std::sync::atomic::Ordering::Relaxed);
        let entry = VirtualHandleEntry {
            handle,
            object_type,
            access_mask,
            name,
            backend,
            created_at: now_millis(),
            process_id: self.pid,
        };
        self.handles.insert(handle, entry);
        handle
    }

    pub fn close_handle(&mut self, handle: u64) -> bool {
        self.handles.remove(&handle).is_some()
    }

    pub fn duplicate_handle(&mut self, source_handle: u64, access_mask: u32) -> Option<u64> {
        let source = self.handles.get(&source_handle)?;
        let dup_entry = VirtualHandleEntry {
            handle: 0,
            object_type: source.object_type,
            access_mask,
            name: source.name.clone(),
            backend: source.backend.clone(),
            created_at: now_millis(),
            process_id: self.pid,
        };
        let new_handle = NEXT_HANDLE.fetch_add(4, std::sync::atomic::Ordering::Relaxed);
        let mut entry = dup_entry;
        entry.handle = new_handle;
        self.handles.insert(new_handle, entry);
        Some(new_handle)
    }

    pub fn query_object(&self, handle: u64) -> Option<&VirtualHandleEntry> {
        self.handles.get(&handle)
    }

    pub fn enumerate(&self) -> Vec<&VirtualHandleEntry> {
        self.handles.values().collect()
    }

    pub fn enumerate_by_type(&self, object_type: NtObjectType) -> Vec<&VirtualHandleEntry> {
        self.handles.values().filter(|e| e.object_type == object_type).collect()
    }

    pub fn handle_count(&self) -> usize {
        self.handles.len()
    }

    pub fn type_counts(&self) -> BTreeMap<String, usize> {
        let mut counts = BTreeMap::new();
        for entry in self.handles.values() {
            *counts.entry(entry.object_type.type_name().to_string()).or_insert(0) += 1;
        }
        counts
    }

    pub fn to_system_handle_information(&self) -> Value {
        let handles: Vec<Value> = self
            .handles
            .values()
            .map(|e| {
                json!({
                    "ProcessId": e.process_id,
                    "Handle": format!("0x{:08X}", e.handle),
                    "ObjectTypeNumber": object_type_number(e.object_type),
                    "Flags": 0,
                    "Pointer": format!("0x{:016X}", e.handle * 31),
                    "GrantedAccess": format!("0x{:08X}", e.access_mask),
                    "TypeName": e.object_type.type_name(),
                    "Name": e.name,
                })
            })
            .collect();
        json!({
            "NumberOfHandles": handles.len(),
            "Handles": handles,
        })
    }
}

fn object_type_number(t: NtObjectType) -> u8 {
    match t {
        NtObjectType::Process => 0x01,
        NtObjectType::Thread => 0x02,
        NtObjectType::File => 0x03,
        NtObjectType::Device => 0x04,
        NtObjectType::Key => 0x05,
        NtObjectType::Event => 0x06,
        NtObjectType::Mutant => 0x07,
        NtObjectType::Semaphore => 0x08,
        NtObjectType::Timer => 0x09,
        NtObjectType::Port => 0x0A,
        NtObjectType::IoCompletion => 0x0B,
        NtObjectType::Section => 0x0C,
        NtObjectType::Directory => 0x0D,
        NtObjectType::SymbolicLink => 0x0E,
        NtObjectType::Token => 0x0F,
        NtObjectType::Job => 0x10,
        NtObjectType::Debug => 0x11,
        NtObjectType::KeyedEvent => 0x12,
        NtObjectType::WaitablePort => 0x13,
        NtObjectType::WorkerFactory => 0x14,
        NtObjectType::Profile => 0x15,
        NtObjectType::Transaction => 0x16,
        NtObjectType::Unknown => 0xFF,
    }
}

fn now_millis() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_millis() as u64
}

fn get_or_create_table(pid: u32) -> Value {
    let mut tables = HANDLE_TABLES.lock().unwrap();
    tables.entry(pid).or_insert_with(|| VirtualHandleTable::new(pid));
    json!({"pid": pid})
}

pub fn handle_create(body: &Map<String, Value>) -> Value {
    let pid = match body.get("pid").and_then(|v| v.as_u64()) {
        Some(p) if p <= u32::MAX as u64 => p as u32,
        _ => return json!({"ok": false, "error": "pid (u32) required"}),
    };
    let type_name = match body.get("object_type").and_then(|v| v.as_str()) {
        Some(t) => t,
        _ => return json!({"ok": false, "error": "object_type string required"}),
    };
    let object_type = match NtObjectType::from_type_name(type_name) {
        Some(t) => t,
        None => return json!({"ok": false, "error": format!("unknown object_type '{}'", type_name)}),
    };
    let access_mask = body.get("access_mask").and_then(|v| v.as_u64()).unwrap_or(0x001F01FF) as u32;
    let name = body.get("name").and_then(|v| v.as_str()).unwrap_or("").to_string();

    let backend = match body.get("backend") {
        Some(b) => {
            let bobj = match b.as_object() {
                Some(o) => o,
                None => {
                    return json!({"ok": false, "error": "backend must be an object: {fd: i32} or {mach_port: u32, right: str} or {virtual: str}"})
                },
            };
            if let Some(fd) = bobj.get("fd").and_then(|v| v.as_i64()) {
                HandleBackend::Fd(fd as i32)
            } else if let Some(port) = bobj.get("mach_port").and_then(|v| v.as_u64()) {
                let right = bobj.get("right").and_then(|v| v.as_str()).unwrap_or("send").to_string();
                HandleBackend::MachPort { name: port as u32, right }
            } else if let Some(desc) = bobj.get("virtual").and_then(|v| v.as_str()) {
                HandleBackend::Virtual(desc.to_string())
            } else {
                HandleBackend::Virtual("unnamed".to_string())
            }
        },
        None => HandleBackend::Virtual("auto".to_string()),
    };

    get_or_create_table(pid);
    let mut tables = HANDLE_TABLES.lock().unwrap();
    let table = tables.get_mut(&pid).unwrap();
    let handle = table.alloc_handle(object_type, access_mask, name, backend);
    let entry = table.query_object(handle).unwrap();

    json!({
        "ok": true,
        "handle": format!("0x{:08X}", handle),
        "handle_raw": handle,
        "entry": entry,
    })
}

pub fn handle_close(body: &Map<String, Value>) -> Value {
    let pid = match body.get("pid").and_then(|v| v.as_u64()) {
        Some(p) if p <= u32::MAX as u64 => p as u32,
        _ => return json!({"ok": false, "error": "pid (u32) required"}),
    };
    let handle_raw = match body.get("handle").and_then(|v| v.as_u64()) {
        Some(h) => h,
        None => match body.get("handle").and_then(|v| v.as_str()) {
            Some(h) => u64::from_str_radix(h.trim_start_matches("0x"), 16).unwrap_or(0),
            None => return json!({"ok": false, "error": "handle (u64 or hex string) required"}),
        },
    };

    let mut tables = HANDLE_TABLES.lock().unwrap();
    match tables.get_mut(&pid) {
        Some(table) => {
            let removed = table.close_handle(handle_raw);
            json!({"ok": removed, "handle": format!("0x{:08X}", handle_raw), "removed": removed})
        },
        None => json!({"ok": false, "error": format!("no handle table for pid {}", pid)}),
    }
}

pub fn handle_duplicate(body: &Map<String, Value>) -> Value {
    let pid = match body.get("pid").and_then(|v| v.as_u64()) {
        Some(p) if p <= u32::MAX as u64 => p as u32,
        _ => return json!({"ok": false, "error": "pid (u32) required"}),
    };
    let source_handle = match body.get("source_handle").and_then(|v| v.as_u64()) {
        Some(h) => h,
        None => return json!({"ok": false, "error": "source_handle (u64) required"}),
    };
    let access_mask = body.get("access_mask").and_then(|v| v.as_u64()).unwrap_or(0) as u32;
    let options = body.get("options").and_then(|v| v.as_u64()).unwrap_or(0) as u32;

    let mut tables = HANDLE_TABLES.lock().unwrap();
    match tables.get_mut(&pid) {
        Some(table) => match table.duplicate_handle(source_handle, access_mask) {
            Some(new_handle) => {
                let entry = table.query_object(new_handle).unwrap();
                json!({
                    "ok": true,
                    "source_handle": format!("0x{:08X}", source_handle),
                    "new_handle": format!("0x{:08X}", new_handle),
                    "new_handle_raw": new_handle,
                    "entry": entry,
                    "options": options,
                })
            },
            None => json!({"ok": false, "error": format!("source handle 0x{:08X} not found", source_handle)}),
        },
        None => json!({"ok": false, "error": format!("no handle table for pid {}", pid)}),
    }
}

pub fn handle_query(body: &Map<String, Value>) -> Value {
    let pid = match body.get("pid").and_then(|v| v.as_u64()) {
        Some(p) if p <= u32::MAX as u64 => p as u32,
        _ => return json!({"ok": false, "error": "pid (u32) required"}),
    };
    let handle_raw = match body.get("handle").and_then(|v| v.as_u64()) {
        Some(h) => h,
        None => return json!({"ok": false, "error": "handle (u64) required"}),
    };

    let tables = HANDLE_TABLES.lock().unwrap();
    match tables.get(&pid) {
        Some(table) => match table.query_object(handle_raw) {
            Some(entry) => json!({
                "ok": true,
                "handle": format!("0x{:08X}", handle_raw),
                "TypeName": entry.object_type.type_name(),
                "HandleCount": 1,
                "PointerCount": 1,
                "Name": entry.name,
                "GrantedAccess": format!("0x{:08X}", entry.access_mask),
                "entry": entry,
            }),
            None => json!({"ok": false, "error": format!("handle 0x{:08X} not found in pid {}", handle_raw, pid)}),
        },
        None => json!({"ok": false, "error": format!("no handle table for pid {}", pid)}),
    }
}

pub fn handle_enumerate(body: &Map<String, Value>) -> Value {
    let pid = match body.get("pid").and_then(|v| v.as_u64()) {
        Some(p) if p <= u32::MAX as u64 => p as u32,
        _ => return json!({"ok": false, "error": "pid (u32) required"}),
    };
    let filter_type = body.get("filter_type").and_then(|v| v.as_str());

    let tables = HANDLE_TABLES.lock().unwrap();
    match tables.get(&pid) {
        Some(table) => {
            let entries: Vec<&VirtualHandleEntry> = match filter_type {
                Some(t) => match NtObjectType::from_type_name(t) {
                    Some(ot) => table.enumerate_by_type(ot),
                    None => return json!({"ok": false, "error": format!("unknown filter_type '{}'", t)}),
                },
                None => table.enumerate(),
            };
            json!({
                "ok": true,
                "pid": pid,
                "handleCount": entries.len(),
                "typeCounts": table.type_counts(),
                "handles": entries.iter().map(|e| {
                    json!({
                        "Handle": format!("0x{:08X}", e.handle),
                        "TypeName": e.object_type.type_name(),
                        "Name": e.name,
                        "GrantedAccess": format!("0x{:08X}", e.access_mask),
                        "Backend": e.backend,
                    })
                }).collect::<Vec<_>>(),
            })
        },
        None => json!({"ok": false, "error": format!("no handle table for pid {}", pid)}),
    }
}

pub fn handle_system_handle_information(body: &Map<String, Value>) -> Value {
    let pid = match body.get("pid").and_then(|v| v.as_u64()) {
        Some(p) if p <= u32::MAX as u64 => p as u32,
        _ => return json!({"ok": false, "error": "pid (u32) required"}),
    };

    let tables = HANDLE_TABLES.lock().unwrap();
    match tables.get(&pid) {
        Some(table) => json!({
            "ok": true,
            "SystemHandleInformation": table.to_system_handle_information(),
        }),
        None => json!({"ok": false, "error": format!("no handle table for pid {}", pid)}),
    }
}

pub fn handle_table_status(body: &Map<String, Value>) -> Value {
    let tables = HANDLE_TABLES.lock().unwrap();
    let pids: Vec<u32> = tables.keys().copied().collect();
    let summaries: Vec<Value> = tables
        .values()
        .map(|t| {
            json!({
                "pid": t.pid,
                "handleCount": t.handle_count(),
                "typeCounts": t.type_counts(),
            })
        })
        .collect();

    let filter_pid = body.get("pid").and_then(|v| v.as_u64()).map(|p| p as u32);
    match filter_pid {
        Some(pid) => match tables.get(&pid) {
            Some(table) => json!({
                "ok": true,
                "pid": pid,
                "handleCount": table.handle_count(),
                "typeCounts": table.type_counts(),
                "entries": table.enumerate().iter().map(|e| {
                    json!({
                        "Handle": format!("0x{:08X}", e.handle),
                        "TypeName": e.object_type.type_name(),
                        "Name": e.name,
                    })
                }).collect::<Vec<_>>(),
            }),
            None => json!({"ok": false, "error": format!("no handle table for pid {}", pid)}),
        },
        None => json!({
            "ok": true,
            "totalTables": pids.len(),
            "pids": pids,
            "tables": summaries,
        }),
    }
}

pub fn handle_seed_demo(body: &Map<String, Value>) -> Value {
    let pid = match body.get("pid").and_then(|v| v.as_u64()) {
        Some(p) if p <= u32::MAX as u64 => p as u32,
        _ => return json!({"ok": false, "error": "pid (u32) required"}),
    };

    get_or_create_table(pid);
    let mut tables = HANDLE_TABLES.lock().unwrap();
    let table = tables.get_mut(&pid).unwrap();

    let seed_count = body.get("count").and_then(|v| v.as_u64()).unwrap_or(25) as usize;

    let demo_handles: [(NtObjectType, u32, &str, HandleBackend); 20] = [
        (NtObjectType::Process, 0x001F0FFF, "\\??\\explorer.exe", HandleBackend::Fd(0)),
        (NtObjectType::Thread, 0x001F0FFF, "", HandleBackend::Virtual("main thread".into())),
        (
            NtObjectType::File,
            0x00100001,
            "\\Device\\HarddiskVolume3\\Windows\\System32\\ntdll.dll",
            HandleBackend::Fd(3),
        ),
        (
            NtObjectType::File,
            0x00100001,
            "\\Device\\HarddiskVolume3\\Windows\\System32\\kernel32.dll",
            HandleBackend::Fd(4),
        ),
        (NtObjectType::File, 0x00100001, "\\Device\\HarddiskVolume3\\game\\game.exe", HandleBackend::Fd(5)),
        (NtObjectType::Section, 0x00000005, "\\Windows\\System32\\ntdll.dll", HandleBackend::Fd(6)),
        (
            NtObjectType::Event,
            0x001F0003,
            "\\BaseNamedObjects\\Global\\GameInitComplete",
            HandleBackend::Virtual("event".into()),
        ),
        (
            NtObjectType::Mutant,
            0x001F0001,
            "\\BaseNamedObjects\\Global\\GameMutex",
            HandleBackend::Virtual("mutex".into()),
        ),
        (
            NtObjectType::Key,
            0x00020019,
            "\\REGISTRY\\MACHINE\\SOFTWARE\\Game",
            HandleBackend::Virtual("registry".into()),
        ),
        (
            NtObjectType::Key,
            0x00020019,
            "\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Services",
            HandleBackend::Virtual("registry".into()),
        ),
        (NtObjectType::Token, 0x00020008, "", HandleBackend::Virtual("primary token".into())),
        (
            NtObjectType::Port,
            0x000F003F,
            "\\RPC Control\\epmapper",
            HandleBackend::MachPort { name: 0x1307, right: "send".into() },
        ),
        (NtObjectType::Device, 0x000F003F, "\\Device\\Afd", HandleBackend::Fd(7)),
        (NtObjectType::IoCompletion, 0x001F0003, "", HandleBackend::Fd(8)),
        (
            NtObjectType::Semaphore,
            0x001F0003,
            "\\BaseNamedObjects\\Global\\MaxConnections",
            HandleBackend::Virtual("semaphore".into()),
        ),
        (NtObjectType::Timer, 0x001F0003, "", HandleBackend::MachPort { name: 0x1401, right: "send".into() }),
        (NtObjectType::Debug, 0x000F003F, "", HandleBackend::Virtual("debug object".into())),
        (NtObjectType::Job, 0x001F0FFF, "\\BaseNamedObjects\\GameJob", HandleBackend::Virtual("coalition".into())),
        (NtObjectType::Directory, 0x00020001, "\\??\\", HandleBackend::Virtual("object directory".into())),
        (NtObjectType::SymbolicLink, 0x00020001, "\\??\\C:", HandleBackend::Virtual("symlink to prefix".into())),
    ];

    let mut created = Vec::new();
    for (i, (ot, am, name, be)) in demo_handles.iter().enumerate() {
        if i >= seed_count {
            break;
        }
        let handle = table.alloc_handle(*ot, *am, name.to_string(), be.clone());
        created.push(format!("0x{:08X} {} {}", handle, ot.type_name(), name));
    }

    json!({
        "ok": true,
        "pid": pid,
        "created": created.len(),
        "totalHandles": table.handle_count(),
        "typeCounts": table.type_counts(),
        "handles": created,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fresh_table() -> VirtualHandleTable {
        VirtualHandleTable::new(1234)
    }

    #[test]
    fn test_alloc_handle_returns_increasing_values() {
        let mut table = fresh_table();
        let h1 = table.alloc_handle(NtObjectType::File, 0x01, "test1".into(), HandleBackend::Fd(3));
        let h2 = table.alloc_handle(NtObjectType::Event, 0x02, "test2".into(), HandleBackend::Virtual("v".into()));
        assert_ne!(h1, h2);
        assert_eq!(table.handle_count(), 2);
    }

    #[test]
    fn test_close_handle_removes_entry() {
        let mut table = fresh_table();
        let h = table.alloc_handle(NtObjectType::File, 0x01, "test".into(), HandleBackend::Fd(3));
        assert!(table.close_handle(h));
        assert!(!table.close_handle(h));
        assert_eq!(table.handle_count(), 0);
    }

    #[test]
    fn test_query_object_returns_entry() {
        let mut table = fresh_table();
        let h = table.alloc_handle(NtObjectType::Process, 0x1F0FFF, "explorer.exe".into(), HandleBackend::Fd(0));
        let entry = table.query_object(h).unwrap();
        assert_eq!(entry.object_type, NtObjectType::Process);
        assert_eq!(entry.name, "explorer.exe");
    }

    #[test]
    fn test_query_nonexistent_handle_returns_none() {
        let table = fresh_table();
        assert!(table.query_object(0xDEAD).is_none());
    }

    #[test]
    fn test_duplicate_handle_creates_new_entry() {
        let mut table = fresh_table();
        let h1 = table.alloc_handle(NtObjectType::File, 0x01, "original".into(), HandleBackend::Fd(3));
        let h2 = table.duplicate_handle(h1, 0x02).unwrap();
        assert_ne!(h1, h2);
        assert_eq!(table.handle_count(), 2);
        let e2 = table.query_object(h2).unwrap();
        assert_eq!(e2.name, "original");
        assert_eq!(e2.access_mask, 0x02);
    }

    #[test]
    fn test_enumerate_by_type_filters() {
        let mut table = fresh_table();
        table.alloc_handle(NtObjectType::File, 0x01, "a".into(), HandleBackend::Fd(3));
        table.alloc_handle(NtObjectType::File, 0x01, "b".into(), HandleBackend::Fd(4));
        table.alloc_handle(NtObjectType::Event, 0x02, "c".into(), HandleBackend::Virtual("v".into()));
        let files = table.enumerate_by_type(NtObjectType::File);
        assert_eq!(files.len(), 2);
        let events = table.enumerate_by_type(NtObjectType::Event);
        assert_eq!(events.len(), 1);
    }

    #[test]
    fn test_type_counts() {
        let mut table = fresh_table();
        table.alloc_handle(NtObjectType::File, 0x01, "a".into(), HandleBackend::Fd(3));
        table.alloc_handle(NtObjectType::File, 0x01, "b".into(), HandleBackend::Fd(4));
        table.alloc_handle(NtObjectType::Event, 0x02, "c".into(), HandleBackend::Virtual("v".into()));
        let counts = table.type_counts();
        assert_eq!(counts.get("File").copied(), Some(2));
        assert_eq!(counts.get("Event").copied(), Some(1));
    }

    #[test]
    fn test_to_system_handle_information_format() {
        let mut table = fresh_table();
        table.alloc_handle(NtObjectType::Process, 0x1F0FFF, "test.exe".into(), HandleBackend::Fd(0));
        let info = table.to_system_handle_information();
        assert_eq!(info["NumberOfHandles"], 1);
        let handles = info["Handles"].as_array().unwrap();
        assert_eq!(handles[0]["TypeName"], "Process");
    }

    #[test]
    fn test_object_type_round_trip() {
        for name in ["Process", "Thread", "File", "Event", "Key", "Token"] {
            let ot = NtObjectType::from_type_name(name).unwrap();
            assert_eq!(ot.type_name(), name);
        }
    }
}
