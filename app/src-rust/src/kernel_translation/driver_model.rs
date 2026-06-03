use serde_json::{json, Map, Value};
use std::collections::BTreeMap;
use std::sync::atomic::{AtomicU64, Ordering};

const MAX_COLLECTION_LEN: usize = 4096;

static NEXT_DRIVER_ID: AtomicU64 = AtomicU64::new(1);
static NEXT_DEVICE_ID: AtomicU64 = AtomicU64::new(1);
static NEXT_IRP_ID: AtomicU64 = AtomicU64::new(1);
static NEXT_IOCTL_ID: AtomicU64 = AtomicU64::new(1);

const METHOD_BUFFERED: u32 = 0;
const METHOD_IN_DIRECT: u32 = 1;
const METHOD_OUT_DIRECT: u32 = 2;
const METHOD_NEITHER: u32 = 3;

const FILE_DEVICE_UNKNOWN: u16 = 0x0022;
const FILE_DEVICE_DISK: u16 = 0x0007;
const FILE_DEVICE_NETWORK: u16 = 0x0012;
const FILE_DEVICE_SECURITY: u16 = 0x0082;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum IrpMajorFunction {
    Create,
    Close,
    Read,
    Write,
    DeviceControl,
    InternalDeviceControl,
    Shutdown,
    Cleanup,
    FlushBuffers,
    Pnp,
    Power,
    SystemControl,
}

impl IrpMajorFunction {
    pub fn nt_code(&self) -> u8 {
        match self {
            Self::Create => 0x00,
            Self::Close => 0x02,
            Self::Read => 0x03,
            Self::Write => 0x04,
            Self::DeviceControl => 0x0E,
            Self::InternalDeviceControl => 0x0F,
            Self::Shutdown => 0x10,
            Self::Cleanup => 0x12,
            Self::FlushBuffers => 0x08,
            Self::Pnp => 0x1B,
            Self::Power => 0x16,
            Self::SystemControl => 0x1E,
        }
    }

