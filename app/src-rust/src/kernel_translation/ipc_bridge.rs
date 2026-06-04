use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use std::sync::Mutex;
use std::thread;

const MS_IPC_MAGIC: u32 = 0x4D534B54;
const MS_IPC_VERSION: u16 = 1;
const IPC_HEADER_SIZE: usize = 16;
const MAX_CONCURRENT_CLIENTS: usize = 16;

const OP_NT_OPEN_PROCESS: u16 = 0x0001;
const OP_NT_OPEN_THREAD: u16 = 0x0002;
const OP_NT_QUERY_SYSTEM_INFO: u16 = 0x0003;
const OP_NT_QUERY_INFORMATION_PROC: u16 = 0x0004;
const OP_NT_QUERY_OBJECT: u16 = 0x0005;
const OP_NT_SET_INFORMATION_THREAD: u16 = 0x0006;
const OP_NT_CLOSE: u16 = 0x0007;
const OP_NT_QUERY_VIRTUAL_MEMORY: u16 = 0x0009;
const OP_NT_DEVICE_IO_CONTROL: u16 = 0x000D;

const STATUS_SUCCESS: i32 = 0;
const STATUS_NOT_IMPLEMENTED: i32 = 0xC0000002_u32 as i32;
const STATUS_INVALID_PARAMETER: i32 = 0xC000000D_u32 as i32;
const STATUS_INVALID_HANDLE: i32 = 0xC0000008_u32 as i32;
const STATUS_ACCESS_DENIED: i32 = 0xC0000022_u32 as i32;
const STATUS_INFO_LENGTH_MISMATCH: i32 = 0xC0000004_u32 as i32;

static IPC_LISTENER_RUNNING: AtomicBool = AtomicBool::new(false);
static IPC_ACTIVE_CLIENTS: AtomicU32 = AtomicU32::new(0);
static NEXT_REQUEST_ID: AtomicU32 = AtomicU32::new(1);
static NEXT_VIRTUAL_HANDLE: AtomicU64 = AtomicU64::new(0x00000100);

static IPC_LISTENER: std::sync::LazyLock<Mutex<Option<TcpListener>>> = std::sync::LazyLock::new(|| Mutex::new(None));

static VIRTUAL_HANDLES: std::sync::LazyLock<std::sync::Mutex<BTreeMap<u64, VirtualHandleEntry>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(BTreeMap::new()));

#[derive(Debug, Clone)]
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

fn read_exact_u8(stream: &mut TcpStream, len: usize) -> std::io::Result<Vec<u8>> {
    let mut buf = vec![0u8; len];
    stream.read_exact(&mut buf)?;
    Ok(buf)
}

fn pack_i32(v: i32) -> [u8; 4] {
    v.to_le_bytes()
}

fn pack_u32(v: u32) -> [u8; 4] {
    v.to_le_bytes()
}

fn pack_u64(v: u64) -> [u8; 8] {
    v.to_le_bytes()
}

fn unpack_u32(buf: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(buf[offset..offset + 4].try_into().unwrap_or([0; 4]))
}

fn unpack_u64(buf: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(buf[offset..offset + 8].try_into().unwrap_or([0; 8]))
}

#[cfg(target_os = "macos")]
fn xnu_process_exists(pid: u32) -> bool {
    unsafe {
        let mut info: [i32; 4096] = [0; 4096];
        let size = std::mem::size_of::<i32>() * info.len();
        let kr = libc::proc_pidinfo(
            pid as i32,
            libc::PROC_PIDTASKINFO,
            0,
            info.as_mut_ptr() as *mut libc::c_void,
            size as i32,
        );
        kr > 0
    }
}

#[cfg(not(target_os = "macos"))]
fn xnu_process_exists(_pid: u32) -> bool {
    true
}

#[cfg(target_os = "macos")]
const PROC_PIDLISTTHREADS: i32 = 4;

