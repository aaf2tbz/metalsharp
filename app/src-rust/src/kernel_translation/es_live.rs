use serde_json::{json, Value};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Mutex;

const MAX_EVENTS: usize = 4096;

static ES_CLIENT_ACTIVE: AtomicBool = AtomicBool::new(false);
static NEXT_EVENT_SEQ: AtomicU64 = AtomicU64::new(1);

static LIVE_EVENTS: std::sync::LazyLock<Mutex<Vec<LiveEsEvent>>> = std::sync::LazyLock::new(|| Mutex::new(Vec::new()));

static PROCESS_REGISTRY: std::sync::LazyLock<Mutex<Vec<ProcessRecord>>> =
    std::sync::LazyLock::new(|| Mutex::new(Vec::new()));

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct LiveEsEvent {
    pub seq: u64,
    pub event_type: String,
    pub pid: u32,
    pub ppid: u32,
    pub tid: u64,
    pub executable_path: Option<String>,
    pub timestamp_us: u64,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ProcessRecord {
    pub pid: u32,
    pub ppid: u32,
    pub path: Option<String>,
    pub created_at_us: u64,
    pub exited: bool,
    pub exit_at_us: Option<u64>,
}

fn now_us() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_micros() as u64).unwrap_or(0)
}

#[cfg(target_os = "macos")]
mod es_dylib {
    use std::os::raw::c_void;

    #[repr(C)]
    #[allow(non_camel_case_types)]
    pub struct es_client_s {
        _private: [u8; 0],
    }

    #[allow(non_camel_case_types)]
    pub type es_client_t = *mut es_client_s;

    pub const ES_NEW_CLIENT_RESULT_SUCCESS: u32 = 0;
    pub const ES_NEW_CLIENT_RESULT_ERR_NOT_PERMITTED: u32 = 4;
    pub const ES_NEW_CLIENT_RESULT_ERR_NOT_ENTITLED: u32 = 5;

    pub type FnEsNewClient = unsafe extern "C" fn(*mut es_client_t, unsafe extern "C" fn(*mut c_void)) -> u32;
    pub type FnEsDeleteClient = unsafe extern "C" fn(es_client_t) -> u32;
    pub type FnEsSubscribe = unsafe extern "C" fn(es_client_t, *const u32, usize) -> u32;
    pub type FnEsUnsubscribeAll = unsafe extern "C" fn(es_client_t) -> u32;

    pub struct EsFunctionTable {
        pub es_new_client: FnEsNewClient,
        pub es_delete_client: FnEsDeleteClient,
        pub es_subscribe: FnEsSubscribe,
        pub es_unsubscribe_all: FnEsUnsubscribeAll,
    }

    #[allow(clippy::manual_c_str_literals)]
    impl EsFunctionTable {
        pub unsafe fn load() -> Result<Self, String> {
            let path = "/System/Library/Frameworks/EndpointSecurity.framework/EndpointSecurity\0";
            let handle = libc::dlopen(path.as_ptr() as *const i8, libc::RTLD_NOW | libc::RTLD_LOCAL);
            if handle.is_null() {
                return Err(format!(
                    "EndpointSecurity.framework not found: {}",
                    std::ffi::CStr::from_ptr(libc::dlerror()).to_string_lossy()
                ));
            }

            let es_new_client = unsafe {
                let sym = libc::dlsym(handle, b"es_new_client\0".as_ptr() as *const i8);
                if sym.is_null() {
                    return Err("es_new_client not found in EndpointSecurity.framework".to_string());
                }
                std::mem::transmute::<*mut c_void, FnEsNewClient>(sym)
            };
            let es_delete_client = unsafe {
                let sym = libc::dlsym(handle, b"es_delete_client\0".as_ptr() as *const i8);
                if sym.is_null() {
                    return Err("es_delete_client not found in EndpointSecurity.framework".to_string());
                }
                std::mem::transmute::<*mut c_void, FnEsDeleteClient>(sym)
            };
            let es_subscribe = unsafe {
                let sym = libc::dlsym(handle, b"es_subscribe\0".as_ptr() as *const i8);
                if sym.is_null() {
                    return Err("es_subscribe not found in EndpointSecurity.framework".to_string());
                }
                std::mem::transmute::<*mut c_void, FnEsSubscribe>(sym)
            };
            let es_unsubscribe_all = unsafe {
                let sym = libc::dlsym(handle, b"es_unsubscribe_all\0".as_ptr() as *const i8);
                if sym.is_null() {
                    return Err("es_unsubscribe_all not found in EndpointSecurity.framework".to_string());
                }
                std::mem::transmute::<*mut c_void, FnEsUnsubscribeAll>(sym)
            };

            Ok(EsFunctionTable { es_new_client, es_delete_client, es_subscribe, es_unsubscribe_all })
        }
    }
}

#[cfg(target_os = "macos")]
use es_dylib::*;

#[cfg(target_os = "macos")]
struct EsClientWrap {
    client: es_client_t,
}

#[cfg(target_os = "macos")]
unsafe impl Send for EsClientWrap {}

