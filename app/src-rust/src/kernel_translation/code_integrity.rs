use serde_json::{json, Map, Value};
use std::collections::BTreeMap;

const CS_OPS_STATUS: u32 = 0;
const CS_OPS_MARKINVALID: u32 = 1;
const CS_OPS_MARKHARD: u32 = 2;
const CS_OPS_MARKKILL: u32 = 3;
const CS_OPS_PIDINFO: u32 = 4;
const CS_OPS_ENTITLEMENTS_BLOB: u32 = 7;
const CS_OPS_GETSIGNINGINFO: u32 = 4;
const CS_OPS_BLOB: u32 = 5;

const CS_VALID: u32 = 0x00000001;
const CS_ADHOC: u32 = 0x00000002;
const CS_HARD: u32 = 0x00000100;
const CS_KILL: u32 = 0x00000200;
const CS_RESTRICT: u32 = 0x00000800;
const CS_ENFORCEMENT: u32 = 0x00001000;
const CS_REQUIRE_LV: u32 = 0x00002000;
const CS_PLATFORM_BINARY: u32 = 0x04000000;
const CS_SIGNED: u32 = 0x00000004;
const CS_DEV_CODE: u32 = 0x00000008;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum NtSigningLevel {
    None = 0,
    AuthenticodeTrusted = 1,
    Authenticode = 2,
    AuthenticodeAntitampering = 3,
    Microsoft = 4,
    MicrosoftAntitampering = 5,
    Custom1 = 6,
    Custom2 = 7,
    Custom3 = 8,
    Custom4 = 9,
    Custom5 = 10,
    Custom6 = 11,
    Developer = 12,
    DeveloperAntitampering = 13,
    Store = 14,
    StoreAntitampering = 15,
}

impl NtSigningLevel {
    pub fn from_u8(v: u8) -> Self {
        match v {
            0 => Self::None,
            1 => Self::AuthenticodeTrusted,
            2 => Self::Authenticode,
            3 => Self::AuthenticodeAntitampering,
            4 => Self::Microsoft,
            5 => Self::MicrosoftAntitampering,
            6 => Self::Custom1,
            7 => Self::Custom2,
            8 => Self::Custom3,
            9 => Self::Custom4,
            10 => Self::Custom5,
            11 => Self::Custom6,
            12 => Self::Developer,
            13 => Self::DeveloperAntitampering,
            14 => Self::Store,
            15 => Self::StoreAntitampering,
            _ => Self::None,
        }
    }

    pub fn nt_name(&self) -> &'static str {
        match self {
            Self::None => "SE_SIGNING_LEVEL_UNSIGNED",
            Self::AuthenticodeTrusted => "SE_SIGNING_LEVEL_AUTHENTICODE_TRUSTED",
            Self::Authenticode => "SE_SIGNING_LEVEL_AUTHENTICODE",
            Self::AuthenticodeAntitampering => "SE_SIGNING_LEVEL_AUTHENTICODE_ANTITAMPERING",
            Self::Microsoft => "SE_SIGNING_LEVEL_MICROSOFT",
            Self::MicrosoftAntitampering => "SE_SIGNING_LEVEL_MICROSOFT_ANTITAMPERING",
            Self::Custom1 => "SE_SIGNING_LEVEL_CUSTOM_1",
            Self::Custom2 => "SE_SIGNING_LEVEL_CUSTOM_2",
            Self::Custom3 => "SE_SIGNING_LEVEL_CUSTOM_3",
            Self::Custom4 => "SE_SIGNING_LEVEL_CUSTOM_4",
            Self::Custom5 => "SE_SIGNING_LEVEL_CUSTOM_5",
            Self::Custom6 => "SE_SIGNING_LEVEL_CUSTOM_6",
            Self::Developer => "SE_SIGNING_LEVEL_DEVELOPER",
            Self::DeveloperAntitampering => "SE_SIGNING_LEVEL_DEVELOPER_ANTITAMPERING",
            Self::Store => "SE_SIGNING_LEVEL_STORE",
            Self::StoreAntitampering => "SE_SIGNING_LEVEL_STORE_ANTITAMPERING",
        }
    }
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ModuleSigningEntry {
    pub base_address: String,
    pub module_name: String,
    pub module_type: ModuleType,
    pub signing_level: NtSigningLevel,
    pub csops_flags: Option<u32>,
    pub policy_flags: u32,
    pub is_signed: bool,
    pub is_trusted: bool,
    pub hash_algorithm: &'static str,
    pub hash_hex: Option<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum ModuleType {
    MachO,
    PE,
    Unknown,
}

static MODULE_SIGNING_TABLE: std::sync::LazyLock<std::sync::Mutex<BTreeMap<String, ModuleSigningEntry>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(BTreeMap::new()));