    pub fn nt_name(&self) -> &'static str {
        match self {
            Self::Create => "IRP_MJ_CREATE",
            Self::Close => "IRP_MJ_CLOSE",
            Self::Read => "IRP_MJ_READ",
            Self::Write => "IRP_MJ_WRITE",
            Self::DeviceControl => "IRP_MJ_DEVICE_CONTROL",
            Self::InternalDeviceControl => "IRP_MJ_INTERNAL_DEVICE_CONTROL",
            Self::Shutdown => "IRP_MJ_SHUTDOWN",
            Self::Cleanup => "IRP_MJ_CLEANUP",
            Self::FlushBuffers => "IRP_MJ_FLUSH_BUFFERS",
            Self::Pnp => "IRP_MJ_PNP",
            Self::Power => "IRP_MJ_POWER",
            Self::SystemControl => "IRP_MJ_SYSTEM_CONTROL",
        }
    }

    pub fn iokit_equivalent(&self) -> &'static str {
        match self {
            Self::Create => "IOServiceOpen -> IOUserClient::start",
            Self::Close => "IOServiceClose -> IOUserClient::stop",
            Self::Read => "IOUserClient::registerNotification or shared memory read",
            Self::Write => "IOUserClient::setProperties or shared memory write",
            Self::DeviceControl => "IOUserClient::externalMethod (selector dispatch)",
            Self::InternalDeviceControl => "IOUserClient::externalMethod (privileged selector)",
            Self::Shutdown => "IOService::systemWillShutdown notification",
            Self::Cleanup => "IOUserClient::clientClose",
            Self::FlushBuffers => "No direct equivalent — userspace flush",
            Self::Pnp => "IOService::message(kIOMessageServiceIsTerminated/requested)",
            Self::Power => "IOService::powerStateDidChangeTo",
            Self::SystemControl => "IORegistryEntry::setProperties",
        }
    }

    fn from_code(code: u8) -> Option<Self> {
        match code {
            0x00 => Some(Self::Create),
            0x02 => Some(Self::Close),
            0x03 => Some(Self::Read),
            0x04 => Some(Self::Write),
            0x08 => Some(Self::FlushBuffers),
            0x0E => Some(Self::DeviceControl),
            0x0F => Some(Self::InternalDeviceControl),
            0x10 => Some(Self::Shutdown),
            0x12 => Some(Self::Cleanup),
            0x16 => Some(Self::Power),
            0x1B => Some(Self::Pnp),
            0x1E => Some(Self::SystemControl),
            _ => None,
        }
    }

    fn from_name(s: &str) -> Option<Self> {
        match s {
            "IRP_MJ_CREATE" | "create" => Some(Self::Create),
            "IRP_MJ_CLOSE" | "close" => Some(Self::Close),
            "IRP_MJ_READ" | "read" => Some(Self::Read),
            "IRP_MJ_WRITE" | "write" => Some(Self::Write),
            "IRP_MJ_DEVICE_CONTROL" | "device_control" => Some(Self::DeviceControl),
            "IRP_MJ_INTERNAL_DEVICE_CONTROL" | "internal_device_control" => Some(Self::InternalDeviceControl),
            "IRP_MJ_SHUTDOWN" | "shutdown" => Some(Self::Shutdown),
            "IRP_MJ_CLEANUP" | "cleanup" => Some(Self::Cleanup),
            "IRP_MJ_FLUSH_BUFFERS" | "flush_buffers" => Some(Self::FlushBuffers),
            "IRP_MJ_PNP" | "pnp" => Some(Self::Pnp),
            "IRP_MJ_POWER" | "power" => Some(Self::Power),
            "IRP_MJ_SYSTEM_CONTROL" | "system_control" => Some(Self::SystemControl),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum DriverStatus {
    Unloaded,
    Loading,
    Loaded,
    Running,
    Stopped,
    Failed,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct NtDriver {
    pub id: u64,
    pub name: String,
    pub nt_driver_object: String,
    pub iokit_service_class: String,
    pub extension_type: ExtensionType,
    pub status: DriverStatus,
    pub dispatch_table: BTreeMap<String, String>,
    pub devices: Vec<u64>,
    pub created_at: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum ExtensionType {
    EndpointSecurity,
    NetworkExtension,
    DriverKit,
    Hybrid,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct NtDevice {
    pub id: u64,
    pub driver_id: u64,
    pub name: String,
    pub nt_device_name: String,
    pub dos_device_name: String,
    pub device_type: u16,
    pub iokit_user_client_class: String,
    pub characteristics: u32,
    pub created_at: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum IrpStatus {
    Pending,
    Completed,
    Error,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct NtIrp {
    pub id: u64,
    pub driver_id: u64,
    pub device_id: u64,
    pub major_function: IrpMajorFunction,
    pub minor_function: u8,
    pub status: IrpStatus,
    pub nt_status: u32,
    pub input_buffer: Option<String>,
    pub output_buffer: Option<String>,
    pub input_size: usize,
    pub output_size: usize,
    pub mach_message_id: u64,
    pub iokit_selector: u32,
    pub timestamp: u64,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct IoctlMapping {
    pub id: u64,
    pub nt_ioctl_code: u32,
    pub nt_name: String,
    pub device_type: u16,
    pub function: u16,
    pub access: u8,
    pub method: u32,
    pub iokit_selector: u32,
    pub mach_message_type: String,
    pub input_size: usize,
    pub output_size: usize,
    pub description: String,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct DriverTemplate {
    pub name: String,
    pub extension_type: ExtensionType,
    pub nt_callbacks: Vec<String>,
    pub iokit_methods: Vec<String>,
    pub es_subscriptions: Vec<String>,
    pub mach_services: Vec<String>,
}

static DRIVERS: std::sync::LazyLock<std::sync::Mutex<BTreeMap<u64, NtDriver>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(BTreeMap::new()));

static DEVICES: std::sync::LazyLock<std::sync::Mutex<BTreeMap<u64, NtDevice>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(BTreeMap::new()));

static IRPS: std::sync::LazyLock<std::sync::Mutex<Vec<NtIrp>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(Vec::new()));

static IOCTL_MAP: std::sync::LazyLock<std::sync::Mutex<Vec<IoctlMapping>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(Vec::new()));

fn lock_drivers() -> std::sync::MutexGuard<'static, BTreeMap<u64, NtDriver>> {
    match DRIVERS.lock() {
        Ok(g) => g,
        Err(e) => e.into_inner(),
    }
}

fn lock_devices() -> std::sync::MutexGuard<'static, BTreeMap<u64, NtDevice>> {
    match DEVICES.lock() {
        Ok(g) => g,
        Err(e) => e.into_inner(),
    }
}

fn lock_irps() -> std::sync::MutexGuard<'static, Vec<NtIrp>> {
    match IRPS.lock() {
        Ok(g) => g,
        Err(e) => e.into_inner(),
    }
}

fn lock_ioctl_map() -> std::sync::MutexGuard<'static, Vec<IoctlMapping>> {
    match IOCTL_MAP.lock() {
        Ok(g) => g,
        Err(e) => e.into_inner(),
    }
}

fn now_ms() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_millis() as u64
}

fn decode_ioctl(code: u32) -> (u16, u16, u8, u32) {
    let device_type = ((code >> 16) & 0xFFFF) as u16;
    let function = ((code >> 2) & 0xFFF) as u16;
    let access = ((code >> 14) & 0x3) as u8;
    let method = code & 0x3;
    (device_type, function, access, method)
}

fn encode_ioctl(device_type: u16, function: u16, access: u8, method: u32) -> u32 {
    ((device_type as u32) << 16) | ((access as u32) << 14) | ((function as u32) << 2) | (method & 0x3)
}