#[cfg(target_os = "macos")]
fn xnu_thread_exists(tid: u32) -> bool {
    unsafe {
        let pid = getpid();
        let mut buf: [u8; 32768] = [0; 32768];
        let count =
            libc::proc_pidinfo(pid, PROC_PIDLISTTHREADS, 0, buf.as_mut_ptr() as *mut libc::c_void, buf.len() as i32);
        if count <= 0 {
            return false;
        }
        let thread_count = (count as usize) / std::mem::size_of::<u64>();
        if thread_count == 0 {
            return false;
        }
        let thread_ids = std::slice::from_raw_parts(buf.as_ptr() as *const u64, thread_count);
        thread_ids.iter().any(|&t| t as u32 == tid)
    }
}

#[cfg(not(target_os = "macos"))]
fn xnu_thread_exists(_tid: u32) -> bool {
    true
}

#[cfg(target_os = "macos")]
fn getpid() -> i32 {
    unsafe { libc::getpid() }
}

#[cfg(not(target_os = "macos"))]
fn getpid() -> i32 {
    0
}

fn handle_ipc_request(operation: u16, body: &[u8]) -> Vec<u8> {
    match operation {
        OP_NT_OPEN_PROCESS => handle_nt_open_process(body),
        OP_NT_OPEN_THREAD => handle_nt_open_thread(body),
        OP_NT_QUERY_SYSTEM_INFO => handle_nt_query_system_info(body),
        OP_NT_QUERY_INFORMATION_PROC => handle_nt_query_info_process(body),
        OP_NT_QUERY_OBJECT => handle_nt_query_object(body),
        OP_NT_SET_INFORMATION_THREAD => handle_nt_set_information_thread(body),
        OP_NT_CLOSE => handle_nt_close(body),
        OP_NT_QUERY_VIRTUAL_MEMORY => handle_nt_query_virtual_memory(body),
        OP_NT_DEVICE_IO_CONTROL => handle_nt_device_io_control(body),
        _ => {
            let mut resp = Vec::with_capacity(8);
            resp.extend_from_slice(&pack_i32(STATUS_NOT_IMPLEMENTED));
            resp.extend_from_slice(&pack_u32(0));
            resp
        },
    }
}

fn handle_nt_open_process(body: &[u8]) -> Vec<u8> {
    if body.len() < 12 {
        let mut resp = Vec::with_capacity(12);
        resp.extend_from_slice(&pack_i32(STATUS_INVALID_PARAMETER));
        resp.extend_from_slice(&pack_u64(0));
        return resp;
    }
    let desired_access = unpack_u32(body, 0);
    let _inherit = unpack_u32(body, 4);
    let pid = unpack_u32(body, 8);

    if !xnu_process_exists(pid) {
        let mut resp = Vec::with_capacity(12);
        resp.extend_from_slice(&pack_i32(STATUS_ACCESS_DENIED));
        resp.extend_from_slice(&pack_u64(0));
        return resp;
    }

    let handle = alloc_handle(pid, 0, desired_access, "Process");

    let mut resp = Vec::with_capacity(12);
    resp.extend_from_slice(&pack_i32(STATUS_SUCCESS));
    resp.extend_from_slice(&pack_u64(handle));
    resp
}

fn handle_nt_open_thread(body: &[u8]) -> Vec<u8> {
    if body.len() < 12 {
        let mut resp = Vec::with_capacity(12);
        resp.extend_from_slice(&pack_i32(STATUS_INVALID_PARAMETER));
        resp.extend_from_slice(&pack_u64(0));
        return resp;
    }
    let desired_access = unpack_u32(body, 0);
    let _inherit = unpack_u32(body, 4);
    let tid = unpack_u32(body, 8);

    if !xnu_thread_exists(tid) {
        let mut resp = Vec::with_capacity(12);
        resp.extend_from_slice(&pack_i32(STATUS_ACCESS_DENIED));
        resp.extend_from_slice(&pack_u64(0));
        return resp;
    }

    let handle = alloc_handle(0, tid, desired_access, "Thread");

    let mut resp = Vec::with_capacity(12);
    resp.extend_from_slice(&pack_i32(STATUS_SUCCESS));
    resp.extend_from_slice(&pack_u64(handle));
    resp
}