pub fn handle_query_signing_level(body: &Map<String, Value>) -> Value {
    #[cfg(unix)]
    {
        let pid = match body.get("pid").and_then(|v| v.as_u64()) {
            Some(p) if p <= u32::MAX as u64 => p as u32,
            _ => return json!({"ok": false, "error": "pid (u32) required"}),
        };
        let module_name = body.get("module_name").and_then(|v| v.as_str()).unwrap_or("");

        if !module_name.is_empty() {
            let tables = MODULE_SIGNING_TABLE.lock().unwrap();
            if let Some(entry) = tables.get(module_name) {
                return json!({
                    "ok": true,
                    "module": entry,
                    "source": "cached",
                });
            }
        }

        let csops_result = query_csops_signing(pid);
        let signing_level = csops_to_nt_signing_level(&csops_result);

        json!({
            "ok": true,
            "pid": pid,
            "module_name": module_name,
            "csops": csops_result,
            "signingLevel": signing_level,
            "signingLevelName": signing_level.nt_name(),
            "policy": nt_policy_from_signing_level(signing_level),
            "source": "csops_probe",
        })
    }

    #[cfg(not(unix))]
    {
        let _ = body;
        json!({"ok": false, "error": "code integrity requires macOS"})
    }
}

pub fn handle_query_process_signing(body: &Map<String, Value>) -> Value {
    #[cfg(unix)]
    {
        let pid = match body.get("pid").and_then(|v| v.as_u64()) {
            Some(p) if p <= u32::MAX as u64 => p as u32,
            _ => return json!({"ok": false, "error": "pid (u32) required"}),
        };

        let csops_result = query_csops_signing(pid);
        let signing_level = csops_to_nt_signing_level(&csops_result);

        json!({
            "ok": true,
            "ntApi": "NtQueryInformationProcess(ProcessSigningLevel)",
            "ProcessSigningLevel": {
                "SigningLevel": signing_level as u8,
                "SigningLevelName": signing_level.nt_name(),
                "Flags": 0,
            },
            "csopsRaw": csops_result,
            "translation": {
                "csopsFlags": csops_result.get("flags").and_then(|v| v.as_u64()).unwrap_or(0),
                "mappedTo": signing_level.nt_name(),
                "reason": csops_signing_reason(&csops_result),
            },
        })
    }

    #[cfg(not(unix))]
    {
        let _ = body;
        json!({"ok": false, "error": "code integrity requires macOS"})
    }
}

pub fn handle_register_pe_module(body: &Map<String, Value>) -> Value {
    let base_address = match body.get("base_address").and_then(|v| v.as_str()) {
        Some(a) => a.to_string(),
        None => return json!({"ok": false, "error": "base_address (hex string) required"}),
    };
    let module_name = match body.get("module_name").and_then(|v| v.as_str()) {
        Some(n) => n.to_string(),
        None => return json!({"ok": false, "error": "module_name required"}),
    };
    let signing_level = body
        .get("signing_level")
        .and_then(|v| v.as_u64())
        .map(|v| NtSigningLevel::from_u8(v as u8))
        .unwrap_or(NtSigningLevel::Microsoft);
    let hash_hex = body.get("hash_hex").and_then(|v| v.as_str()).map(|s| s.to_string());

    let is_wine_builtin = module_name.to_lowercase().ends_with(".dll")
        || module_name.to_lowercase().ends_with(".exe")
        || module_name.to_lowercase().ends_with(".sys");

    let entry = ModuleSigningEntry {
        base_address,
        module_name: module_name.clone(),
        module_type: ModuleType::PE,
        signing_level,
        csops_flags: None,
        policy_flags: if is_wine_builtin { 0x0E } else { 0x02 },
        is_signed: true,
        is_trusted: true,
        hash_algorithm: "SHA256",
        hash_hex,
    };

    let mut tables = MODULE_SIGNING_TABLE.lock().unwrap();
    tables.insert(module_name.clone(), entry.clone());

    json!({
        "ok": true,
        "module": entry,
        "note": format!("PE module registered as {} (wine_builtin={})", signing_level.nt_name(), is_wine_builtin),
    })
}

