use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::io::{Read, Write};
use std::os::unix::net::{UnixListener, UnixStream};
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use std::thread;

const MS_IPC_MAGIC: u32 = 0x4D534B54;
const MS_IPC_VERSION: u16 = 1;
const IPC_HEADER_SIZE: usize = 16;

const OP_NT_OPEN_PROCESS: u16 = 0x0001;
const OP_NT_OPEN_THREAD: u16 = 0x0002;
const OP_NT_QUERY_SYSTEM_INFO: u16 = 0x0003;
const OP_NT_QUERY_INFORMATION_PROC: u16 = 0x0004;
const OP_NT_CLOSE: u16 = 0x0007;
const OP_NT_DEVICE_IO_CONTROL: u16 = 0x000D;

static IPC_LISTENER_RUNNING: AtomicBool = AtomicBool::new(false);
static NEXT_REQUEST_ID: AtomicU32 = AtomicU32::new(1);
static NEXT_VIRTUAL_HANDLE: AtomicU64 = AtomicU64::new(0x00000100);

static VIRTUAL_HANDLES: std::sync::LazyLock<std::sync::Mutex<BTreeMap<u64, VirtualHandleEntry>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(BTreeMap::new()));

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
struct VirtualHandleEntry {
    handle: u64,
    pid: u32,
    tid: u32,
    access_mask: u32,
    handle_type: String,
}

fn lock_handles() -> std::sync::MutexGuard<'static, BTreeMap<u64, VirtualHandleEntry>> {
    match VIRTUAL_HANDLES.lock() {
        Ok(g) => g,
        Err(e) => e.into_inner(),
    }
}

fn alloc_handle(pid: u32, tid: u32, access_mask: u32, handle_type: &str) -> u64 {
    let handle = NEXT_VIRTUAL_HANDLE.fetch_add(4, Ordering::Relaxed);
    let entry = VirtualHandleEntry { handle, pid, tid, access_mask, handle_type: handle_type.to_string() };
    lock_handles().insert(handle, entry);
    handle
}

#[repr(C, packed)]
struct IpcHeader {
    magic: u32,
    version: u16,
    operation: u16,
    request_id: u32,
    body_size: u32,
}

fn read_exact_u8(stream: &mut UnixStream, len: usize) -> std::io::Result<Vec<u8>> {
    let mut buf = vec![0u8; len];
    stream.read_exact(&mut buf)?;
    Ok(buf)
}

fn handle_ipc_request(operation: u16, body: &[u8]) -> Vec<u8> {
    match operation {
        OP_NT_OPEN_PROCESS => handle_nt_open_process(body),
        OP_NT_OPEN_THREAD => handle_nt_open_thread(body),
        OP_NT_QUERY_SYSTEM_INFO => handle_nt_query_system_info(body),
        OP_NT_QUERY_INFORMATION_PROC => handle_nt_query_info_process(body),
        OP_NT_CLOSE => handle_nt_close(body),
        OP_NT_DEVICE_IO_CONTROL => handle_nt_device_io_control(body),
        _ => {
            let resp = json!({"nt_status": 0xC0000002u32 as i64, "data_length": 0u32});
            serde_json::to_vec(&resp).unwrap_or_default()
        },
    }
}

fn handle_nt_open_process(body: &[u8]) -> Vec<u8> {
    if body.len() < 12 {
        let resp = json!({"nt_status": 0xC000000Du32 as i64, "handle": 0u64});
        return serde_json::to_vec(&resp).unwrap_or_default();
    }
    let desired_access = u32::from_le_bytes(body[0..4].try_into().unwrap_or([0; 4]));
    let _inherit = u32::from_le_bytes(body[4..8].try_into().unwrap_or([0; 4]));
    let pid = u32::from_le_bytes(body[8..12].try_into().unwrap_or([0; 4]));

    let handle = alloc_handle(pid, 0, desired_access, "Process");

    let resp = json!({
        "nt_status": 0u32,
        "handle": handle,
    });
    serde_json::to_vec(&resp).unwrap_or_default()
}

fn handle_nt_open_thread(body: &[u8]) -> Vec<u8> {
    if body.len() < 12 {
        let resp = json!({"nt_status": 0xC000000Du32 as i64, "handle": 0u64});
        return serde_json::to_vec(&resp).unwrap_or_default();
    }
    let desired_access = u32::from_le_bytes(body[0..4].try_into().unwrap_or([0; 4]));
    let _inherit = u32::from_le_bytes(body[4..8].try_into().unwrap_or([0; 4]));
    let tid = u32::from_le_bytes(body[8..12].try_into().unwrap_or([0; 4]));

    let handle = alloc_handle(0, tid, desired_access, "Thread");

    let resp = json!({
        "nt_status": 0u32,
        "handle": handle,
    });
    serde_json::to_vec(&resp).unwrap_or_default()
}

