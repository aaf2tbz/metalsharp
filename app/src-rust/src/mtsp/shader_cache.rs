use std::path::{Path, PathBuf};

pub fn deploy_preset_cache(home: &PathBuf, cache_subdir: &str, appid: u32) -> Option<u64> {
    let cache_base =
        crate::platform::metalsharp_home_dir_for(&home).join("shader-cache").join(cache_subdir).join(appid.to_string());
    deploy_preset_cache_to(home, cache_subdir, appid, &cache_base)
}

pub fn deploy_preset_cache_to(home: &PathBuf, cache_subdir: &str, appid: u32, cache_base: &Path) -> Option<u64> {
    let _ = std::fs::create_dir_all(cache_base);

    let mut deployed = 0;

    if let Some(preset_db) = find_preset(home, cache_subdir, appid) {
        let user_db = cache_base.join(preset_db.file_name()?.to_string_lossy().to_string());

        if user_db.exists() {
            deployed += merge_preset_into_user(&preset_db, &user_db).unwrap_or(0);
        } else if std::fs::copy(&preset_db, &user_db).is_ok() {
            deployed += 1;
        }
    }

    if cache_subdir == "m12" {
        deployed += deploy_m12_shader_engine_cache(home, appid, cache_base);
    }

    (deployed > 0).then_some(deployed)
}

fn find_preset(home: &PathBuf, cache_subdir: &str, appid: u32) -> Option<PathBuf> {
    let ms_share = crate::platform::metalsharp_home_dir_for(&home)
        .join("runtime")
        .join("wine")
        .join("share")
        .join("shader-presets");
    let exe_presets = find_exe_relative_presets();

    let search_dirs: Vec<PathBuf> = {
        let mut dirs = vec![ms_share];
        dirs.extend(exe_presets);
        dirs
    };

    for subdir in preset_lookup_subdirs(cache_subdir) {
        for preset_dir in &search_dirs {
            if let Some(p) = check_preset_dir(preset_dir, subdir, appid) {
                return Some(p);
            }
        }
    }

    None
}

fn preset_lookup_subdirs(cache_subdir: &str) -> Vec<&str> {
    match cache_subdir {
        "m9" | "m10" | "m11" => vec![cache_subdir, "dxmt-metal"],
        "m12" => vec![cache_subdir, "dxmt-metal12"],
        _ => vec![cache_subdir],
    }
}

fn find_exe_relative_presets() -> Vec<PathBuf> {
    let mut results = Vec::new();
    if let Ok(exe) = std::env::current_exe() {
        if let Some(mut dir) = exe.parent() {
            for _ in 0..8 {
                let candidate = dir.join("shader-presets");
                if candidate.exists() {
                    results.push(candidate);
                }
                match dir.parent() {
                    Some(p) => dir = p,
                    None => break,
                }
            }
        }
    }
    results
}

fn check_preset_dir(preset_dir: &PathBuf, cache_subdir: &str, appid: u32) -> Option<PathBuf> {
    let engine_dir = preset_dir.join(cache_subdir);
    if !engine_dir.exists() {
        return None;
    }

    let candidates =
        vec![engine_dir.join(format!("{}.db", appid)), engine_dir.join(format!("shaders_320_{}.db", appid))];

    for c in &candidates {
        if c.exists() {
            return Some(c.clone());
        }
    }

    if let Ok(entries) = std::fs::read_dir(&engine_dir) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            if name.starts_with(&appid.to_string()) && name.ends_with(".db") {
                return Some(entry.path());
            }
        }
    }

    None
}

fn deploy_m12_shader_engine_cache(home: &PathBuf, appid: u32, cache_base: &Path) -> u64 {
    let mut copied = 0;
    for source_dir in find_m12_shader_engine_sources(home, appid) {
        copied += copy_shader_engine_files(&source_dir, cache_base);
    }
    copied
}

fn find_m12_shader_engine_sources(home: &PathBuf, appid: u32) -> Vec<PathBuf> {
    let ms_home = crate::platform::metalsharp_home_dir_for(home);
    let mut roots = vec![
        ms_home.join("runtime").join("wine").join("share").join("d3d12-metal-sdk").join("shader-corpus"),
        ms_home.join("runtime").join("d3d12-metal-sdk").join("shader-corpus"),
        ms_home.join("scripts").join("tools").join("d3d12-metal-sdk").join("shader-corpus"),
        ms_home.join("runtime").join("wine").join("share").join("shader-presets").join("m12").join(appid.to_string()),
        ms_home
            .join("runtime")
            .join("wine")
            .join("share")
            .join("shader-presets")
            .join("dxmt-metal12")
            .join(appid.to_string()),
    ];

    if let Ok(source) = std::env::var("METALSHARP_M12_SHADER_ENGINE_SOURCE") {
        roots.push(PathBuf::from(source));
    }

    if let Some(resources) = crate::platform::app_resources_dir() {
        roots.push(resources.join("d3d12-metal-sdk").join("shader-corpus"));
        roots.push(resources.join("tools").join("d3d12-metal-sdk").join("shader-corpus"));
    }

    roots.into_iter().filter(|path| path.is_dir()).collect()
}

fn copy_shader_engine_files(source_dir: &Path, cache_base: &Path) -> u64 {
    let mut copied = 0;
    let Ok(entries) = std::fs::read_dir(source_dir) else {
        return 0;
    };

    for entry in entries.flatten() {
        let path = entry.path();
        let name = entry.file_name().to_string_lossy().to_string();
        if path.is_dir() {
            if matches!(name.as_str(), "proof" | "results" | "logs" | "direct-metal-errors") {
                continue;
            }
            copied += copy_shader_engine_files(&path, cache_base);
            continue;
        }

        if !is_m12_shader_engine_file(&path) {
            continue;
        }
        let dest = cache_base.join(entry.file_name());
        if dest.exists() && files_match(&path, &dest) {
            continue;
        }
        if std::fs::copy(&path, &dest).is_ok() {
            copied += 1;
        }
    }

    copied
}