fn handle_nt_query_system_info(body: &[u8]) -> Vec<u8> {
    if body.len() < 8 {
        let mut resp = Vec::with_capacity(12);
        resp.extend_from_slice(&pack_i32(STATUS_INVALID_PARAMETER));
        resp.extend_from_slice(&pack_u32(0));
        resp.extend_from_slice(&pack_u32(0));
        return resp;
    }
    let info_class = unpack_u32(body, 0);
    let buf_len = unpack_u32(body, 4);

    let handles = lock_handles();
    let process_count = handles.values().filter(|h| h.handle_type == "Process").count() as u32;

    match info_class {
        0x10 => {
            let needed: usize = process_count as usize * 56 + 8;
            if (buf_len as usize) < needed {
                let mut resp = Vec::with_capacity(12);
                resp.extend_from_slice(&pack_i32(STATUS_INFO_LENGTH_MISMATCH));
                resp.extend_from_slice(&pack_u32(needed as u32));
                resp.extend_from_slice(&pack_u32(0));
                return resp;
            }
            let mut data = Vec::with_capacity(8 + process_count as usize * 56);
            data.extend_from_slice(&pack_u32(process_count));
            data.extend_from_slice(&pack_u32(0));
            for h in handles.values().filter(|h| h.handle_type == "Process") {
                data.extend_from_slice(&pack_u32(next_handle_delta(&handles, h.pid)));
                data.extend_from_slice(&pack_u32(h.pid));
                data.extend_from_slice(&[0u8; 48]);
                data.extend_from_slice(&pack_u32(0));
            }
            let mut resp = Vec::with_capacity(12 + data.len());
            resp.extend_from_slice(&pack_i32(STATUS_SUCCESS));
            resp.extend_from_slice(&pack_u32(data.len() as u32));
            resp.extend_from_slice(&pack_u32(data.len() as u32));
            resp.extend_from_slice(&data);
            resp
        },
        _ => {
            let mut resp = Vec::with_capacity(12);
            resp.extend_from_slice(&pack_i32(STATUS_SUCCESS));
            resp.extend_from_slice(&pack_u32(process_count * 8));
            resp.extend_from_slice(&pack_u32(0));
            resp
        },
    }
}

fn next_handle_delta(handles: &BTreeMap<u64, VirtualHandleEntry>, pid: u32) -> u32 {
    for (i, h) in handles.values().enumerate() {
        if h.pid == pid {
            return (i + 1) as u32;
        }
    }
    0
}

fn handle_nt_query_info_process(body: &[u8]) -> Vec<u8> {
    if body.len() < 12 {
        let mut resp = Vec::with_capacity(12);
        resp.extend_from_slice(&pack_i32(STATUS_INVALID_PARAMETER));
        resp.extend_from_slice(&pack_u32(0));
        resp.extend_from_slice(&pack_u32(0));
        return resp;
    }
    let _handle = unpack_u64(body, 0);
    let info_class = unpack_u32(body, 8);
    let _buf_len = unpack_u32(body, 12);

    match info_class {
        0x07 => {
            let mut resp = Vec::with_capacity(20);
            resp.extend_from_slice(&pack_i32(STATUS_SUCCESS));
            resp.extend_from_slice(&pack_u32(8));
            resp.extend_from_slice(&pack_u32(8));
            resp.extend_from_slice(&pack_u32(getpid() as u32));
            resp.extend_from_slice(&pack_u32(getpid() as u32));
            resp
        },
        0x1e => {
            let mut resp = Vec::with_capacity(16);
            resp.extend_from_slice(&pack_i32(STATUS_SUCCESS));
            resp.extend_from_slice(&pack_u32(4));
            resp.extend_from_slice(&pack_u32(4));
            resp.extend_from_slice(&pack_u32(0));
            resp
        },
        0x1f => {
            let mut resp = Vec::with_capacity(16);
            resp.extend_from_slice(&pack_i32(STATUS_SUCCESS));
            resp.extend_from_slice(&pack_u32(4));
            resp.extend_from_slice(&pack_u32(4));
            resp.extend_from_slice(&pack_u32(1));
            resp
        },
        0x00 => {
            let mut resp = Vec::with_capacity(28);
            resp.extend_from_slice(&pack_i32(STATUS_SUCCESS));
            resp.extend_from_slice(&pack_u32(16));
            resp.extend_from_slice(&pack_u32(16));
            resp.extend_from_slice(&pack_i32(259));
            resp.extend_from_slice(&pack_u64(0));
            resp.extend_from_slice(&pack_u32(0));
            resp
        },
        _ => {
            let mut resp = Vec::with_capacity(12);
            resp.extend_from_slice(&pack_i32(STATUS_NOT_IMPLEMENTED));
            resp.extend_from_slice(&pack_u32(0));
            resp.extend_from_slice(&pack_u32(0));
            resp
        },
    }
}