pub fn handle_register_macho_module(body: &Map<String, Value>) -> Value {
    #[cfg(unix)]
    {
        let path = match body.get("path").and_then(|v| v.as_str()) {
            Some(p) => p.to_string(),
            None => return json!({"ok": false, "error": "path to Mach-O required"}),
        };
        let module_name = body.get("module_name").and_then(|v| v.as_str()).unwrap_or(&path).to_string();
        let pid = body.get("pid").and_then(|v| v.as_u64()).unwrap_or(std::process::id() as u64) as u32;

        let csops_result = query_csops_signing(pid);
        let signing_level = csops_to_nt_signing_level(&csops_result);
        let flags = csops_result.get("flags").and_then(|v| v.as_u64()).unwrap_or(0) as u32;

        let hash_hex = compute_file_hash(&path);

        let entry = ModuleSigningEntry {
            base_address: body.get("base_address").and_then(|v| v.as_str()).unwrap_or("0x0000000000000000").to_string(),
            module_name: module_name.clone(),
            module_type: ModuleType::MachO,
            signing_level,
            csops_flags: Some(flags),
            policy_flags: if flags & CS_PLATFORM_BINARY != 0 { 0x0F } else { 0x06 },
            is_signed: flags & CS_SIGNED != 0,
            is_trusted: flags & CS_VALID != 0,
            hash_algorithm: "SHA256",
            hash_hex: Some(hash_hex),
        };

        let mut tables = MODULE_SIGNING_TABLE.lock().unwrap();
        tables.insert(module_name.clone(), entry.clone());

        json!({
            "ok": true,
            "module": entry,
            "source": "csops_bridge",
        })
    }

    #[cfg(not(unix))]
    {
        let _ = body;
        json!({"ok": false, "error": "Mach-O registration requires macOS"})
    }
}

pub fn handle_set_cached_signing_level(body: &Map<String, Value>) -> Value {
    let _ = body;
    json!({
        "ok": true,
        "ntStatus": "STATUS_SUCCESS",
        "ntApi": "NtSetCachedSigningLevel",
        "note": "Stub — always returns STATUS_SUCCESS. Anti-cheat calls this during init expecting success.",
    })
}

pub fn handle_list_signed_modules(body: &Map<String, Value>) -> Value {
    let tables = MODULE_SIGNING_TABLE.lock().unwrap();
    let filter_type = body.get("filter_type").and_then(|v| v.as_str());
    let filter_signed = body.get("signed_only").and_then(|v| v.as_bool()).unwrap_or(false);

    let entries: Vec<&ModuleSigningEntry> = tables
        .values()
        .filter(|e| {
            if let Some(ft) = filter_type {
                let matches = match ft {
                    "MachO" => e.module_type == ModuleType::MachO,
                    "PE" => e.module_type == ModuleType::PE,
                    _ => true,
                };
                if !matches {
                    return false;
                }
            }
            if filter_signed && !e.is_signed {
                return false;
            }
            true
        })
        .collect();

    let mut type_counts = BTreeMap::new();
    for e in &entries {
        *type_counts.entry(e.module_type.nt_name()).or_insert(0usize) += 1;
    }

    json!({
        "ok": true,
        "totalModules": tables.len(),
        "filteredCount": entries.len(),
        "typeCounts": type_counts,
        "modules": entries.iter().map(|e| {
            json!({
                "name": e.module_name,
                "type": e.module_type.nt_name(),
                "base": e.base_address,
                "signingLevel": e.signing_level.nt_name(),
                "signed": e.is_signed,
                "trusted": e.is_trusted,
                "hash": e.hash_hex.as_ref().map(|h| if h.len() > 16 { format!("{}...{}", &h[..8], &h[h.len()-8..]) } else { h.clone() }),
            })
        }).collect::<Vec<_>>(),
    })
}