pub fn handle_load_driver(body: &Map<String, Value>) -> Value {
    let name = match body.get("name").and_then(|v| v.as_str()) {
        Some(n) => n.to_string(),
        None => return json!({"ok": false, "error": "name required"}),
    };

    let ext_type = match body.get("extension_type").and_then(|v| v.as_str()) {
        Some("endpoint_security") => ExtensionType::EndpointSecurity,
        Some("network_extension") => ExtensionType::NetworkExtension,
        Some("driver_kit") => ExtensionType::DriverKit,
        Some("hybrid") => ExtensionType::Hybrid,
        _ => ExtensionType::EndpointSecurity,
    };

    let id = NEXT_DRIVER_ID.fetch_add(1, Ordering::Relaxed);
    let driver = NtDriver {
        id,
        name: name.clone(),
        nt_driver_object: format!("\\Driver\\{}", name),
        iokit_service_class: format!("com.metalsharp.anticheat.{}", name.to_lowercase().replace(' ', "_")),
        extension_type: ext_type,
        status: DriverStatus::Loaded,
        dispatch_table: [
            ("IRP_MJ_CREATE", "IOServiceOpen"),
            ("IRP_MJ_CLOSE", "IOServiceClose"),
            ("IRP_MJ_DEVICE_CONTROL", "IOUserClient::externalMethod"),
            ("IRP_MJ_READ", "shared_memory_read"),
            ("IRP_MJ_WRITE", "shared_memory_write"),
            ("IRP_MJ_CLEANUP", "IOUserClient::clientClose"),
            ("IRP_MJ_SHUTDOWN", "systemWillShutdown"),
        ]
        .iter()
        .map(|(k, v)| (k.to_string(), v.to_string()))
        .collect(),
        devices: Vec::new(),
        created_at: now_ms(),
    };

    lock_drivers().insert(id, driver.clone());

    json!({
        "ok": true,
        "driver_id": id,
        "driver": driver,
        "translation": format!("NT DriverEntry({}) → macOS {} system extension activated", driver.nt_driver_object, match ext_type {
            ExtensionType::EndpointSecurity => "EndpointSecurity",
            ExtensionType::NetworkExtension => "NetworkExtension",
            ExtensionType::DriverKit => "DriverKit",
            ExtensionType::Hybrid => "Hybrid (ES + NE)",
        }),
    })
}

pub fn handle_unload_driver(body: &Map<String, Value>) -> Value {
    let id = match body.get("driver_id").and_then(|v| v.as_u64()) {
        Some(id) => id,
        None => return json!({"ok": false, "error": "driver_id required"}),
    };

    let mut drivers = lock_drivers();
    match drivers.remove(&id) {
        Some(removed) => {
            for device_id in &removed.devices {
                lock_devices().remove(device_id);
            }
            json!({"ok": true, "unloaded": removed})
        },
        None => json!({"ok": false, "error": format!("driver {} not found", id)}),
    }
}

pub fn handle_list_drivers(_body: &Map<String, Value>) -> Value {
    let drivers = lock_drivers();
    let list: Vec<&NtDriver> = drivers.values().collect();
    json!({
        "ok": true,
        "count": list.len(),
        "drivers": list,
    })
}

pub fn handle_create_device(body: &Map<String, Value>) -> Value {
    let driver_id = match body.get("driver_id").and_then(|v| v.as_u64()) {
        Some(id) => id,
        None => return json!({"ok": false, "error": "driver_id required"}),
    };

    let device_name = body.get("device_name").and_then(|v| v.as_str()).unwrap_or("AntiCheat0").to_string();
    let id = NEXT_DEVICE_ID.fetch_add(1, Ordering::Relaxed);
    let device = NtDevice {
        id,
        driver_id,
        name: device_name.clone(),
        nt_device_name: format!("\\Device\\{}", device_name),
        dos_device_name: format!("\\??\\{}", device_name),
        device_type: body.get("device_type").and_then(|v| v.as_u64()).unwrap_or(FILE_DEVICE_UNKNOWN as u64) as u16,
        iokit_user_client_class: format!("MetalSharp{}_UserClient", device_name.replace(' ', "")),
        characteristics: body.get("characteristics").and_then(|v| v.as_u64()).unwrap_or(0) as u32,
        created_at: now_ms(),
    };

    lock_devices().insert(id, device.clone());
    match lock_drivers().get_mut(&driver_id) {
        Some(d) => d.devices.push(id),
        None => return json!({"ok": false, "error": format!("driver {} not found during device creation", driver_id)}),
    }

    json!({
        "ok": true,
        "device_id": id,
        "device": device,
        "translation": format!("NT IoCreateDevice({}) → IOKit IOService + {} user client", device.nt_device_name, device.iokit_user_client_class),
    })
}

pub fn handle_list_devices(_body: &Map<String, Value>) -> Value {
    let devices = lock_devices();
    let list: Vec<&NtDevice> = devices.values().collect();
    json!({
        "ok": true,
        "count": list.len(),
        "devices": list,
    })
}