fn handle_nt_close(body: &[u8]) -> Vec<u8> {
    if body.len() < 8 {
        return pack_i32(STATUS_INVALID_HANDLE).to_vec();
    }
    let handle = unpack_u64(body, 0);
    let removed = lock_handles().remove(&handle).is_some();
    let status = if removed { STATUS_SUCCESS } else { STATUS_INVALID_HANDLE };
    pack_i32(status).to_vec()
}

fn handle_nt_device_io_control(body: &[u8]) -> Vec<u8> {
    if body.len() < 20 {
        let mut resp = Vec::with_capacity(12);
        resp.extend_from_slice(&pack_i32(STATUS_INVALID_PARAMETER));
        resp.extend_from_slice(&pack_u32(0));
        resp.extend_from_slice(&pack_u32(0));
        return resp;
    }
    let _handle = unpack_u64(body, 0);
    let ioctl_code = unpack_u32(body, 8);
    let input_len = unpack_u32(body, 12);
    let output_len = unpack_u32(body, 16);

    let _input_data = if body.len() > 20 && input_len as usize <= body.len() - 20 {
        &body[20..20 + input_len as usize]
    } else {
        &body[20..]
    };

    match ioctl_code {
        0x00090000..=0x0009FFFF => {
            let mut resp = Vec::with_capacity(12 + output_len as usize);
            resp.extend_from_slice(&pack_i32(STATUS_SUCCESS));
            resp.extend_from_slice(&pack_u32(0));
            resp.extend_from_slice(&pack_u32(output_len));
            if output_len > 0 {
                resp.extend_from_slice(&vec![0u8; output_len as usize]);
            }
            resp
        },
        _ => {
            let mut resp = Vec::with_capacity(12);
            resp.extend_from_slice(&pack_i32(STATUS_NOT_IMPLEMENTED));
            resp.extend_from_slice(&pack_u32(0));
            resp.extend_from_slice(&pack_u32(0));
            resp
        },
    }
}