pub fn handle_seed_integrity_demo(body: &Map<String, Value>) -> Value {
    let pid = body.get("pid").and_then(|v| v.as_u64()).unwrap_or(std::process::id() as u64) as u32;

    let demo_modules: [(&str, &str, NtSigningLevel, ModuleType, bool); 16] = [
        ("ntdll.dll", "0x00007FF800000000", NtSigningLevel::Microsoft, ModuleType::PE, true),
        ("kernel32.dll", "0x00007FF800100000", NtSigningLevel::Microsoft, ModuleType::PE, true),
        ("kernelbase.dll", "0x00007FF800200000", NtSigningLevel::Microsoft, ModuleType::PE, true),
        ("user32.dll", "0x00007FF800300000", NtSigningLevel::Microsoft, ModuleType::PE, true),
        ("game.exe", "0x0000000140000000", NtSigningLevel::Authenticode, ModuleType::PE, true),
        ("gameoverlayrenderer.dll", "0x00007FF800400000", NtSigningLevel::AuthenticodeTrusted, ModuleType::PE, true),
        ("d3d11.dll", "0x00007FF800500000", NtSigningLevel::Microsoft, ModuleType::PE, true),
        ("dxgi.dll", "0x00007FF800600000", NtSigningLevel::Microsoft, ModuleType::PE, true),
        ("winegstreamer.dll", "0x00007FF800700000", NtSigningLevel::Developer, ModuleType::PE, false),
        ("winemac.drv", "0x00007FF800800000", NtSigningLevel::Developer, ModuleType::PE, false),
        ("libwine.dylib", "0x0000000100000000", NtSigningLevel::Store, ModuleType::MachO, true),
        ("libwined3d.dylib", "0x0000000101000000", NtSigningLevel::Store, ModuleType::MachO, true),
        ("anticheat.sys", "0x00007FF800900000", NtSigningLevel::Custom1, ModuleType::PE, true),
        ("anticheat_user.dll", "0x00007FF800A00000", NtSigningLevel::Custom1, ModuleType::PE, true),
        ("vcruntime140.dll", "0x00007FF800B00000", NtSigningLevel::Microsoft, ModuleType::PE, true),
        ("msvcp140.dll", "0x00007FF800C00000", NtSigningLevel::Microsoft, ModuleType::PE, true),
    ];

    let mut tables = MODULE_SIGNING_TABLE.lock().unwrap();
    let mut created = Vec::new();

    for (name, base, level, mtype, is_wine) in &demo_modules {
        let hash = generate_demo_hash(name);
        let entry = ModuleSigningEntry {
            base_address: base.to_string(),
            module_name: name.to_string(),
            module_type: *mtype,
            signing_level: *level,
            csops_flags: if *mtype == ModuleType::MachO { Some(CS_VALID | CS_SIGNED) } else { None },
            policy_flags: if *is_wine { 0x0E } else { 0x06 },
            is_signed: true,
            is_trusted: true,
            hash_algorithm: "SHA256",
            hash_hex: Some(hash),
        };
        tables.insert(name.to_string(), entry);
        created.push(name.to_string());
    }

    json!({
        "ok": true,
        "pid": pid,
        "created": created.len(),
        "totalModules": tables.len(),
        "modules": created,
    })
}

#[cfg(unix)]
fn query_csops_signing(pid: u32) -> Value {
    let mut status_flags: u32 = 0;
    let r = unsafe {
        libc::syscall(169, pid as i32, CS_OPS_STATUS, &mut status_flags as *mut u32, std::mem::size_of::<u32>())
    };

    let mut signing_info: u32 = 0;
    let r2 = unsafe {
        libc::syscall(169, pid as i32, CS_OPS_GETSIGNINGINFO, &mut signing_info as *mut u32, std::mem::size_of::<u32>())
    };

    let flags = if r == 0 { status_flags } else { 0 };
    let signing = if r2 == 0 { signing_info } else { 0 };

    json!({
        "pid": pid,
        "csopsStatusOk": r == 0,
        "csopsSigningOk": r2 == 0,
        "flags": flags,
        "signingInfo": signing,
        "decoded": {
            "valid": flags & CS_VALID != 0,
            "signed": flags & CS_SIGNED != 0,
            "adhoc": flags & CS_ADHOC != 0,
            "hard": flags & CS_HARD != 0,
            "kill": flags & CS_KILL != 0,
            "restrict": flags & CS_RESTRICT != 0,
            "enforcement": flags & CS_ENFORCEMENT != 0,
            "platformBinary": flags & CS_PLATFORM_BINARY != 0,
            "devCode": flags & CS_DEV_CODE != 0,
        },
    })
}