pub fn handle_dispatch_irp(body: &Map<String, Value>) -> Value {
    let driver_id = match body.get("driver_id").and_then(|v| v.as_u64()) {
        Some(id) => id,
        None => return json!({"ok": false, "error": "driver_id required"}),
    };
    let device_id = match body.get("device_id").and_then(|v| v.as_u64()) {
        Some(id) => id,
        None => return json!({"ok": false, "error": "device_id required"}),
    };
    let major_fn = match body.get("major_function").and_then(|v| v.as_str()).and_then(IrpMajorFunction::from_name) {
        Some(f) => f,
        None => {
            return json!({"ok": false, "error": "major_function required (e.g. device_control, create, close, read, write)"})
        },
    };

    let irp_id = NEXT_IRP_ID.fetch_add(1, Ordering::Relaxed);
    let iokit_selector =
        body.get("iokit_selector").and_then(|v| v.as_u64()).unwrap_or(major_fn.nt_code() as u64) as u32;
    let mach_msg_id = 0x80000000 + (driver_id as u64 * 256) + major_fn.nt_code() as u64;

    let irp = NtIrp {
        id: irp_id,
        driver_id,
        device_id,
        major_function: major_fn,
        minor_function: body.get("minor_function").and_then(|v| v.as_u64()).unwrap_or(0) as u8,
        status: IrpStatus::Completed,
        nt_status: 0,
        input_buffer: body.get("input_buffer").and_then(|v| v.as_str()).map(|s| s.to_string()),
        output_buffer: body.get("output_buffer").and_then(|v| v.as_str()).map(|s| s.to_string()),
        input_size: body.get("input_size").and_then(|v| v.as_u64()).unwrap_or(0) as usize,
        output_size: body.get("output_size").and_then(|v| v.as_u64()).unwrap_or(0) as usize,
        mach_message_id: mach_msg_id,
        iokit_selector,
        timestamp: now_ms(),
    };

    {
        let mut irps = lock_irps();
        irps.push(irp.clone());
        if irps.len() > MAX_COLLECTION_LEN {
            let excess = irps.len() - MAX_COLLECTION_LEN;
            irps.drain(0..excess);
        }
    }

    json!({
        "ok": true,
        "irp_id": irp_id,
        "irp": irp,
        "translation": {
            "nt_path": format!("{} -> {}(IRP)", major_fn.nt_name(), format!("\\Device\\{}", device_id)),
            "macos_path": format!("IOUserClient::externalMethod(selector={}) -> Mach msg 0x{:08X}", iokit_selector, mach_msg_id),
            "dispatch": format!("{} → {}", major_fn.nt_name(), major_fn.iokit_equivalent()),
        },
    })
}

pub fn handle_list_irps(_body: &Map<String, Value>) -> Value {
    let irps = lock_irps();
    json!({
        "ok": true,
        "count": irps.len(),
        "irps": irps.iter().collect::<Vec<_>>(),
    })
}

pub fn handle_register_ioctl(body: &Map<String, Value>) -> Value {
    let nt_ioctl_code = match body.get("nt_ioctl_code").and_then(|v| v.as_u64()) {
        Some(c) => c as u32,
        None => return json!({"ok": false, "error": "nt_ioctl_code required"}),
    };

    let id = NEXT_IOCTL_ID.fetch_add(1, Ordering::Relaxed);
    let (device_type, function, access, method) = decode_ioctl(nt_ioctl_code);

    let mapping = IoctlMapping {
        id,
        nt_ioctl_code,
        nt_name: body.get("nt_name").and_then(|v| v.as_str()).unwrap_or("unknown").to_string(),
        device_type,
        function,
        access,
        method,
        iokit_selector: body.get("iokit_selector").and_then(|v| v.as_u64()).unwrap_or(function as u64) as u32,
        mach_message_type: body
            .get("mach_message_type")
            .and_then(|v| v.as_str())
            .unwrap_or("async_notification")
            .to_string(),
        input_size: body.get("input_size").and_then(|v| v.as_u64()).unwrap_or(0) as usize,
        output_size: body.get("output_size").and_then(|v| v.as_u64()).unwrap_or(0) as usize,
        description: body.get("description").and_then(|v| v.as_str()).unwrap_or("").to_string(),
    };

    {
        let mut map = lock_ioctl_map();
        map.push(mapping.clone());
        if map.len() > MAX_COLLECTION_LEN {
            let excess = map.len() - MAX_COLLECTION_LEN;
            map.drain(0..excess);
        }
    }

    let method_name = match method {
        METHOD_BUFFERED => "METHOD_BUFFERED",
        METHOD_IN_DIRECT => "METHOD_IN_DIRECT",
        METHOD_OUT_DIRECT => "METHOD_OUT_DIRECT",
        METHOD_NEITHER => "METHOD_NEITHER",
        _ => "UNKNOWN",
    };

    json!({
        "ok": true,
        "ioctl_id": id,
        "mapping": mapping,
        "decoded": {
            "device_type": format!("0x{:04X}", device_type),
            "function": function,
            "access": format!("0x{:02X}", access),
            "method": method_name,
        },
        "translation": format!("DeviceIoControl(0x{:08X}) → IOUserClient::externalMethod(selector={})", nt_ioctl_code, mapping.iokit_selector),
    })
}