fn handle_nt_query_virtual_memory(body: &[u8]) -> Vec<u8> {
    if body.len() < 20 {
        let mut resp = Vec::with_capacity(32);
        resp.extend_from_slice(&pack_i32(STATUS_INVALID_PARAMETER));
        resp.extend_from_slice(&pack_u32(0));
        resp.extend_from_slice(&pack_u32(0));
        resp.extend_from_slice(&pack_u64(0));
        resp.extend_from_slice(&pack_u64(0));
        resp.extend_from_slice(&pack_u32(0));
        resp.extend_from_slice(&pack_u32(0));
        resp.extend_from_slice(&pack_u32(0));
        return resp;
    }
    let _process_handle = unpack_u64(body, 0);
    let base_address = unpack_u64(body, 8);
    let info_class = unpack_u32(body, 16);
    let _buf_len = unpack_u32(body, 20);

    match info_class {
        0x00 => {
            let mut resp = Vec::with_capacity(32);
            resp.extend_from_slice(&pack_i32(STATUS_SUCCESS));
            resp.extend_from_slice(&pack_u32(48));
            resp.extend_from_slice(&pack_u32(48));
            resp.extend_from_slice(&pack_u64(base_address));
            resp.extend_from_slice(&pack_u64(0x10000));
            resp.extend_from_slice(&pack_u32(0x1000));
            resp.extend_from_slice(&pack_u32(0x04));
            resp.extend_from_slice(&pack_u32(0x00020000));
            resp
        },
        _ => {
            let mut resp = Vec::with_capacity(32);
            resp.extend_from_slice(&pack_i32(STATUS_NOT_IMPLEMENTED));
            resp.extend_from_slice(&pack_u32(0));
            resp.extend_from_slice(&pack_u32(0));
            resp.extend_from_slice(&pack_u64(0));
            resp.extend_from_slice(&pack_u64(0));
            resp.extend_from_slice(&pack_u32(0));
            resp.extend_from_slice(&pack_u32(0));
            resp.extend_from_slice(&pack_u32(0));
            resp
        },
    }
}

fn handle_nt_query_object(body: &[u8]) -> Vec<u8> {
    if body.len() < 12 {
        let mut resp = Vec::with_capacity(12);
        resp.extend_from_slice(&pack_i32(STATUS_INVALID_PARAMETER));
        resp.extend_from_slice(&pack_u32(0));
        resp.extend_from_slice(&pack_u32(0));
        return resp;
    }
    let _handle = unpack_u64(body, 0);
    let info_class = unpack_u32(body, 8);
    let _buf_len = unpack_u32(body, 12);

    match info_class {
        0x01 => {
            let mut resp = Vec::with_capacity(28);
            resp.extend_from_slice(&pack_i32(STATUS_SUCCESS));
            resp.extend_from_slice(&pack_u32(24));
            resp.extend_from_slice(&pack_u32(24));
            resp.extend_from_slice(&pack_u32(0x001A0001));
            resp.extend_from_slice(&pack_u32(0x00000001));
            resp.extend_from_slice(&pack_u32(0));
            resp.extend_from_slice(&pack_u32(0));
            resp.extend_from_slice(&pack_u32(0));
            resp
        },
        0x02 => {
            let mut resp = Vec::with_capacity(16);
            resp.extend_from_slice(&pack_i32(STATUS_SUCCESS));
            resp.extend_from_slice(&pack_u32(8));
            resp.extend_from_slice(&pack_u32(8));
            resp.extend_from_slice(&pack_u32(0x000F001F));
            resp.extend_from_slice(&pack_u32(0));
            resp
        },
        _ => {
            let mut resp = Vec::with_capacity(12);
            resp.extend_from_slice(&pack_i32(STATUS_NOT_IMPLEMENTED));
            resp.extend_from_slice(&pack_u32(0));
            resp.extend_from_slice(&pack_u32(0));
            resp
        },
    }
}

fn handle_nt_set_information_thread(body: &[u8]) -> Vec<u8> {
    if body.len() < 12 {
        return pack_i32(STATUS_INVALID_PARAMETER).to_vec();
    }
    let _thread_handle = unpack_u64(body, 0);
    let info_class = unpack_u32(body, 8);

    match info_class {
        0x11 => pack_i32(STATUS_SUCCESS).to_vec(),
        _ => pack_i32(STATUS_NOT_IMPLEMENTED).to_vec(),
    }
}

fn handle_client(mut stream: TcpStream) {
    let active = IPC_ACTIVE_CLIENTS.fetch_add(1, Ordering::Relaxed);
    if active >= MAX_CONCURRENT_CLIENTS as u32 {
        IPC_ACTIVE_CLIENTS.fetch_sub(1, Ordering::Relaxed);
        return;
    }

    let result = handle_client_inner(&mut stream);

    IPC_ACTIVE_CLIENTS.fetch_sub(1, Ordering::Relaxed);
    let _ = result;
}