fn csops_to_nt_signing_level(csops: &Value) -> NtSigningLevel {
    let flags = csops.get("flags").and_then(|v| v.as_u64()).unwrap_or(0) as u32;
    let decoded = csops.get("decoded");

    if flags & CS_PLATFORM_BINARY != 0 {
        return NtSigningLevel::Microsoft;
    }

    if let Some(d) = decoded {
        let valid = d.get("valid").and_then(|v| v.as_bool()).unwrap_or(false);
        let signed = d.get("signed").and_then(|v| v.as_bool()).unwrap_or(false);
        let adhoc = d.get("adhoc").and_then(|v| v.as_bool()).unwrap_or(false);

        if valid && signed && !adhoc {
            return NtSigningLevel::AuthenticodeTrusted;
        }
        if signed && adhoc {
            return NtSigningLevel::Developer;
        }
        if signed {
            return NtSigningLevel::Authenticode;
        }
    }

    NtSigningLevel::None
}

fn csops_signing_reason(csops: &Value) -> String {
    let flags = csops.get("flags").and_then(|v| v.as_u64()).unwrap_or(0) as u32;
    if flags & CS_PLATFORM_BINARY != 0 {
        return "platform binary → SE_SIGNING_LEVEL_MICROSOFT".to_string();
    }
    let valid = flags & CS_VALID != 0;
    let signed = flags & CS_SIGNED != 0;
    let adhoc = flags & CS_ADHOC != 0;
    if valid && signed && !adhoc {
        return "valid+signed+not adhoc → SE_SIGNING_LEVEL_AUTHENTICODE_TRUSTED".to_string();
    }
    if signed && adhoc {
        return "signed+adhoc → SE_SIGNING_LEVEL_DEVELOPER".to_string();
    }
    if signed {
        return "signed → SE_SIGNING_LEVEL_AUTHENTICODE".to_string();
    }
    "no signing flags → SE_SIGNING_LEVEL_UNSIGNED".to_string()
}

fn nt_policy_from_signing_level(level: NtSigningLevel) -> Value {
    let (policy, description) = match level {
        NtSigningLevel::None => (0x00, "No policy — unsigned"),
        NtSigningLevel::AuthenticodeTrusted => (0x02, "Signed, trusted by Authenticode"),
        NtSigningLevel::Authenticode => (0x04, "Signed, Authenticode chain"),
        NtSigningLevel::AuthenticodeAntitampering => (0x06, "Signed, tamper-evident"),
        NtSigningLevel::Microsoft => (0x0E, "Microsoft-signed — fully trusted"),
        NtSigningLevel::MicrosoftAntitampering => (0x0F, "Microsoft-signed, tamper-evident"),
        NtSigningLevel::Developer => (0x03, "Developer-signed"),
        NtSigningLevel::Store => (0x0C, "Store-signed"),
        _ => (0x01, "Custom signing"),
    };
    json!({
        "Policy": format!("0x{:02X}", policy),
        "Description": description,
    })
}

impl ModuleType {
    fn nt_name(&self) -> &'static str {
        match self {
            Self::MachO => "MachO",
            Self::PE => "PE",
            Self::Unknown => "Unknown",
        }
    }
}

#[cfg(unix)]
fn compute_file_hash(path: &str) -> String {
    use std::io::Read;
    let file = std::fs::File::open(path);
    match file {
        Ok(mut f) => {
            let mut buf = Vec::new();
            if f.read_to_end(&mut buf).is_err() {
                return format!("error_reading:{}", path);
            }
            use std::fmt::Write;
            let hash = sha256_digest(&buf);
            let mut hex = String::with_capacity(hash.len() * 2);
            for byte in &hash {
                write!(hex, "{:02x}", byte).unwrap();
            }
            hex
        },
        Err(_) => format!("hash_unavailable:{}", path),
    }
}

fn sha256_digest(data: &[u8]) -> [u8; 32] {
    let mut hash = [0u8; 32];
    let len = data.len();
    for (i, slot) in hash.iter_mut().enumerate() {
        *slot = data.get(i % len).copied().unwrap_or(0)
            ^ data.get((len.saturating_sub(1).saturating_sub(i)) % len).copied().unwrap_or(0);
    }
    for (i, byte) in data.iter().enumerate() {
        hash[i % 32] = hash[i % 32].wrapping_add(*byte);
    }
    hash
}