fn handle_nt_query_system_info(body: &[u8]) -> Vec<u8> {
    if body.len() < 8 {
        let resp = json!({"nt_status": 0xC000000Du32 as i64, "return_length": 0u32, "buffer_length": 0u32});
        return serde_json::to_vec(&resp).unwrap_or_default();
    }
    let _info_class = u32::from_le_bytes(body[0..4].try_into().unwrap_or([0; 4]));
    let _buf_len = u32::from_le_bytes(body[4..8].try_into().unwrap_or([0; 4]));

    let handles = lock_handles();
    let process_count = handles.values().filter(|h| h.handle_type == "Process").count() as u32;

    let resp = json!({
        "nt_status": 0u32,
        "return_length": process_count * 8,
        "buffer_length": 0u32,
        "process_count": process_count,
    });
    serde_json::to_vec(&resp).unwrap_or_default()
}

fn handle_nt_query_info_process(body: &[u8]) -> Vec<u8> {
    if body.len() < 12 {
        let resp = json!({"nt_status": 0xC000000Du32 as i64, "return_length": 0u32, "buffer_length": 0u32});
        return serde_json::to_vec(&resp).unwrap_or_default();
    }
    let _handle = u64::from_le_bytes(body[0..8].try_into().unwrap_or([0; 8]));
    let info_class = u32::from_le_bytes(body[8..12].try_into().unwrap_or([0; 4]));
    let _buf_len = u32::from_le_bytes(body[12..16].try_into().unwrap_or([0; 4]));

    match info_class {
        0x1e => {
            let resp = json!({
                "nt_status": 0u32,
                "return_length": 4u32,
                "buffer_length": 4u32,
                "debug_port": 0u64,
                "no_debug_inherit": 1u32,
            });
            serde_json::to_vec(&resp).unwrap_or_default()
        },
        0x1f => {
            let resp = json!({
                "nt_status": 0u32,
                "return_length": 4u32,
                "buffer_length": 4u32,
                "process_debug_flags": 1u32,
            });
            serde_json::to_vec(&resp).unwrap_or_default()
        },
        0x00 => {
            let resp = json!({
                "nt_status": 0u32,
                "return_length": 0u32,
                "buffer_length": 0u32,
                "basic_information": true,
                "exit_status": 259u32,
                "peb_base_address": "0x0000000000000000",
            });
            serde_json::to_vec(&resp).unwrap_or_default()
        },
        _ => {
            let resp = json!({
                "nt_status": 0xC0000003u32 as i64,
                "return_length": 0u32,
                "buffer_length": 0u32,
            });
            serde_json::to_vec(&resp).unwrap_or_default()
        },
    }
}

fn handle_nt_close(body: &[u8]) -> Vec<u8> {
    if body.len() < 8 {
        let resp = json!({"nt_status": 0xC0000008u32 as i64});
        return serde_json::to_vec(&resp).unwrap_or_default();
    }
    let handle = u64::from_le_bytes(body[0..8].try_into().unwrap_or([0; 8]));
    let removed = lock_handles().remove(&handle).is_some();
    let status = if removed { 0u32 } else { 0xC0000008u32 };
    let resp = json!({"nt_status": status as i64});
    serde_json::to_vec(&resp).unwrap_or_default()
}

fn handle_nt_device_io_control(body: &[u8]) -> Vec<u8> {
    if body.len() < 20 {
        let resp = json!({"nt_status": 0xC000000Du32 as i64, "bytes_returned": 0u32, "output_buffer_length": 0u32});
        return serde_json::to_vec(&resp).unwrap_or_default();
    }
    let _handle = u64::from_le_bytes(body[0..8].try_into().unwrap_or([0; 8]));
    let ioctl_code = u32::from_le_bytes(body[8..12].try_into().unwrap_or([0; 4]));
    let input_len = u32::from_le_bytes(body[12..16].try_into().unwrap_or([0; 4]));
    let output_len = u32::from_le_bytes(body[16..20].try_into().unwrap_or([0; 4]));

    let _input_data = if body.len() > 20 && input_len as usize <= body.len() - 20 {
        &body[20..20 + input_len as usize]
    } else {
        &body[20..]
    };

    let resp = json!({
        "nt_status": 0u32,
        "bytes_returned": 0u32,
        "output_buffer_length": output_len,
        "ioctl_code": format!("0x{:08X}", ioctl_code),
        "note": "device_io_control intercepted by kernel translation layer",
    });
    serde_json::to_vec(&resp).unwrap_or_default()
}