fn handle_client_inner(stream: &mut TcpStream) -> Result<(), ()> {
    loop {
        let header_buf = read_exact_u8(stream, IPC_HEADER_SIZE).map_err(|_| ())?;

        if header_buf.len() < IPC_HEADER_SIZE {
            return Err(());
        }

        let magic = u32::from_le_bytes(header_buf[0..4].try_into().unwrap_or([0; 4]));
        let version = u16::from_le_bytes(header_buf[4..6].try_into().unwrap_or([0; 2]));
        let operation = u16::from_le_bytes(header_buf[6..8].try_into().unwrap_or([0; 2]));
        let _request_id = u32::from_le_bytes(header_buf[8..12].try_into().unwrap_or([0; 4]));
        let body_size = u32::from_le_bytes(header_buf[12..16].try_into().unwrap_or([0; 4]));

        if magic != MS_IPC_MAGIC || version != MS_IPC_VERSION {
            let _ = stream.write_all(&0xC0000001u32.to_le_bytes());
            return Err(());
        }

        let body = if body_size > 0 && body_size <= 65536 {
            read_exact_u8(stream, body_size as usize).map_err(|_| ())?
        } else {
            Vec::new()
        };

        let response = handle_ipc_request(operation, &body);

        let resp_body_size = response.len() as u32;
        let resp_request_id = NEXT_REQUEST_ID.fetch_add(1, Ordering::Relaxed);

        let mut out = Vec::with_capacity(IPC_HEADER_SIZE + response.len());
        out.extend_from_slice(&MS_IPC_MAGIC.to_le_bytes());
        out.extend_from_slice(&MS_IPC_VERSION.to_le_bytes());
        out.extend_from_slice(&operation.to_le_bytes());
        out.extend_from_slice(&resp_request_id.to_le_bytes());
        out.extend_from_slice(&resp_body_size.to_le_bytes());
        out.extend_from_slice(&response);

        stream.write_all(&out).map_err(|_| ())?;
    }
}