pub fn handle_decode_ioctl(body: &Map<String, Value>) -> Value {
    let code = match body.get("ioctl_code").and_then(|v| v.as_u64()) {
        Some(c) => c as u32,
        None => return json!({"ok": false, "error": "ioctl_code required"}),
    };

    let (device_type, function, access, method) = decode_ioctl(code);
    let reconstructed = encode_ioctl(device_type, function, access, method);

    let method_name = match method {
        METHOD_BUFFERED => "METHOD_BUFFERED",
        METHOD_IN_DIRECT => "METHOD_IN_DIRECT",
        METHOD_OUT_DIRECT => "METHOD_OUT_DIRECT",
        METHOD_NEITHER => "METHOD_NEITHER",
        _ => "UNKNOWN",
    };

    let known = lock_ioctl_map().iter().find(|m| m.nt_ioctl_code == code).cloned();

    json!({
        "ok": true,
        "ioctl_code": format!("0x{:08X}", code),
        "decoded": {
            "device_type": format!("0x{:04X}", device_type),
            "function": function,
            "access": format!("0x{:02X}", access),
            "method": method_name,
        },
        "reconstructed": format!("0x{:08X}", reconstructed),
        "matches_original": reconstructed == code,
        "known_mapping": known,
    })
}

pub fn handle_list_ioctls(_body: &Map<String, Value>) -> Value {
    let map = lock_ioctl_map();
    json!({
        "ok": true,
        "count": map.len(),
        "mappings": map.iter().collect::<Vec<_>>(),
    })
}

pub fn handle_type_mapping_survey(_body: &Map<String, Value>) -> Value {
    json!({
        "ok": true,
        "wdm_to_iokit": [
            {"nt": "DRIVER_OBJECT", "macos": "IOService subclass", "notes": "Driver state container. macOS: com_metalsharp_anticheat_* IOService."},
            {"nt": "DEVICE_OBJECT", "macos": "IOUserClient", "notes": "Device endpoint. User-mode opens via IOServiceOpen → IOUserClient."},
            {"nt": "IRP (I/O Request Packet)", "macos": "Mach message / IOExternalMethod", "notes": "Request/response unit. Serialized as Mach IPC message or IOExternalMethodDispatch."},
            {"nt": "DRIVER_DISPATCH", "macos": "IOUserClient::externalMethod", "notes": "Dispatch function for IRP_MJ_*. Maps to IOKit selector-based dispatch."},
            {"nt": "IO_STATUS_BLOCK", "macos": "Mach message return", "notes": "Status + bytes transferred. Encoded in Mach reply message."},
            {"nt": "UNICODE_STRING (dev name)", "macos": "IORegistryEntry name", "notes": "Device naming. \\Device\\AntiCheat → IORegistry /metalsharp/anticheat/0."},
            {"nt": "IO_DPC_ROUTINE", "macos": "IOWorkLoop + IOCommandGate", "notes": "Deferred procedure call. IOKit serialized work loop for safe concurrency."},
            {"nt": "KINTERRUPT", "macos": "IOFilterInterruptEventSource", "notes": "Interrupt handling. IOKit interrupt event source for hardware drivers."},
            {"nt": "DEVICE_EXTENSION", "macos": "IOService::fWorkspace", "notes": "Per-device private data. Stored in IOService member variables."},
        ],
        "irp_major_functions": [
            {"code": "0x00", "nt": "IRP_MJ_CREATE", "iokit": "IOServiceOpen → IOUserClient::start"},
            {"code": "0x02", "nt": "IRP_MJ_CLOSE", "iokit": "IOServiceClose → IOUserClient::stop"},
            {"code": "0x03", "nt": "IRP_MJ_READ", "iokit": "IOUserClient shared memory / registerNotification"},
            {"code": "0x04", "nt": "IRP_MJ_WRITE", "iokit": "IOUserClient setProperties / shared memory write"},
            {"code": "0x08", "nt": "IRP_MJ_FLUSH_BUFFERS", "iokit": "No direct equivalent (userspace flush)"},
            {"code": "0x0E", "nt": "IRP_MJ_DEVICE_CONTROL", "iokit": "IOUserClient::externalMethod (main anti-cheat path)"},
            {"code": "0x0F", "nt": "IRP_MJ_INTERNAL_DEVICE_CONTROL", "iokit": "IOUserClient::externalMethod (privileged)"},
            {"code": "0x10", "nt": "IRP_MJ_SHUTDOWN", "iokit": "IOService::systemWillShutdown"},
            {"code": "0x12", "nt": "IRP_MJ_CLEANUP", "iokit": "IOUserClient::clientClose"},
            {"code": "0x16", "nt": "IRP_MJ_POWER", "iokit": "IOService::powerStateDidChangeTo"},
            {"code": "0x1B", "nt": "IRP_MJ_PNP", "iokit": "IOService::message(kIOMessageService*)"},
            {"code": "0x1E", "nt": "IRP_MJ_SYSTEM_CONTROL", "iokit": "IORegistryEntry::setProperties"},
        ],
        "ioctl_encoding": "CTL_CODE(DeviceType, Function, Method, Access) = (DeviceType << 16) | (Access << 14) | (Function << 2) | Method",
        "communication_path": {
            "nt": "UserMode → DeviceIoControl(hDevice, ioctl, in, out) → KernelDriver → IRP_MJ_DEVICE_CONTROL → Result",
            "macos": "UserMode → IOConnectCallMethod(connect, selector, in, out) → IOUserClient::externalMethod → Mach reply → Result",
        },
    })
}

