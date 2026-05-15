use std::path::Path;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum D3dApi {
    D3D9,
    D3D10,
    D3D11,
    D3D12,
    Unknown,
}

#[derive(Debug)]
pub struct PeInfo {
    pub machine_type: u16,
    pub is_64_bit: bool,
    pub imports: Vec<String>,
    pub detected_api: D3dApi,
}

const MACHINE_I386: u16 = 0x014c;
const MACHINE_AMD64: u16 = 0x8664;

pub fn parse_pe_imports(data: &[u8]) -> Option<PeInfo> {
    if data.len() < 64 {
        return None;
    }
    if data[0] != b'M' || data[1] != b'Z' {
        return None;
    }

    let pe_offset = u32::from_le_bytes(data[0x3c..0x40].try_into().ok()?) as usize;
    if pe_offset + 24 > data.len() {
        return None;
    }
    if data[pe_offset] != b'P' || data[pe_offset + 1] != b'E' || data[pe_offset + 2] != 0 || data[pe_offset + 3] != 0 {
        return None;
    }

    let machine = u16::from_le_bytes(data[pe_offset + 4..pe_offset + 6].try_into().ok()?);
    let is_64_bit = machine == MACHINE_AMD64;
    let optional_hdr_size = u16::from_le_bytes(data[pe_offset + 20..pe_offset + 22].try_into().ok()?) as usize;
    let optional_magic = u16::from_le_bytes(data[pe_offset + 24..pe_offset + 26].try_into().ok()?);

    let (import_dir_rva, import_dir_size) = if optional_magic == 0x20b {
        if pe_offset + 24 + 112 + 8 > data.len() {
            return Some(PeInfo { machine_type: machine, is_64_bit, imports: vec![], detected_api: D3dApi::Unknown });
        }
        let rva = u32::from_le_bytes(data[pe_offset + 24 + 112..pe_offset + 24 + 116].try_into().ok()?);
        let size = u32::from_le_bytes(data[pe_offset + 24 + 116..pe_offset + 24 + 120].try_into().ok()?);
        (rva, size)
    } else if optional_magic == 0x10b {
        if pe_offset + 24 + 96 + 8 > data.len() {
            return Some(PeInfo { machine_type: machine, is_64_bit, imports: vec![], detected_api: D3dApi::Unknown });
        }
        let rva = u32::from_le_bytes(data[pe_offset + 24 + 96..pe_offset + 24 + 100].try_into().ok()?);
        let size = u32::from_le_bytes(data[pe_offset + 24 + 100..pe_offset + 24 + 104].try_into().ok()?);
        (rva, size)
    } else {
        return Some(PeInfo { machine_type: machine, is_64_bit, imports: vec![], detected_api: D3dApi::Unknown });
    };

    if import_dir_rva == 0 || import_dir_size == 0 {
        return Some(PeInfo { machine_type: machine, is_64_bit, imports: vec![], detected_api: D3dApi::Unknown });
    }

    let section_offset = pe_offset + 24 + optional_hdr_size;
    let num_sections = u16::from_le_bytes(data[pe_offset + 6..pe_offset + 8].try_into().ok()?) as usize;

    let mut imports = Vec::new();
    for i in 0..num_sections {
        let sec_off = section_offset + i * 40;
        if sec_off + 40 > data.len() {
            break;
        }
        let sec_vsize = u32::from_le_bytes(data[sec_off + 8..sec_off + 12].try_into().ok()?);
        let sec_rva = u32::from_le_bytes(data[sec_off + 12..sec_off + 16].try_into().ok()?);
        let sec_raw = u32::from_le_bytes(data[sec_off + 20..sec_off + 24].try_into().ok()?) as usize;
        let sec_end = sec_rva + sec_vsize;

        if import_dir_rva >= sec_rva && import_dir_rva < sec_end {
            let dir_file_offset = sec_raw + (import_dir_rva - sec_rva) as usize;

            let mut entry_off = dir_file_offset;
            loop {
                if entry_off + 20 > data.len() {
                    break;
                }
                let ilt_rva = u32::from_le_bytes(data[entry_off..entry_off + 4].try_into().ok()?);
                let name_rva = u32::from_le_bytes(data[entry_off + 12..entry_off + 16].try_into().ok()?);
                if ilt_rva == 0 && name_rva == 0 {
                    break;
                }
                if name_rva >= sec_rva && name_rva < sec_end {
                    let name_off = sec_raw + (name_rva - sec_rva) as usize;
                    if let Some(dll_name) = read_null_terminated(data, name_off) {
                        imports.push(dll_name);
                    }
                }
                entry_off += 20;
                if imports.len() > 256 {
                    break;
                }
            }
            break;
        }
    }

    let detected_api = detect_d3d_api(&imports);

    Some(PeInfo {
        machine_type: machine,
        is_64_bit,
        imports,
        detected_api,
    })
}

fn read_null_terminated(data: &[u8], offset: usize) -> Option<String> {
    let end = data.iter().skip(offset).position(|&b| b == 0)?;
    Some(String::from_utf8_lossy(&data[offset..offset + end]).to_string())
}

pub fn detect_d3d_api(imports: &[String]) -> D3dApi {
    let lower: Vec<String> = imports.iter().map(|s| s.to_lowercase()).collect();

    if lower.iter().any(|d| d == "d3d12.dll") {
        return D3dApi::D3D12;
    }
    if lower.iter().any(|d| d == "d3d11.dll") {
        return D3dApi::D3D11;
    }
    if lower.iter().any(|d| d == "d3d10.dll") {
        return D3dApi::D3D10;
    }
    if lower.iter().any(|d| d == "d3d9.dll") {
        return D3dApi::D3D9;
    }

    D3dApi::Unknown
}

pub fn analyze_game_exe(game_dir: &Path) -> Option<PeInfo> {
    if let Ok(entries) = std::fs::read_dir(game_dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().map(|e| e == "exe").unwrap_or(false) {
                let name = path.file_name()?.to_string_lossy().to_lowercase();
                if name.contains("setup") || name.contains("redist") || name.contains("uninstall")
                    || name.contains("vcredist") || name.contains("installer") || name.contains("crashhandler")
                {
                    continue;
                }
                if let Ok(data) = std::fs::read(&path) {
                    if let Some(info) = parse_pe_imports(&data) {
                        return Some(info);
                    }
                }
            }
        }
    }
    None
}