fn is_m12_shader_engine_file(path: &Path) -> bool {
    matches!(
        path.extension().and_then(|ext| ext.to_str()),
        Some("metallib" | "air" | "msl" | "dxbc" | "dxil" | "cso" | "json")
    ) || path
        .file_name()
        .and_then(|name| name.to_str())
        .map(|name| name.ends_with(".module.txt") || name.ends_with(".dxil_report.txt"))
        .unwrap_or(false)
}

fn files_match(a: &Path, b: &Path) -> bool {
    match (std::fs::read(a), std::fs::read(b)) {
        (Ok(a), Ok(b)) => a == b,
        _ => false,
    }
}

fn merge_preset_into_user(preset_db: &PathBuf, user_db: &PathBuf) -> Option<u64> {
    let mut inserted: u64 = 0;

    let preset_conn = match rusqlite::Connection::open_with_flags(preset_db, rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY)
    {
        Ok(c) => c,
        Err(_) => return None,
    };

    let user_conn = match rusqlite::Connection::open(user_db) {
        Ok(c) => c,
        Err(_) => return None,
    };

    let table_names: Vec<String> = preset_conn
        .prepare("SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'cache_%'")
        .ok()
        .map(|mut stmt| {
            stmt.query_map([], |row| row.get(0))
                .ok()
                .map(|iter| iter.filter_map(|r| r.ok()).collect())
                .unwrap_or_default()
        })
        .unwrap_or_default();

    if table_names.is_empty() {
        return None;
    }

    let _ = user_conn.execute_batch("PRAGMA journal_mode=WAL;");

    for table in &table_names {
        let _ =
            user_conn.execute(&format!("CREATE TABLE IF NOT EXISTS {} (key BLOB PRIMARY KEY, value BLOB)", table), []);

        let mut sel_stmt = match preset_conn.prepare(&format!("SELECT key, value FROM {}", table)) {
            Ok(s) => s,
            Err(_) => continue,
        };

        let rows: Vec<(Vec<u8>, Vec<u8>)> = sel_stmt
            .query_map([], |row| {
                let key: Vec<u8> = row.get(0)?;
                let val: Vec<u8> = row.get(1)?;
                Ok((key, val))
            })
            .ok()
            .map(|iter| iter.filter_map(|r| r.ok()).collect())
            .unwrap_or_default();

        let _ = user_conn.execute_batch("BEGIN TRANSACTION;");

        let ins_sql = format!("INSERT OR IGNORE INTO {} (key, value) VALUES (?1, ?2)", table);
        if let Ok(mut ins_stmt) = user_conn.prepare(&ins_sql) {
            for (key, val) in &rows {
                if ins_stmt.execute(rusqlite::params![key, val]).is_ok() {
                    inserted += 1;
                }
            }
        }

        let _ = user_conn.execute_batch("COMMIT;");
    }

    if inserted > 0 {
        Some(inserted)
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn test_dir(name: &str) -> PathBuf {
        let nonce = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_nanos();
        std::env::temp_dir().join(format!("metalsharp-shader-cache-{}-{}", name, nonce))
    }

    #[test]
    fn m10_reuses_shared_dxmt_metal_preset_family() {
        assert_eq!(preset_lookup_subdirs("m10"), vec!["m10", "dxmt-metal"]);
    }

    #[test]
    fn m9_reuses_dxmt_preset_family() {
        assert_eq!(preset_lookup_subdirs("m9"), vec!["m9", "dxmt-metal"]);
    }

    #[test]
    fn m12_deploys_file_shader_engine_corpus_to_selected_cache_path() {
        let home = test_dir("m12-file-corpus");
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        let corpus = ms_home
            .join("runtime")
            .join("wine")
            .join("share")
            .join("d3d12-metal-sdk")
            .join("shader-corpus")
            .join("baseline");
        let metallib = corpus.join("metallib");
        let msl = corpus.join("msl");
        let air = corpus.join("air");
        let proof = corpus.join("proof");
        std::fs::create_dir_all(&metallib).expect("create metallib corpus");
        std::fs::create_dir_all(&msl).expect("create msl corpus");
        std::fs::create_dir_all(&air).expect("create air corpus");
        std::fs::create_dir_all(&proof).expect("create proof corpus");

        std::fs::write(metallib.join("abc.metallib"), b"metallib").expect("write metallib");
        std::fs::write(msl.join("abc.msl"), b"msl").expect("write msl");
        std::fs::write(air.join("abc.air"), b"air").expect("write air");
        std::fs::write(corpus.join("pso-render-abc.json"), b"{}").expect("write pso");
        std::fs::write(proof.join("compile-summary.txt"), b"not runtime cache").expect("write proof");

        let selected_cache =
            home.join("Game").join(".metalsharp-cache").join("shader-cache").join("m12").join("1962700");
        let deployed = deploy_preset_cache_to(&home, "m12", 1962700, &selected_cache).expect("deploy m12 corpus");

        assert_eq!(deployed, 4);
        assert!(selected_cache.join("abc.metallib").is_file());
        assert!(selected_cache.join("abc.msl").is_file());
        assert!(selected_cache.join("abc.air").is_file());
        assert!(selected_cache.join("pso-render-abc.json").is_file());
        assert!(!selected_cache.join("compile-summary.txt").exists());

        let _ = std::fs::remove_dir_all(home);
    }
}