pub fn handle_extension_template(body: &Map<String, Value>) -> Value {
    let name = body.get("name").and_then(|v| v.as_str()).unwrap_or("MetalSharpAntiCheat").to_string();
    let ext_type = match body.get("extension_type").and_then(|v| v.as_str()) {
        Some("network_extension") => ExtensionType::NetworkExtension,
        Some("driver_kit") => ExtensionType::DriverKit,
        Some("hybrid") => ExtensionType::Hybrid,
        _ => ExtensionType::EndpointSecurity,
    };

    let template = DriverTemplate {
        name: name.clone(),
        extension_type: ext_type,
        nt_callbacks: vec![
            "PsSetCreateProcessNotifyRoutineEx2".to_string(),
            "PsSetCreateThreadNotifyRoutineEx".to_string(),
            "PsSetLoadImageNotifyRoutineEx".to_string(),
            "ObRegisterCallbacks".to_string(),
            "CmRegisterCallbackEx".to_string(),
        ],
        iokit_methods: vec![
            "start(IOService *provider)".to_string(),
            "stop(IOService *provider)".to_string(),
            "externalMethod(uint32_t selector, ...)".to_string(),
            "clientClose()".to_string(),
            "registerNotification(mach_port_t port)".to_string(),
        ],
        es_subscriptions: match ext_type {
            ExtensionType::EndpointSecurity | ExtensionType::Hybrid => vec![
                "ES_EVENT_TYPE_NOTIFY_EXEC".to_string(),
                "ES_EVENT_TYPE_NOTIFY_FORK".to_string(),
                "ES_EVENT_TYPE_NOTIFY_EXIT".to_string(),
                "ES_EVENT_TYPE_NOTIFY_MMAP".to_string(),
            ],
            _ => vec![],
        },
        mach_services: vec![
            format!("com.metalsharp.anticheat.{}", name.to_lowercase().replace(' ', "_")),
            format!("com.metalsharp.anticheat.{}.notifications", name.to_lowercase().replace(' ', "_")),
        ],
    };

    json!({
        "ok": true,
        "template": template,
        "scaffold": {
            "entry_point": format!("DriverEntry equivalent: {}::start()", name),
            "dispatch": format!("IRP_MJ_DEVICE_CONTROL → {}::externalMethod()", name),
            "subscriptions": template.es_subscriptions.len(),
            "mach_services": template.mach_services.len(),
            "files": [
                format!("{}.cpp — IOService subclass + IOUserClient", name),
                format!("{}Info.plist — Extension descriptor", name),
                format!("{}.entitlements — com.apple.developer.endpoint-security.client", name),
                "Makefile — meson build for MetalSharp integration".to_string(),
            ],
        },
    })
}