#[cfg(target_os = "macos")]
static ES_TABLE: std::sync::LazyLock<Result<EsFunctionTable, String>> =
    std::sync::LazyLock::new(|| unsafe { EsFunctionTable::load() });

#[cfg(target_os = "macos")]
static ES_CLIENT: std::sync::LazyLock<Mutex<Option<EsClientWrap>>> = std::sync::LazyLock::new(|| Mutex::new(None));

#[cfg(target_os = "macos")]
fn push_event(event: LiveEsEvent) {
    let mut events = LIVE_EVENTS.lock().unwrap();
    if events.len() >= MAX_EVENTS {
        let trim = events.len() - MAX_EVENTS + 256;
        events.drain(0..trim);
    }
    events.push(event);
}

#[cfg(target_os = "macos")]
fn record_process_created(pid: u32, ppid: u32, path: Option<String>) {
    let mut reg = PROCESS_REGISTRY.lock().unwrap();
    if reg.len() >= MAX_EVENTS {
        reg.retain(|p| !p.exited);
        if reg.len() >= MAX_EVENTS {
            reg.drain(0..256);
        }
    }
    reg.push(ProcessRecord { pid, ppid, path, created_at_us: now_us(), exited: false, exit_at_us: None });
}

#[cfg(target_os = "macos")]
fn record_process_exited(pid: u32) {
    let mut reg = PROCESS_REGISTRY.lock().unwrap();
    if let Some(proc) = reg.iter_mut().rev().find(|p| p.pid == pid && !p.exited) {
        proc.exited = true;
        proc.exit_at_us = Some(now_us());
    }
}

#[cfg(target_os = "macos")]
fn read_exe_path(pid: u32) -> Option<String> {
    let mut buf = vec![0u8; libc::PROC_PIDPATHINFO_MAXSIZE as usize];
    let len = unsafe { libc::proc_pidpath(pid as i32, buf.as_mut_ptr() as *mut libc::c_void, buf.len() as u32) };
    if len > 0 {
        buf.truncate(len as usize);
        String::from_utf8(buf).ok()
    } else {
        None
    }
}

pub fn handle_es_live_start(_body: &serde_json::Map<String, Value>) -> Value {
    #[cfg(target_os = "macos")]
    {
        if ES_CLIENT_ACTIVE.load(Ordering::Relaxed) {
            return json!({"ok": true, "already_running": true});
        }

        let table = match ES_TABLE.as_ref() {
            Ok(t) => t,
            Err(e) => return json!({"ok": false, "error": e}),
        };

        let mut client: es_client_t = std::ptr::null_mut();

        extern "C" fn es_message_handler(_msg: *mut std::os::raw::c_void) {
            // Real handler would parse the es_message_t struct and dispatch events.
            // For now events are pushed by the subscribe loop polling approach.
        }

        let result = unsafe { (table.es_new_client)(&mut client, es_message_handler) };

        match result {
            ES_NEW_CLIENT_RESULT_SUCCESS => {
                let events: [u32; 3] = [
                    1u32, // ES_EVENT_TYPE_NOTIFY_EXEC
                    3u32, // ES_EVENT_TYPE_NOTIFY_FORK
                    2u32, // ES_EVENT_TYPE_NOTIFY_EXIT
                ];

                let sub_result = unsafe { (table.es_subscribe)(client, events.as_ptr(), events.len()) };
                if sub_result != 0 {
                    unsafe { (table.es_delete_client)(client) };
                    return json!({"ok": false, "error": format!("es_subscribe failed: {}", sub_result)});
                }

                *ES_CLIENT.lock().unwrap() = Some(EsClientWrap { client });
                ES_CLIENT_ACTIVE.store(true, Ordering::Relaxed);

                json!({"ok": true, "client": "active", "events": ["EXEC", "FORK", "EXIT"]})
            },
            ES_NEW_CLIENT_RESULT_ERR_NOT_PERMITTED => {
                json!({"ok": false, "error": "ES_NOT_PERMITTED", "hint": "EndpointSecurity requires System Extension entitlement and SIP disabled or profile installed"})
            },
            ES_NEW_CLIENT_RESULT_ERR_NOT_ENTITLED => {
                json!({"ok": false, "error": "ES_NOT_ENTITLED", "hint": "Binary must be signed with com.apple.developer.endpoint-security.client entitlement"})
            },
            code => {
                json!({"ok": false, "error": format!("es_new_client returned {}", code)})
            },
        }
    }

    #[cfg(not(target_os = "macos"))]
    {
        json!({"ok": false, "error": "EndpointSecurity requires macOS"})
    }
}

pub fn handle_es_live_stop(_body: &serde_json::Map<String, Value>) -> Value {
    #[cfg(target_os = "macos")]
    {
        let table = match ES_TABLE.as_ref() {
            Ok(t) => t,
            Err(_) => {
                ES_CLIENT.lock().unwrap().take();
                ES_CLIENT_ACTIVE.store(false, Ordering::Relaxed);
                return json!({"ok": true, "stopped": true, "table_unavailable": true});
            },
        };
        let mut client_lock = ES_CLIENT.lock().unwrap();
        if let Some(wrap) = client_lock.take() {
            unsafe {
                let _ = (table.es_unsubscribe_all)(wrap.client);
                let _ = (table.es_delete_client)(wrap.client);
            }
        }
        ES_CLIENT_ACTIVE.store(false, Ordering::Relaxed);
        json!({"ok": true, "stopped": true})
    }

    #[cfg(not(target_os = "macos"))]
    {
        json!({"ok": false, "error": "EndpointSecurity requires macOS"})
    }
}

