use super::engine::PipelineId;
use super::pe::{D3dApi, PeInfo};
use std::path::PathBuf;
use std::sync::OnceLock;

static RULES: OnceLock<std::collections::HashMap<u32, PipelineId>> = OnceLock::new();

fn load_rules() -> &'static std::collections::HashMap<u32, PipelineId> {
    RULES.get_or_init(|| {
        let home = dirs::home_dir().unwrap_or_default();

        let candidates = vec![
            home.join("metalsharp").join("configs").join("mtsp-rules.toml"),
            home.join(".metalsharp").join("configs").join("mtsp-rules.toml"),
            PathBuf::from("configs/mtsp-rules.toml"),
        ];

        for path in &candidates {
            if path.exists() {
                if let Ok(contents) = std::fs::read_to_string(path) {
                    return parse_rules(&contents);
                }
            }
        }

        std::collections::HashMap::new()
    })
}

fn parse_rules(toml_str: &str) -> std::collections::HashMap<u32, PipelineId> {
    let mut map = std::collections::HashMap::new();

    let doc: toml::Value = match toml_str.parse() {
        Ok(v) => v,
        Err(_) => return map,
    };

    let overrides = match doc.get("overrides").and_then(|v| v.as_table()) {
        Some(t) => t,
        None => return map,
    };

    for (appid_str, entry) in overrides {
        if let Ok(appid) = appid_str.parse::<u32>() {
            if let Some(pipeline_str) = entry.get("pipeline").and_then(|v| v.as_str()) {
                if let Some(pipeline) = parse_pipeline_id(pipeline_str) {
                    map.insert(appid, pipeline);
                }
            }
        }
    }

    map
}

fn parse_pipeline_id(s: &str) -> Option<PipelineId> {
    match s {
        "m11" => Some(PipelineId::M11),
        "m12" => Some(PipelineId::M12),
        "m9" => Some(PipelineId::M9),
        "m9_gl" => Some(PipelineId::M9Gl),
        "m32_vk" => Some(PipelineId::M32Vk),
        "m32_w" => Some(PipelineId::M32W),
        "m64" => Some(PipelineId::M64),
        "steam" => Some(PipelineId::Steam),
        "steam_metalfx" => Some(PipelineId::SteamMetalfx),
        "steam_d3dmetal_perf" => Some(PipelineId::SteamD3DMetalPerf),
        "fna_arm64" => Some(PipelineId::FnaArm64),
        "fna_x86" => Some(PipelineId::FnaX86),
        "mono_generic" => Some(PipelineId::MonoGeneric),
        "wine_bare" => Some(PipelineId::WineBare),
        _ => PipelineId::from_legacy_method(s),
    }
}

pub fn resolve_pipeline(appid: u32) -> PipelineId {
    let rules = load_rules();

    if let Some(&pipeline) = rules.get(&appid) {
        return pipeline;
    }

    let game_dir = crate::setup::resolve_game_dir(appid);
    if let Some(ref dir) = game_dir {
        if dir.exists() {
            if crate::setup::detect_dotnet_game(dir) {
                return PipelineId::FnaArm64;
            }

            if let Some(detected) = detect_from_directory(dir) {
                return detected;
            }
        }
    }

    if let Some(ref dir) = game_dir {
        if dir.exists() {
            if let Some(pe_info) = super::pe::analyze_game_exe(dir) {
                if let Some(pipeline) = pe_info_to_pipeline(&pe_info) {
                    return pipeline;
                }
            }
        }
    }

    PipelineId::SteamD3DMetalPerf
}

fn detect_from_directory(dir: &PathBuf) -> Option<PipelineId> {
    let has_file_ci = |name: &str| -> bool {
        let name_lower = name.to_lowercase();
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                if entry.file_name().to_string_lossy().to_lowercase() == name_lower {
                    return true;
                }
            }
        }
        false
    };
    let has_dir_ci = |name: &str| -> bool {
        let name_lower = name.to_lowercase();
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                if entry.path().is_dir() && entry.file_name().to_string_lossy().to_lowercase() == name_lower {
                    return true;
                }
            }
        }
        false
    };
    let has_glob = |pattern: &str| -> bool {
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                let name = entry.file_name().to_string_lossy().to_lowercase();
                if name.ends_with(&pattern.to_lowercase()) {
                    return true;
                }
            }
        }
        false
    };

    if has_file_ci("unityplayer.dll") || has_file_ci("gameassembly.dll") {
        return Some(PipelineId::SteamD3DMetalPerf);
    }

    if has_dir_ci("engine") && has_dir_ci("binaries") {
        return Some(PipelineId::SteamMetalfx);
    }

    if has_glob(".pak") {
        return Some(PipelineId::SteamD3DMetalPerf);
    }

    if has_dir_ci("engine") && has_dir_ci("content") {
        return Some(PipelineId::SteamMetalfx);
    }

    if has_glob(".bdt") || has_glob(".bhd") {
        return Some(PipelineId::SteamMetalfx);
    }

    if has_glob("re_chunk_") || has_file_ci("re2_config.ini") || has_file_ci("re8_config.ini") {
        return Some(PipelineId::SteamD3DMetalPerf);
    }

    if has_file_ci("d3dx9_43.dll") {
        return Some(PipelineId::WineBare);
    }

    if has_file_ci("steam_api64.dll") || has_file_ci("steam_api.dll") {
        return Some(PipelineId::SteamD3DMetalPerf);
    }

    None
}

fn pe_info_to_pipeline(pe: &PeInfo) -> Option<PipelineId> {
    match pe.detected_api {
        D3dApi::D3D12 => {
            if pe.is_64_bit {
                Some(PipelineId::M12)
            } else {
                Some(PipelineId::SteamD3DMetalPerf)
            }
        },
        D3dApi::D3D11 => {
            if pe.is_64_bit {
                Some(PipelineId::M11)
            } else {
                Some(PipelineId::SteamD3DMetalPerf)
            }
        },
        D3dApi::D3D9 => {
            if pe.is_64_bit {
                Some(PipelineId::M9)
            } else {
                Some(PipelineId::M32Vk)
            }
        },
        D3dApi::D3D10 => Some(PipelineId::M11),
        D3dApi::Unknown => None,
    }
}