pub fn handle_seed_demo(_body: &Map<String, Value>) -> Value {
    let r1 = handle_load_driver(
        &serde_json::from_str("{\"name\": \"EasyAntiCheat\", \"extension_type\": \"endpoint_security\"}").unwrap(),
    );
    let driver_id = r1["driver_id"].as_u64().unwrap();

    let r2 = handle_create_device(
        &serde_json::from_str(&format!(
            "{{\"driver_id\": {}, \"device_name\": \"EasyAntiCheat0\", \"device_type\": {}}}",
            driver_id, FILE_DEVICE_UNKNOWN
        ))
        .unwrap(),
    );
    let device_id = r2["device_id"].as_u64().unwrap();

    let _ = handle_register_ioctl(&serde_json::from_str(
        "{\"nt_ioctl_code\": 2293760, \"nt_name\": \"IOCTL_EAC_PROCESS_EVENT\", \"iokit_selector\": 1, \"mach_message_type\": \"process_notification\", \"description\": \"Process create/exit notification from ES extension\"}"
    ).unwrap());

    let _ = handle_register_ioctl(&serde_json::from_str(
        "{\"nt_ioctl_code\": 2293764, \"nt_name\": \"IOCTL_EAC_IMAGE_EVENT\", \"iokit_selector\": 2, \"mach_message_type\": \"image_notification\", \"description\": \"Image load/unload notification from ES extension\"}"
    ).unwrap());

    let _ = handle_register_ioctl(&serde_json::from_str(
        "{\"nt_ioctl_code\": 2293768, \"nt_name\": \"IOCTL_EAC_MEMORY_SCAN\", \"iokit_selector\": 3, \"mach_message_type\": \"memory_scan_request\", \"input_size\": 256, \"output_size\": 1024, \"description\": \"Memory scan request/response\"}"
    ).unwrap());

    let op1 = handle_dispatch_irp(
        &serde_json::from_str(&format!(
            "{{\"driver_id\": {}, \"device_id\": {}, \"major_function\": \"create\"}}",
            driver_id, device_id
        ))
        .unwrap(),
    );

    let op2 = handle_dispatch_irp(&serde_json::from_str(&format!(
        "{{\"driver_id\": {}, \"device_id\": {}, \"major_function\": \"device_control\", \"iokit_selector\": 1, \"input_size\": 64}}",
        driver_id, device_id
    )).unwrap());

    let op3 = handle_dispatch_irp(
        &serde_json::from_str(&format!(
            "{{\"driver_id\": {}, \"device_id\": {}, \"major_function\": \"close\"}}",
            driver_id, device_id
        ))
        .unwrap(),
    );

    json!({
        "ok": true,
        "seeded": {
            "driver_id": driver_id,
            "device_id": device_id,
            "ioctls_registered": 3,
            "irps_dispatched": 3,
        },
        "irp_ids": [
            op1["irp_id"].as_u64().unwrap(),
            op2["irp_id"].as_u64().unwrap(),
            op3["irp_id"].as_u64().unwrap(),
        ],
        "scenario": "EAC-style anti-cheat: load driver → create device → register IOCTL handlers → open → send process event IOCTL → close",
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn empty_body() -> Map<String, Value> {
        Map::new()
    }

    #[test]
    fn test_load_driver() {
        let body: Map<String, Value> =
            serde_json::from_str("{\"name\": \"TestDriver\", \"extension_type\": \"endpoint_security\"}")
                .expect("seed demo json");
        let result = handle_load_driver(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["driver_id"].as_u64().unwrap() > 0);
        assert_eq!(result["driver"]["status"], "Loaded");
        assert!(result["driver"]["nt_driver_object"].as_str().unwrap().contains("TestDriver"));
    }

    #[test]
    fn test_load_driver_missing_name() {
        let body: Map<String, Value> = serde_json::from_str("{}").expect("seed demo json");
        let result = handle_load_driver(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_unload_driver() {
        let body: Map<String, Value> = serde_json::from_str("{\"name\": \"UnloadTest\"}").expect("seed demo json");
        let r = handle_load_driver(&body);
        let id = r["driver_id"].as_u64().unwrap();

        let unload: Map<String, Value> =
            serde_json::from_str(&format!("{{\"driver_id\": {}}}", id)).expect("seed demo json");
        let result = handle_unload_driver(&unload);
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["unloaded"]["devices"].is_array());
    }

    #[test]
    fn test_unload_unknown_driver() {
        let body: Map<String, Value> = serde_json::from_str("{\"driver_id\": 99999999}").expect("seed demo json");
        let result = handle_unload_driver(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_list_drivers() {
        let result = handle_list_drivers(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["count"].as_u64().is_some());
    }

    #[test]
    fn test_create_device() {
        let body: Map<String, Value> = serde_json::from_str("{\"name\": \"DevTest\"}").expect("seed demo json");
        let r = handle_load_driver(&body);
        let driver_id = r["driver_id"].as_u64().unwrap();

        let dev: Map<String, Value> =
            serde_json::from_str(&format!("{{\"driver_id\": {}, \"device_name\": \"TestDev0\"}}", driver_id))
                .expect("seed demo json");
        let result = handle_create_device(&dev);
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["device_id"].as_u64().unwrap() > 0);
        assert!(result["device"]["nt_device_name"].as_str().unwrap().contains("TestDev0"));
    }

    #[test]
    fn test_create_device_unknown_driver() {
        let body: Map<String, Value> =
            serde_json::from_str("{\"driver_id\": 99999999, \"device_name\": \"Bad\"}").expect("seed demo json");
        let result = handle_create_device(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_list_devices() {
        let result = handle_list_devices(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_dispatch_irp() {
        let body: Map<String, Value> = serde_json::from_str("{\"name\": \"IRPTest\"}").expect("seed demo json");
        let r = handle_load_driver(&body);
        let driver_id = r["driver_id"].as_u64().unwrap();

        let dev: Map<String, Value> =
            serde_json::from_str(&format!("{{\"driver_id\": {}}}", driver_id)).expect("seed demo json");
        let rd = handle_create_device(&dev);
        let device_id = rd["device_id"].as_u64().unwrap();

        let irp: Map<String, Value> = serde_json::from_str(&format!(
            "{{\"driver_id\": {}, \"device_id\": {}, \"major_function\": \"device_control\", \"iokit_selector\": 5}}",
            driver_id, device_id
        ))
        .unwrap();
        let result = handle_dispatch_irp(&irp);
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["irp_id"].as_u64().unwrap() > 0);
        assert_eq!(result["irp"]["major_function"], "DeviceControl");
        assert!(result["translation"]["dispatch"].as_str().unwrap().contains("IRP_MJ_DEVICE_CONTROL"));
    }

    #[test]
    fn test_dispatch_irp_missing_params() {
        let body: Map<String, Value> = serde_json::from_str("{}").expect("seed demo json");
        let result = handle_dispatch_irp(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_dispatch_irp_invalid_function() {
        let body: Map<String, Value> =
            serde_json::from_str("{\"driver_id\": 1, \"device_id\": 1, \"major_function\": \"invalid\"}")
                .expect("seed demo json");
        let result = handle_dispatch_irp(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_list_irps() {
        let result = handle_list_irps(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_register_ioctl() {
        let code = encode_ioctl(FILE_DEVICE_UNKNOWN, 0x800, 3, METHOD_BUFFERED);
        let body: Map<String, Value> = serde_json::from_str(&format!(
            "{{\"nt_ioctl_code\": {}, \"nt_name\": \"IOCTL_TEST\", \"iokit_selector\": 1}}",
            code
        ))
        .unwrap();
        let result = handle_register_ioctl(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["decoded"]["method"].as_str().unwrap().contains("BUFFERED"));
    }

    #[test]
    fn test_register_ioctl_missing_code() {
        let body: Map<String, Value> = serde_json::from_str("{\"nt_name\": \"test\"}").expect("seed demo json");
        let result = handle_register_ioctl(&body);
        assert!(!result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_decode_ioctl() {
        let code = encode_ioctl(0x0022, 0x800, 3, METHOD_NEITHER);
        let body: Map<String, Value> =
            serde_json::from_str(&format!("{{\"ioctl_code\": {}}}", code)).expect("seed demo json");
        let result = handle_decode_ioctl(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["decoded"]["device_type"], format!("0x{:04X}", 0x0022));
        assert_eq!(result["decoded"]["function"], 0x800);
        assert!(result["matches_original"].as_bool().unwrap());
    }

    #[test]
    fn test_decode_ioctl_known_mapping() {
        let code = encode_ioctl(0x0022, 0x900, 0, METHOD_BUFFERED);
        handle_register_ioctl(
            &serde_json::from_str(&format!("{{\"nt_ioctl_code\": {}, \"nt_name\": \"IOCTL_KNOWN_TEST\"}}", code))
                .unwrap(),
        );

        let body: Map<String, Value> =
            serde_json::from_str(&format!("{{\"ioctl_code\": {}}}", code)).expect("seed demo json");
        let result = handle_decode_ioctl(&body);
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["known_mapping"].is_object());
        assert_eq!(result["known_mapping"]["nt_name"], "IOCTL_KNOWN_TEST");
    }

    #[test]
    fn test_list_ioctls() {
        let result = handle_list_ioctls(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
    }

    #[test]
    fn test_type_mapping_survey() {
        let result = handle_type_mapping_survey(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["wdm_to_iokit"].as_array().unwrap().len() >= 8);
        assert!(result["irp_major_functions"].as_array().unwrap().len() >= 10);
    }

    #[test]
    fn test_extension_template() {
        let result = handle_extension_template(
            &serde_json::from_str("{\"name\": \"TestAC\", \"extension_type\": \"endpoint_security\"}").unwrap(),
        );
        assert!(result["ok"].as_bool().unwrap());
        assert!(result["template"]["es_subscriptions"].as_array().unwrap().len() >= 3);
        assert!(result["scaffold"]["files"].as_array().unwrap().len() >= 3);
    }

    #[test]
    fn test_irp_major_function_roundtrip() {
        for code in [0x00, 0x02, 0x03, 0x04, 0x08, 0x0E, 0x0F, 0x10, 0x12, 0x16, 0x1B, 0x1E] {
            let mf = IrpMajorFunction::from_code(code).unwrap();
            assert_eq!(mf.nt_code(), code);
        }
    }

    #[test]
    fn test_ioctl_encode_decode_roundtrip() {
        let original = encode_ioctl(0x0022, 0x800, 3, METHOD_BUFFERED);
        let (dt, func, acc, meth) = decode_ioctl(original);
        assert_eq!(dt, 0x0022);
        assert_eq!(func, 0x800);
        assert_eq!(acc, 3);
        assert_eq!(meth, METHOD_BUFFERED);
        assert_eq!(encode_ioctl(dt, func, acc, meth), original);
    }

    #[test]
    fn test_seed_demo() {
        let result = handle_seed_demo(&empty_body());
        assert!(result["ok"].as_bool().unwrap());
        assert_eq!(result["seeded"]["ioctls_registered"], 3);
        assert_eq!(result["seeded"]["irps_dispatched"], 3);
        assert_eq!(result["irp_ids"].as_array().unwrap().len(), 3);
    }
}
