use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::RwLock;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GameRule {
    pub appid: u32,
    pub name: String,
    pub engine: String,
    #[serde(default)]
    pub method: String,
    #[serde(default = "default_none")]
    pub setup: String,
    #[serde(default)]
    pub exe: Option<String>,
    #[serde(default = "default_steam")]
    pub prefix: String,
    #[serde(default)]
    pub status: Option<String>,
    #[serde(default)]
    pub notes: Option<String>,
    #[serde(default)]
    pub env: HashMap<String, String>,
}

fn default_none() -> String { "none".into() }
fn default_steam() -> String { "steam".into() }

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GlobalConfig {
    #[serde(default = "default_global_method")]
    pub default_method: String,
    #[serde(default = "default_steam")]
    pub default_prefix: String,
}

fn default_global_method() -> String { "steam_d3dmetal_perf".into() }

#[derive(Debug, Serialize, Deserialize, Default)]
pub struct RulesConfig {
    pub global: Option<GlobalConfig>,
    #[serde(default)]
    pub game: Vec<GameRule>,
}

pub struct Rules {
    rules: RwLock<RulesConfig>,
    appid_index: RwLock<HashMap<u32, usize>>,
}

impl Rules {
    pub fn load() -> Self {
        let config = Self::load_config();
        let index = Self::build_index(&config);
        Rules {
            rules: RwLock::new(config),
            appid_index: RwLock::new(index),
        }
    }

    pub fn reload(&self) -> bool {
        let config = Self::load_config();
        let index = Self::build_index(&config);
        if let Ok(mut rules) = self.rules.write() {
            *rules = config;
        }
        if let Ok(mut idx) = self.appid_index.write() {
            *idx = index;
        }
        true
    }

    pub fn find(&self, appid: u32) -> Option<GameRule> {
        let idx = self.appid_index.read().ok()?.get(&appid).copied()?;
        let rules = self.rules.read().ok()?;
        rules.game.get(idx).cloned()
    }

    pub fn find_method(&self, appid: u32) -> String {
        self.find(appid)
            .map(|r| {
                if r.method.is_empty() {
                    r.engine.clone()
                } else {
                    r.method.clone()
                }
            })
            .unwrap_or_else(|| self.default_method())
    }

    pub fn find_engine(&self, appid: u32) -> String {
        self.find(appid)
            .map(|r| r.engine.clone())
            .unwrap_or_else(|| "steam_d3dmetal_perf".into())
    }

    pub fn find_env(&self, appid: u32) -> HashMap<String, String> {
        self.find(appid)
            .map(|r| r.env)
            .unwrap_or_default()
    }

    pub fn find_prefix(&self, appid: u32) -> String {
        self.find(appid)
            .map(|r| r.prefix.clone())
            .unwrap_or_else(|| self.default_prefix())
    }

    pub fn find_setup(&self, appid: u32) -> String {
        self.find(appid)
            .map(|r| r.setup.clone())
            .unwrap_or_else(|| "none".into())
    }

    pub fn find_exe(&self, appid: u32) -> Option<String> {
        self.find(appid).and_then(|r| r.exe)
    }

    pub fn default_method(&self) -> String {
        if let Ok(r) = self.rules.read() {
            if let Some(ref g) = r.global {
                return g.default_method.clone();
            }
        }
        "steam_d3dmetal_perf".into()
    }

    pub fn default_prefix(&self) -> String {
        if let Ok(r) = self.rules.read() {
            if let Some(ref g) = r.global {
                return g.default_prefix.clone();
            }
        }
        "steam".into()
    }

    pub fn list_all(&self) -> Vec<GameRule> {
        self.rules
            .read()
            .map(|r| r.game.clone())
            .unwrap_or_default()
    }

    pub fn list_global(&self) -> GlobalConfig {
        self.rules
            .read()
            .ok()
            .and_then(|r| r.global.clone())
            .unwrap_or(GlobalConfig {
                default_method: "steam_d3dmetal_perf".into(),
                default_prefix: "steam".into(),
            })
    }

    fn load_config() -> RulesConfig {
        let user_path = dirs::home_dir()
            .map(|h| h.join(".metalsharp/configs/rules.toml"));

        let bundled_paths = find_bundled_rules();

        if let Some(ref path) = user_path {
            if path.exists() {
                if let Ok(content) = std::fs::read_to_string(path) {
                    match toml::from_str(&content) {
                        Ok(config) => return config,
                        Err(e) => eprintln!("Failed to parse user rules.toml: {}", e),
                    }
                }
            }
        }

        for path in &bundled_paths {
            if let Ok(content) = std::fs::read_to_string(path) {
                match toml::from_str(&content) {
                    Ok(config) => return config,
                    Err(e) => eprintln!("Failed to parse bundled rules.toml: {}", e),
                }
            }
        }

        RulesConfig::default()
    }

    fn build_index(config: &RulesConfig) -> HashMap<u32, usize> {
        let mut map = HashMap::new();
        for (i, game) in config.game.iter().enumerate() {
            map.insert(game.appid, i);
        }
        map
    }
}

fn find_bundled_rules() -> Vec<PathBuf> {
    let home = dirs::home_dir().unwrap_or_default();
    let candidates = vec![
        home.join("metalsharp/configs/rules.toml"),
        home.join(".metalsharp/configs/rules.toml"),
        PathBuf::from("/tmp/metalsharp-repo/configs/rules.toml"),
    ];
    candidates.into_iter().filter(|p| p.exists()).collect()
}

pub fn detect_engine_from_dir(game_dir: &Option<PathBuf>) -> String {
    let dir = match game_dir {
        Some(d) if d.exists() => d,
        _ => return "steam_d3dmetal_perf".into(),
    };

    if crate::setup::detect_dotnet_game(dir) {
        return "fna_arm64".into();
    }

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
        return "steam_d3dmetal_perf".into();
    }

    if has_dir_ci("engine") && has_dir_ci("binaries") {
        return "steam_metalfx".into();
    }

    if has_glob(".pak") || (has_dir_ci("engine") && has_dir_ci("content")) {
        return "steam_metalfx".into();
    }

    if has_glob(".bdt") || has_glob(".bhd") {
        return "steam_metalfx".into();
    }

    if has_glob("re_chunk_") || has_file_ci("re2_config.ini") || has_file_ci("re8_config.ini") {
        return "steam_d3dmetal_perf".into();
    }

    if has_file_ci("d3dx9_43.dll") {
        return "metalsharp_wine".into();
    }

    if has_file_ci("steam_api64.dll") || has_file_ci("steam_api.dll") {
        return "steam_d3dmetal_perf".into();
    }

    "steam_d3dmetal_perf".into()
}