fn handle_client(mut stream: UnixStream) {
    loop {
        let header_buf = match read_exact_u8(&mut stream, IPC_HEADER_SIZE) {
            Ok(b) => b,
            Err(_) => return,
        };

        if header_buf.len() < IPC_HEADER_SIZE {
            return;
        }

        let magic = u32::from_le_bytes(header_buf[0..4].try_into().unwrap_or([0; 4]));
        let version = u16::from_le_bytes(header_buf[4..6].try_into().unwrap_or([0; 2]));
        let operation = u16::from_le_bytes(header_buf[6..8].try_into().unwrap_or([0; 2]));
        let _request_id = u32::from_le_bytes(header_buf[8..12].try_into().unwrap_or([0; 4]));
        let body_size = u32::from_le_bytes(header_buf[12..16].try_into().unwrap_or([0; 4]));

        if magic != MS_IPC_MAGIC || version != MS_IPC_VERSION {
            let _ = stream.write_all(&0xC0000001u32.to_le_bytes());
            return;
        }

        let body = if body_size > 0 && body_size <= 65536 {
            match read_exact_u8(&mut stream, body_size as usize) {
                Ok(b) => b,
                Err(_) => return,
            }
        } else {
            Vec::new()
        };

        let response = handle_ipc_request(operation, &body);
        let resp_header = IpcHeader {
            magic: MS_IPC_MAGIC,
            version: MS_IPC_VERSION,
            operation,
            request_id: NEXT_REQUEST_ID.fetch_add(1, Ordering::Relaxed),
            body_size: response.len() as u32,
        };

        let mut out = Vec::with_capacity(IPC_HEADER_SIZE + response.len());
        out.extend_from_slice(&resp_header.magic.to_le_bytes());
        out.extend_from_slice(&resp_header.version.to_le_bytes());
        out.extend_from_slice(&resp_header.operation.to_le_bytes());
        out.extend_from_slice(&resp_header.request_id.to_le_bytes());
        out.extend_from_slice(&resp_header.body_size.to_le_bytes());
        out.extend_from_slice(&response);

        if stream.write_all(&out).is_err() {
            return;
        }
    }
}

pub fn handle_ipc_status(_body: &serde_json::Map<String, Value>) -> Value {
    json!({
        "ok": true,
        "socket_path": crate::kernel_translation::ipc_bridge::IPC_SOCKET_PATH,
        "running": IPC_LISTENER_RUNNING.load(Ordering::Relaxed),
        "virtual_handles": lock_handles().len(),
    })
}

pub fn handle_ipc_handles(_body: &serde_json::Map<String, Value>) -> Value {
    let handles = lock_handles();
    let entries: Vec<Value> = handles
        .values()
        .map(|h| {
            json!({
                "handle": format!("0x{:08X}", h.handle),
                "pid": h.pid,
                "tid": h.tid,
                "access_mask": format!("0x{:08X}", h.access_mask),
                "type": h.handle_type,
            })
        })
        .collect();
    json!({
        "ok": true,
        "count": entries.len(),
        "handles": entries,
    })
}

pub fn start_ipc_listener() -> Result<(), String> {
    if IPC_LISTENER_RUNNING.load(Ordering::Relaxed) {
        return Ok(());
    }

    let socket_path = IPC_SOCKET_PATH.to_string();
    let _ = std::fs::remove_file(&socket_path);

    let listener =
        UnixListener::bind(&socket_path).map_err(|e| format!("IPC bind failed at {}: {}", socket_path, e))?;

    IPC_LISTENER_RUNNING.store(true, Ordering::Relaxed);

    thread::spawn(move || {
        for stream in listener.incoming() {
            match stream {
                Ok(stream) => {
                    thread::spawn(|| handle_client(stream));
                },
                Err(_) => {
                    if !IPC_LISTENER_RUNNING.load(Ordering::Relaxed) {
                        break;
                    }
                },
            }
        }
        let _ = std::fs::remove_file(&socket_path);
    });

    Ok(())
}

pub fn stop_ipc_listener() -> Value {
    IPC_LISTENER_RUNNING.store(false, Ordering::Relaxed);
    let _ = std::fs::remove_file(IPC_SOCKET_PATH);
    json!({"ok": true, "stopped": true})
}

pub const IPC_SOCKET_PATH: &str = "/tmp/metalsharp-kernel-translation.sock";