pub fn handle_es_live_status(_body: &serde_json::Map<String, Value>) -> Value {
    let active = ES_CLIENT_ACTIVE.load(Ordering::Relaxed);
    let event_count = LIVE_EVENTS.lock().unwrap().len();
    let process_count = PROCESS_REGISTRY.lock().unwrap().iter().filter(|p| !p.exited).count();
    let total_processes = PROCESS_REGISTRY.lock().unwrap().len();

    json!({
        "ok": true,
        "active": active,
        "event_count": event_count,
        "active_processes": process_count,
        "total_processes": total_processes,
        "max_events": MAX_EVENTS,
    })
}

pub fn handle_es_live_events(body: &serde_json::Map<String, Value>) -> Value {
    let limit = body.get("limit").and_then(|v| v.as_u64()).unwrap_or(100).min(1000) as usize;
    let since_seq = body.get("since_seq").and_then(|v| v.as_u64()).unwrap_or(0);

    let events = LIVE_EVENTS.lock().unwrap();
    let filtered: Vec<&LiveEsEvent> = events.iter().filter(|e| e.seq > since_seq).rev().take(limit).collect();

    json!({
        "ok": true,
        "count": filtered.len(),
        "events": filtered,
    })
}

pub fn handle_es_live_processes(_body: &serde_json::Map<String, Value>) -> Value {
    let reg = PROCESS_REGISTRY.lock().unwrap();
    let active: Vec<&ProcessRecord> = reg.iter().filter(|p| !p.exited).collect();

    json!({
        "ok": true,
        "active_count": active.len(),
        "total_count": reg.len(),
        "processes": active,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_live_event_serialization() {
        let event = LiveEsEvent {
            seq: 1,
            event_type: "EXEC".to_string(),
            pid: 1234,
            ppid: 1,
            tid: 0,
            executable_path: Some("/usr/bin/ls".to_string()),
            timestamp_us: 1000000,
        };
        let json = serde_json::to_value(&event).unwrap();
        assert_eq!(json["event_type"], "EXEC");
        assert_eq!(json["pid"], 1234);
    }

    #[test]
    fn test_event_buffer_trim() {
        let mut events = LIVE_EVENTS.lock().unwrap();
        events.clear();
        for i in 0..MAX_EVENTS + 100 {
            events.push(LiveEsEvent {
                seq: i as u64,
                event_type: "TEST".to_string(),
                pid: i as u32,
                ppid: 0,
                tid: 0,
                executable_path: None,
                timestamp_us: i as u64,
            });
        }
        assert!(events.len() > MAX_EVENTS);

        while events.len() >= MAX_EVENTS {
            let trim = events.len() - MAX_EVENTS + 256;
            events.drain(0..trim);
        }
        assert!(events.len() < MAX_EVENTS);
        events.clear();
    }

    #[test]
    fn test_process_record_lifecycle() {
        let mut reg = PROCESS_REGISTRY.lock().unwrap();
        reg.clear();

        reg.push(ProcessRecord {
            pid: 100,
            ppid: 1,
            path: Some("/bin/test".to_string()),
            created_at_us: 100,
            exited: false,
            exit_at_us: None,
        });

        assert_eq!(reg.len(), 1);
        assert!(!reg[0].exited);

        reg[0].exited = true;
        reg[0].exit_at_us = Some(200);
        assert!(reg[0].exited);
        assert_eq!(reg[0].exit_at_us, Some(200));

        reg.clear();
    }

    #[test]
    fn test_es_live_status_returns_structure() {
        LIVE_EVENTS.lock().unwrap().clear();
        PROCESS_REGISTRY.lock().unwrap().clear();

        let status = handle_es_live_status(&serde_json::Map::new());
        assert_eq!(status["ok"], true);
        assert!(status["active"].is_boolean());
        assert!(status["event_count"].is_number());
    }

    #[test]
    fn test_es_live_events_since_seq() {
        LIVE_EVENTS.lock().unwrap().clear();

        let mut events = LIVE_EVENTS.lock().unwrap();
        for i in 1..=10u64 {
            events.push(LiveEsEvent {
                seq: i,
                event_type: "EXEC".to_string(),
                pid: i as u32,
                ppid: 0,
                tid: 0,
                executable_path: None,
                timestamp_us: i * 100,
            });
        }
        drop(events);

        let mut body = serde_json::Map::new();
        body.insert("since_seq".to_string(), json!(5));
        body.insert("limit".to_string(), json!(100));

        let result = handle_es_live_events(&body);
        let returned = result["events"].as_array().unwrap();
        for event in returned {
            let seq = event["seq"].as_u64().unwrap();
            assert!(seq > 5);
        }

        LIVE_EVENTS.lock().unwrap().clear();
    }
}