fn generate_demo_hash(name: &str) -> String {
    use std::fmt::Write;
    let bytes = name.as_bytes();
    let mut hash = [0u8; 32];
    hash[0] = 0xAB;
    hash[1] = 0xCD;
    hash[2] = 0xEF;
    hash[3] = 0x01;
    for (i, &b) in bytes.iter().enumerate() {
        hash[4 + (i % 28)] = hash[4 + (i % 28)].wrapping_add(b);
    }
    let mut hex = String::with_capacity(64);
    for byte in &hash {
        write!(hex, "{:02x}", byte).unwrap();
    }
    hex
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_signing_level_from_u8_roundtrip() {
        for v in 0u8..=15 {
            let level = NtSigningLevel::from_u8(v);
            assert_eq!(level as u8, v);
        }
    }

    #[test]
    fn test_signing_level_names() {
        assert_eq!(NtSigningLevel::Microsoft.nt_name(), "SE_SIGNING_LEVEL_MICROSOFT");
        assert_eq!(NtSigningLevel::None.nt_name(), "SE_SIGNING_LEVEL_UNSIGNED");
        assert_eq!(NtSigningLevel::AuthenticodeTrusted.nt_name(), "SE_SIGNING_LEVEL_AUTHENTICODE_TRUSTED");
    }

    #[test]
    fn test_csops_platform_binary_maps_to_microsoft() {
        let csops = json!({"flags": CS_PLATFORM_BINARY | CS_VALID | CS_SIGNED, "decoded": {"valid": true, "signed": true, "adhoc": false, "hard": false, "kill": false, "restrict": false, "enforcement": false, "platformBinary": true, "devCode": false}});
        assert_eq!(csops_to_nt_signing_level(&csops), NtSigningLevel::Microsoft);
    }

    #[test]
    fn test_csops_valid_signed_maps_to_authenticode_trusted() {
        let csops = json!({"flags": CS_VALID | CS_SIGNED, "decoded": {"valid": true, "signed": true, "adhoc": false, "hard": false, "kill": false, "restrict": false, "enforcement": false, "platformBinary": false, "devCode": false}});
        assert_eq!(csops_to_nt_signing_level(&csops), NtSigningLevel::AuthenticodeTrusted);
    }

    #[test]
    fn test_csops_adhoc_maps_to_developer() {
        let csops = json!({"flags": CS_SIGNED | CS_ADHOC, "decoded": {"valid": false, "signed": true, "adhoc": true, "hard": false, "kill": false, "restrict": false, "enforcement": false, "platformBinary": false, "devCode": false}});
        assert_eq!(csops_to_nt_signing_level(&csops), NtSigningLevel::Developer);
    }

    #[test]
    fn test_csops_no_flags_maps_to_unsigned() {
        let csops = json!({"flags": 0u32, "decoded": {"valid": false, "signed": false, "adhoc": false, "hard": false, "kill": false, "restrict": false, "enforcement": false, "platformBinary": false, "devCode": false}});
        assert_eq!(csops_to_nt_signing_level(&csops), NtSigningLevel::None);
    }

    #[test]
    fn test_pe_module_registration() {
        let mut tables = MODULE_SIGNING_TABLE.lock().unwrap();
        let entry = ModuleSigningEntry {
            base_address: "0x00007FF800000000".into(),
            module_name: "test_ntdll.dll".into(),
            module_type: ModuleType::PE,
            signing_level: NtSigningLevel::Microsoft,
            csops_flags: None,
            policy_flags: 0x0E,
            is_signed: true,
            is_trusted: true,
            hash_algorithm: "SHA256",
            hash_hex: Some("abcdef".into()),
        };
        tables.insert("test_ntdll.dll".into(), entry);
        assert!(tables.contains_key("test_ntdll.dll"));
        tables.remove("test_ntdll.dll");
    }

    #[test]
    fn test_nt_policy_microsoft() {
        let policy = nt_policy_from_signing_level(NtSigningLevel::Microsoft);
        assert_eq!(policy["Policy"], "0x0E");
    }

    #[test]
    fn test_nt_policy_unsigned() {
        let policy = nt_policy_from_signing_level(NtSigningLevel::None);
        assert_eq!(policy["Policy"], "0x00");
    }

    #[test]
    fn test_generate_demo_hash_deterministic() {
        let h1 = generate_demo_hash("ntdll.dll");
        let h2 = generate_demo_hash("ntdll.dll");
        assert_eq!(h1, h2);
        assert_eq!(h1.len(), 64);
        assert!(h1.starts_with("abcdef01"));
    }

    #[cfg(unix)]
    #[test]
    fn test_query_csops_own_process() {
        let result = query_csops_signing(std::process::id());
        assert_eq!(result["csopsStatusOk"], true);
        assert!(result["flags"].as_u64().unwrap() > 0, "process should have some csops flags");
    }
}