pub fn handle_ipc_status(_body: &serde_json::Map<String, Value>) -> Value {
    json!({
        "ok": true,
        "bind_addr": crate::kernel_translation::ipc_bridge::IPC_BIND_ADDR,
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

    let listener =
        TcpListener::bind(IPC_BIND_ADDR).map_err(|e| format!("IPC TCP bind failed at {}: {}", IPC_BIND_ADDR, e))?;

    IPC_LISTENER_RUNNING.store(true, Ordering::Relaxed);
    *IPC_LISTENER.lock().unwrap() = Some(listener.try_clone().map_err(|e| format!("clone failed: {}", e))?);

    thread::spawn(move || {
        for stream in listener.incoming() {
            if !IPC_LISTENER_RUNNING.load(Ordering::Relaxed) {
                break;
            }
            match stream {
                Ok(stream) => {
                    if IPC_ACTIVE_CLIENTS.load(Ordering::Relaxed) < MAX_CONCURRENT_CLIENTS as u32 {
                        thread::spawn(|| handle_client(stream));
                    }
                },
                Err(_) => {
                    if !IPC_LISTENER_RUNNING.load(Ordering::Relaxed) {
                        break;
                    }
                },
            }
        }
    });

    Ok(())
}

pub fn stop_ipc_listener() -> Value {
    IPC_LISTENER_RUNNING.store(false, Ordering::Relaxed);
    let listener = IPC_LISTENER.lock().unwrap().take();
    drop(listener);
    json!({"ok": true, "stopped": true, "active_clients": IPC_ACTIVE_CLIENTS.load(Ordering::Relaxed)})
}

pub const IPC_BIND_ADDR: &str = "127.0.0.1:19384";

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_alloc_handle_increments() {
        VIRTUAL_HANDLES.lock().unwrap().clear();

        let h1 = alloc_handle(100, 0, 0x1F0FFF, "Process");
        let h2 = alloc_handle(200, 0, 0x1F0FFF, "Process");

        assert!(h1 >= 0x100);
        assert_eq!(h2, h1 + 4);

        let handles = lock_handles();
        assert_eq!(handles.len(), 2);
        assert_eq!(handles[&h1].pid, 100);
        assert_eq!(handles[&h2].pid, 200);
    }

    #[test]
    fn test_nt_open_process_returns_handle() {
        VIRTUAL_HANDLES.lock().unwrap().clear();
        let mut req = Vec::new();
        req.extend_from_slice(&0x1F0FFFu32.to_le_bytes());
        req.extend_from_slice(&0u32.to_le_bytes());
        req.extend_from_slice(&getpid().to_le_bytes());

        let resp = handle_nt_open_process(&req);
        assert_eq!(resp.len(), 12);
        let status = i32::from_le_bytes(resp[0..4].try_into().unwrap());
        assert_eq!(status, STATUS_SUCCESS);
        let handle = u64::from_le_bytes(resp[4..12].try_into().unwrap());
        assert!(handle >= 0x100);
    }

    #[test]
    fn test_nt_close_removes_handle() {
        VIRTUAL_HANDLES.lock().unwrap().clear();
        let h = alloc_handle(42, 0, 0, "Process");

        let mut req = Vec::new();
        req.extend_from_slice(&h.to_le_bytes());

        let resp = handle_nt_close(&req);
        let status = i32::from_le_bytes(resp[0..4].try_into().unwrap());
        assert_eq!(status, STATUS_SUCCESS);

        let resp2 = handle_nt_close(&req);
        let status2 = i32::from_le_bytes(resp2[0..4].try_into().unwrap());
        assert_eq!(status2, STATUS_INVALID_HANDLE);
    }

    #[test]
    fn test_nt_query_info_process_debug_flags() {
        let mut req = Vec::new();
        req.extend_from_slice(&0x1234u64.to_le_bytes());
        req.extend_from_slice(&0x1Fu32.to_le_bytes());
        req.extend_from_slice(&4u32.to_le_bytes());

        let resp = handle_nt_query_info_process(&req);
        let status = i32::from_le_bytes(resp[0..4].try_into().unwrap());
        assert_eq!(status, STATUS_SUCCESS);
        let flags = u32::from_le_bytes(resp[12..16].try_into().unwrap());
        assert_eq!(flags, 1);
    }

    #[test]
    fn test_nt_device_io_control_fsctl_ioctl_succeeds() {
        let mut req = Vec::new();
        req.extend_from_slice(&0x5678u64.to_le_bytes());
        req.extend_from_slice(&0x00090000u32.to_le_bytes());
        req.extend_from_slice(&0u32.to_le_bytes());
        req.extend_from_slice(&64u32.to_le_bytes());

        let resp = handle_nt_device_io_control(&req);
        let status = i32::from_le_bytes(resp[0..4].try_into().unwrap());
        assert_eq!(status, STATUS_SUCCESS);
        assert!(resp.len() >= 12);
    }

    #[test]
    fn test_ipc_header_roundtrip() {
        VIRTUAL_HANDLES.lock().unwrap().clear();

        let mut req = Vec::new();
        req.extend_from_slice(&MS_IPC_MAGIC.to_le_bytes());
        req.extend_from_slice(&MS_IPC_VERSION.to_le_bytes());
        req.extend_from_slice(&OP_NT_CLOSE.to_le_bytes());
        req.extend_from_slice(&1u32.to_le_bytes());
        req.extend_from_slice(&8u32.to_le_bytes());

        req.extend_from_slice(&0x1234u64.to_le_bytes());

        assert_eq!(req.len(), 24);

        let op = u16::from_le_bytes(req[6..8].try_into().unwrap());
        assert_eq!(op, OP_NT_CLOSE);

        let body_size = u32::from_le_bytes(req[12..16].try_into().unwrap());
        assert_eq!(body_size, 8);
    }

    #[test]
    fn test_protocol_constants_match_dll() {
        assert_eq!(MS_IPC_MAGIC, 0x4D534B54u32);
        assert_eq!(MS_IPC_VERSION, 1u16);
        assert_eq!(IPC_HEADER_SIZE, 16usize);
        assert_eq!(OP_NT_OPEN_PROCESS, 0x0001u16);
        assert_eq!(OP_NT_OPEN_THREAD, 0x0002u16);
        assert_eq!(OP_NT_QUERY_OBJECT, 0x0005u16);
        assert_eq!(OP_NT_SET_INFORMATION_THREAD, 0x0006u16);
        assert_eq!(OP_NT_CLOSE, 0x0007u16);
        assert_eq!(OP_NT_QUERY_VIRTUAL_MEMORY, 0x0009u16);
        assert_eq!(OP_NT_DEVICE_IO_CONTROL, 0x000Du16);
        assert_eq!(STATUS_SUCCESS, 0i32);
        assert_eq!(STATUS_NOT_IMPLEMENTED, 0xC0000002_u32 as i32);
        assert_eq!(STATUS_INVALID_PARAMETER, 0xC000000D_u32 as i32);
        assert_eq!(STATUS_INVALID_HANDLE, 0xC0000008_u32 as i32);
        assert_eq!(STATUS_ACCESS_DENIED, 0xC0000022_u32 as i32);
    }

    #[test]
    fn test_nt_close_error_path_uses_pack() {
        let resp = handle_nt_close(&[0u8; 4]);
        assert_eq!(resp.len(), 4);
        let status = i32::from_le_bytes(resp[0..4].try_into().unwrap());
        assert_eq!(status, STATUS_INVALID_HANDLE);
    }

    #[test]
    fn test_nt_device_io_control_unknown_ioctl_returns_not_implemented() {
        let mut req = Vec::new();
        req.extend_from_slice(&0x5678u64.to_le_bytes());
        req.extend_from_slice(&0xDEAD0000u32.to_le_bytes());
        req.extend_from_slice(&0u32.to_le_bytes());
        req.extend_from_slice(&0u32.to_le_bytes());

        let resp = handle_nt_device_io_control(&req);
        let status = i32::from_le_bytes(resp[0..4].try_into().unwrap());
        assert_eq!(status, STATUS_NOT_IMPLEMENTED);
    }

    #[test]
    fn test_nt_query_virtual_memory_basic_returns_success() {
        let mut req = Vec::new();
        req.extend_from_slice(&0x0u64.to_le_bytes());
        req.extend_from_slice(&0x10000u64.to_le_bytes());
        req.extend_from_slice(&0x00u32.to_le_bytes());
        req.extend_from_slice(&48u32.to_le_bytes());

        let resp = handle_nt_query_virtual_memory(&req);
        assert!(resp.len() >= 32);
        let status = i32::from_le_bytes(resp[0..4].try_into().unwrap());
        assert_eq!(status, STATUS_SUCCESS);
    }

    #[test]
    fn test_nt_query_object_type_info_returns_success() {
        let mut req = Vec::new();
        req.extend_from_slice(&0x100u64.to_le_bytes());
        req.extend_from_slice(&0x01u32.to_le_bytes());
        req.extend_from_slice(&24u32.to_le_bytes());

        let resp = handle_nt_query_object(&req);
        let status = i32::from_le_bytes(resp[0..4].try_into().unwrap());
        assert_eq!(status, STATUS_SUCCESS);
    }

    #[test]
    fn test_nt_set_information_thread_hide_from_debugger() {
        let mut req = Vec::new();
        req.extend_from_slice(&0x100u64.to_le_bytes());
        req.extend_from_slice(&0x11u32.to_le_bytes());
        req.extend_from_slice(&4u32.to_le_bytes());

        let resp = handle_nt_set_information_thread(&req);
        let status = i32::from_le_bytes(resp[0..4].try_into().unwrap());
        assert_eq!(status, STATUS_SUCCESS);
    }
}
