use std::path::PathBuf;

pub fn deploy_preset_cache(home: &PathBuf, cache_subdir: &str, appid: u32) -> Option<u64> {
    let preset_db = find_preset(home, cache_subdir, appid)?;

    let cache_base = home.join(".metalsharp").join("shader-cache").join(cache_subdir).join(appid.to_string());
    let _ = std::fs::create_dir_all(&cache_base);

    let user_db = cache_base.join(preset_db.file_name()?.to_string_lossy().to_string());

    if user_db.exists() {
        return merge_preset_into_user(&preset_db, &user_db);
    }

    std::fs::copy(&preset_db, &user_db).ok()
}

fn find_preset(home: &PathBuf, cache_subdir: &str, appid: u32) -> Option<PathBuf> {
    let ms_share = home.join(".metalsharp").join("runtime").join("wine").join("share").join("shader-presets");
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

    #[test]
    fn m9_reuses_dxmt_preset_family() {
        assert_eq!(preset_lookup_subdirs("m9"), vec!["m9", "dxmt-metal"]);
    }
}
