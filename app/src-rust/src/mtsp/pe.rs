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

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PeExport {
    pub name: Option<String>,
    pub ordinal: u16,
}

const MACHINE_I386: u16 = 0x014c;
const MACHINE_AMD64: u16 = 0x8664;

pub fn parse_pe_imports(data: &[u8]) -> Option<PeInfo> {
    let headers = parse_headers(data)?;
    let machine = headers.machine;
    let is_64_bit = machine == MACHINE_AMD64;

    let (import_dir_rva, import_dir_size) = if headers.optional_magic == 0x20b {
        if headers.optional_offset + 112 + 8 > data.len() {
            return Some(PeInfo { machine_type: machine, is_64_bit, imports: vec![], detected_api: D3dApi::Unknown });
        }
        let rva =
            u32::from_le_bytes(data[headers.optional_offset + 112..headers.optional_offset + 116].try_into().ok()?);
        let size =
            u32::from_le_bytes(data[headers.optional_offset + 116..headers.optional_offset + 120].try_into().ok()?);
        (rva, size)
    } else if headers.optional_magic == 0x10b {
        if headers.optional_offset + 96 + 8 > data.len() {
            return Some(PeInfo { machine_type: machine, is_64_bit, imports: vec![], detected_api: D3dApi::Unknown });
        }
        let rva =
            u32::from_le_bytes(data[headers.optional_offset + 96..headers.optional_offset + 100].try_into().ok()?);
        let size =
            u32::from_le_bytes(data[headers.optional_offset + 100..headers.optional_offset + 104].try_into().ok()?);
        (rva, size)
    } else {
        return Some(PeInfo { machine_type: machine, is_64_bit, imports: vec![], detected_api: D3dApi::Unknown });
    };

    if import_dir_rva == 0 || import_dir_size == 0 {
        return Some(PeInfo { machine_type: machine, is_64_bit, imports: vec![], detected_api: D3dApi::Unknown });
    }

    let mut imports = Vec::new();
    for section in &headers.sections {
        if section.contains(import_dir_rva) {
            let dir_file_offset = section.offset_for(import_dir_rva);
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
                if let Some(name_off) = headers.rva_to_offset(name_rva) {
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

    Some(PeInfo { machine_type: machine, is_64_bit, imports, detected_api })
}

pub fn parse_pe_exports(data: &[u8]) -> Option<Vec<PeExport>> {
    let headers = parse_headers(data)?;
    let (export_dir_rva, export_dir_size) = if headers.optional_magic == 0x20b {
        if headers.optional_offset + 112 + 8 > data.len() {
            return Some(Vec::new());
        }
        let rva =
            u32::from_le_bytes(data[headers.optional_offset + 112..headers.optional_offset + 116].try_into().ok()?);
        let size =
            u32::from_le_bytes(data[headers.optional_offset + 116..headers.optional_offset + 120].try_into().ok()?);
        (rva, size)
    } else if headers.optional_magic == 0x10b {
        if headers.optional_offset + 96 + 8 > data.len() {
            return Some(Vec::new());
        }
        let rva =
            u32::from_le_bytes(data[headers.optional_offset + 96..headers.optional_offset + 100].try_into().ok()?);
        let size =
            u32::from_le_bytes(data[headers.optional_offset + 100..headers.optional_offset + 104].try_into().ok()?);
        (rva, size)
    } else {
        return Some(Vec::new());
    };
    if export_dir_rva == 0 || export_dir_size == 0 {
        return Some(Vec::new());
    }

    let export_off = headers.rva_to_offset(export_dir_rva)?;
    if export_off + 40 > data.len() {
        return Some(Vec::new());
    }

    let ordinal_base = u32::from_le_bytes(data[export_off + 16..export_off + 20].try_into().ok()?);
    let function_count = u32::from_le_bytes(data[export_off + 20..export_off + 24].try_into().ok()?);
    let name_count = u32::from_le_bytes(data[export_off + 24..export_off + 28].try_into().ok()?);
    let functions_rva = u32::from_le_bytes(data[export_off + 28..export_off + 32].try_into().ok()?);
    let names_rva = u32::from_le_bytes(data[export_off + 32..export_off + 36].try_into().ok()?);
    let ordinals_rva = u32::from_le_bytes(data[export_off + 36..export_off + 40].try_into().ok()?);

    let function_table_off = headers.rva_to_offset(functions_rva)?;
    if function_table_off + function_count as usize * 4 > data.len() {
        return Some(Vec::new());
    }

    let mut exports: Vec<PeExport> = (0..function_count)
        .filter_map(|idx| {
            let ordinal = ordinal_base.checked_add(idx)?;
            Some(PeExport { name: None, ordinal: u16::try_from(ordinal).ok()? })
        })
        .collect();

    let Some(name_table_off) = headers.rva_to_offset(names_rva) else {
        return Some(exports);
    };
    let Some(ordinal_table_off) = headers.rva_to_offset(ordinals_rva) else {
        return Some(exports);
    };
    for idx in 0..name_count as usize {
        let name_rva_off = name_table_off + idx * 4;
        let ordinal_index_off = ordinal_table_off + idx * 2;
        if name_rva_off + 4 > data.len() || ordinal_index_off + 2 > data.len() {
            break;
        }
        let name_rva = u32::from_le_bytes(data[name_rva_off..name_rva_off + 4].try_into().ok()?);
        let ordinal_index =
            u16::from_le_bytes(data[ordinal_index_off..ordinal_index_off + 2].try_into().ok()?) as usize;
        let Some(name_off) = headers.rva_to_offset(name_rva) else {
            continue;
        };
        if ordinal_index >= exports.len() {
            continue;
        }
        exports[ordinal_index].name = read_null_terminated(data, name_off);
    }

    Some(exports)
}

pub fn pe_exports_ordinal(data: &[u8], ordinal: u16, expected_name: &str) -> Option<bool> {
    let exports = parse_pe_exports(data)?;
    Some(exports.iter().any(|export| {
        export.ordinal == ordinal
            && export.name.as_deref().map(|name| name.eq_ignore_ascii_case(expected_name)).unwrap_or(false)
    }))
}

#[derive(Debug)]
struct PeHeaders {
    machine: u16,
    optional_magic: u16,
    optional_offset: usize,
    sections: Vec<PeSection>,
}

#[derive(Debug)]
struct PeSection {
    virtual_address: u32,
    virtual_size: u32,
    raw_offset: usize,
    raw_size: u32,
}

impl PeSection {
    fn contains(&self, rva: u32) -> bool {
        let span = self.virtual_size.max(self.raw_size).max(1);
        rva >= self.virtual_address && rva < self.virtual_address.saturating_add(span)
    }

    fn offset_for(&self, rva: u32) -> usize {
        self.raw_offset + (rva - self.virtual_address) as usize
    }
}

impl PeHeaders {
    fn rva_to_offset(&self, rva: u32) -> Option<usize> {
        self.sections.iter().find(|section| section.contains(rva)).map(|section| section.offset_for(rva))
    }
}

fn parse_headers(data: &[u8]) -> Option<PeHeaders> {
    if data.len() < 64 || data[0] != b'M' || data[1] != b'Z' {
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
    let num_sections = u16::from_le_bytes(data[pe_offset + 6..pe_offset + 8].try_into().ok()?) as usize;
    let optional_hdr_size = u16::from_le_bytes(data[pe_offset + 20..pe_offset + 22].try_into().ok()?) as usize;
    let optional_offset = pe_offset + 24;
    if optional_offset + optional_hdr_size > data.len() {
        return None;
    }
    let optional_magic = u16::from_le_bytes(data[optional_offset..optional_offset + 2].try_into().ok()?);
    let section_offset = optional_offset + optional_hdr_size;

    let mut sections = Vec::new();
    for i in 0..num_sections {
        let sec_off = section_offset + i * 40;
        if sec_off + 40 > data.len() {
            break;
        }
        sections.push(PeSection {
            virtual_size: u32::from_le_bytes(data[sec_off + 8..sec_off + 12].try_into().ok()?),
            virtual_address: u32::from_le_bytes(data[sec_off + 12..sec_off + 16].try_into().ok()?),
            raw_size: u32::from_le_bytes(data[sec_off + 16..sec_off + 20].try_into().ok()?),
            raw_offset: u32::from_le_bytes(data[sec_off + 20..sec_off + 24].try_into().ok()?) as usize,
        });
    }

    Some(PeHeaders { machine, optional_magic, optional_offset, sections })
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
    if lower.iter().any(|d| d == "d3d10.dll" || d == "d3d10_1.dll" || d == "d3d10core.dll") {
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
                if name.contains("setup")
                    || name.contains("redist")
                    || name.contains("uninstall")
                    || name.contains("vcredist")
                    || name.contains("installer")
                    || name.contains("crashhandler")
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detects_d3d10_from_public_and_core_imports() {
        assert_eq!(detect_d3d_api(&["d3d10.dll".into()]), D3dApi::D3D10);
        assert_eq!(detect_d3d_api(&["d3d10_1.dll".into()]), D3dApi::D3D10);
        assert_eq!(detect_d3d_api(&["d3d10core.dll".into()]), D3dApi::D3D10);
    }

    #[test]
    fn d3d12_import_takes_priority_over_d3d10_compat_imports() {
        assert_eq!(detect_d3d_api(&["d3d10core.dll".into(), "d3d12.dll".into()]), D3dApi::D3D12);
    }

    #[test]
    fn detects_named_export_at_expected_ordinal() {
        let mut data = vec![0u8; 0x500];
        data[0] = b'M';
        data[1] = b'Z';
        data[0x3c..0x40].copy_from_slice(&(0x80u32).to_le_bytes());
        data[0x80..0x84].copy_from_slice(b"PE\0\0");
        data[0x84..0x86].copy_from_slice(&MACHINE_AMD64.to_le_bytes());
        data[0x86..0x88].copy_from_slice(&(1u16).to_le_bytes());
        data[0x94..0x96].copy_from_slice(&(0xf0u16).to_le_bytes());
        data[0x98..0x9a].copy_from_slice(&(0x20bu16).to_le_bytes());
        data[0x98 + 112..0x98 + 116].copy_from_slice(&(0x1000u32).to_le_bytes());
        data[0x98 + 116..0x98 + 120].copy_from_slice(&(40u32).to_le_bytes());

        let sec = 0x80 + 24 + 0xf0;
        data[sec..sec + 8].copy_from_slice(b".edata\0\0");
        data[sec + 8..sec + 12].copy_from_slice(&(0x300u32).to_le_bytes());
        data[sec + 12..sec + 16].copy_from_slice(&(0x1000u32).to_le_bytes());
        data[sec + 16..sec + 20].copy_from_slice(&(0x300u32).to_le_bytes());
        data[sec + 20..sec + 24].copy_from_slice(&(0x200u32).to_le_bytes());

        let export = 0x200;
        data[export + 16..export + 20].copy_from_slice(&(101u32).to_le_bytes());
        data[export + 20..export + 24].copy_from_slice(&(2u32).to_le_bytes());
        data[export + 24..export + 28].copy_from_slice(&(1u32).to_le_bytes());
        data[export + 28..export + 32].copy_from_slice(&(0x1040u32).to_le_bytes());
        data[export + 32..export + 36].copy_from_slice(&(0x1050u32).to_le_bytes());
        data[export + 36..export + 40].copy_from_slice(&(0x1060u32).to_le_bytes());
        data[0x240..0x248].copy_from_slice(&[1, 0, 0, 0, 2, 0, 0, 0]);
        data[0x250..0x254].copy_from_slice(&(0x1070u32).to_le_bytes());
        data[0x260..0x262].copy_from_slice(&(0u16).to_le_bytes());
        data[0x270..0x282].copy_from_slice(b"D3D12CreateDevice\0");

        assert_eq!(pe_exports_ordinal(&data, 101, "D3D12CreateDevice"), Some(true));
        assert_eq!(pe_exports_ordinal(&data, 102, "D3D12CreateDevice"), Some(false));
    }
}
