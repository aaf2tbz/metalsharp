use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Duration;

pub(crate) const PREFIX_RUNTIME_INIT_TIMEOUT_SECS: u64 = 10;
pub(crate) const PREFIX_RUNTIME_INIT_ARGS: &[&str] = &["cmd", "/c", "exit"];
const M12_REQUIRED_CORPUS_PROOF: &[&str] = &["elden-ring-present-vb-pull-20260612", "proof", "SHA256SUMS"];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct PrefixRuntimeSurfaceReport {
    pub windows_files: usize,
    pub unix_files: usize,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct M12PipelineMaterialReport {
    pub material_files: usize,
}

pub(crate) fn stage_prefix_runtime_surface(runtime_wine: &Path, prefix: &Path) -> Result<usize, String> {
    let system32 = prefix.join("drive_c").join("windows").join("system32");
    let unix_surface = prefix.join(".metalsharp").join("unix");
    fs::create_dir_all(&system32).map_err(|e| format!("create prefix system32 for {}: {}", prefix.display(), e))?;
    fs::create_dir_all(&unix_surface)
        .map_err(|e| format!("create prefix unix surface for {}: {}", prefix.display(), e))?;

    let mut copied = 0usize;
    for (subdir, filename) in prefix_runtime_dll_sources() {
        let source = runtime_wine.join(subdir).join(filename);
        if !source.is_file() {
            return Err(format!("required runtime DLL missing for prefix init: {}", source.display()));
        }
        let dest = system32.join(filename);
        copy_runtime_file_if_changed(&source, &dest)
            .map_err(|e| format!("stage {} to {}: {}", source.display(), dest.display(), e))?;
        copied += 1;
    }

    for filename in prefix_runtime_unix_sidecars() {
        let source = prefix_runtime_unix_source(runtime_wine, filename)
            .ok_or_else(|| format!("required Unix sidecar missing for prefix init: {}", filename))?;
        let dest = unix_surface.join(filename);
        copy_runtime_file_if_changed(&source, &dest)
            .map_err(|e| format!("stage {} to {}: {}", source.display(), dest.display(), e))?;
        copied += 1;
    }

    Ok(copied)
}

pub(crate) fn validate_prefix_runtime_surface(
    runtime_wine: &Path,
    prefix: &Path,
) -> Result<PrefixRuntimeSurfaceReport, String> {
    let system32 = prefix.join("drive_c").join("windows").join("system32");
    let unix_surface = prefix.join(".metalsharp").join("unix");

    let mut windows_files = 0usize;
    for (subdir, filename) in prefix_runtime_dll_sources() {
        let source = runtime_wine.join(subdir).join(filename);
        if !source.is_file() {
            return Err(format!("required runtime DLL missing for prefix validation: {}", source.display()));
        }
        let dest = system32.join(filename);
        validate_matching_file(&source, &dest, filename)?;
        windows_files += 1;
    }

    let mut unix_files = 0usize;
    for filename in prefix_runtime_unix_sidecars() {
        let source = prefix_runtime_unix_source(runtime_wine, filename)
            .ok_or_else(|| format!("required Unix sidecar missing for prefix validation: {}", filename))?;
        let dest = unix_surface.join(filename);
        validate_matching_file(&source, &dest, filename)?;
        unix_files += 1;
    }

    Ok(PrefixRuntimeSurfaceReport { windows_files, unix_files })
}

pub(crate) fn validate_m12_pipeline_material(ms_home: &Path) -> Result<M12PipelineMaterialReport, String> {
    let sources = m12_pipeline_material_sources(ms_home);
    let mut material_files = 0usize;
    let mut proof_sources = Vec::new();
    for source in &sources {
        if !source.is_dir() {
            continue;
        }
        let proof = M12_REQUIRED_CORPUS_PROOF.iter().fold(source.clone(), |path, segment| path.join(segment));
        if proof.is_file() {
            proof_sources.push(source.clone());
            material_files += count_m12_pipeline_material(source);
        }
    }

    if proof_sources.is_empty() {
        let searched = sources.iter().map(|path| path.display().to_string()).collect::<Vec<_>>().join(", ");
        return Err(format!(
            "M12 pipeline material missing; expected shader-engine corpus proof {} under {}",
            M12_REQUIRED_CORPUS_PROOF.join("/"),
            searched
        ));
    }

    if material_files == 0 {
        let searched = proof_sources.iter().map(|path| path.display().to_string()).collect::<Vec<_>>().join(", ");
        return Err(format!("M12 pipeline material incomplete; no shader-engine files found under {}", searched));
    }

    Ok(M12PipelineMaterialReport { material_files })
}

pub(crate) fn validate_install_wine_init_surface(
    ms_home: &Path,
) -> Result<(PrefixRuntimeSurfaceReport, M12PipelineMaterialReport), String> {
    let runtime_wine = ms_home.join("runtime").join("wine");
    let prefix = ms_home.join("prefix-steam");
    let runtime = validate_prefix_runtime_surface(&runtime_wine, &prefix)?;
    let pipeline = validate_m12_pipeline_material(ms_home)?;
    Ok((runtime, pipeline))
}

pub(crate) fn run_bounded_prefix_runtime_init(wine: &Path, runtime_wine: &Path, prefix: &Path) -> Result<(), String> {
    let mut cmd = Command::new(wine);
    cmd.env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "/usr/bin/true")
        .env("WINEDLLOVERRIDES", "winedbg=d;steam,steamwebhelper,steamservice=d")
        .env("WINEDLLPATH", prefix_runtime_winedllpath(runtime_wine))
        .args(PREFIX_RUNTIME_INIT_ARGS)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    crate::platform::set_runtime_library_env(&mut cmd, runtime_wine);

    let mut child = cmd.spawn().map_err(|e| format!("spawn prefix runtime init for {}: {}", prefix.display(), e))?;

    for _ in 0..(PREFIX_RUNTIME_INIT_TIMEOUT_SECS * 2) {
        if let Some(status) =
            child.try_wait().map_err(|e| format!("wait prefix runtime init for {}: {}", prefix.display(), e))?
        {
            if status.success() {
                return Ok(());
            }
            return Err(format!(
                "prefix runtime init failed for {} with exit code: {:?}",
                prefix.display(),
                status.code()
            ));
        }
        std::thread::sleep(Duration::from_millis(500));
    }

    let _ = child.kill();
    let _ = child.wait();
    Err(format!(
        "prefix runtime init exceeded {} seconds for {}; killed init process",
        PREFIX_RUNTIME_INIT_TIMEOUT_SECS,
        prefix.display()
    ))
}

pub(crate) fn prefix_runtime_winedllpath(runtime_wine: &Path) -> String {
    [
        runtime_wine.join("lib").join("dxmt").join("x86_64-windows"),
        runtime_wine.join("lib").join("wine").join("x86_64-windows"),
        runtime_wine.join("lib").join("metalsharp").join("x86_64-windows"),
    ]
    .iter()
    .map(|path| path.to_string_lossy().to_string())
    .collect::<Vec<_>>()
    .join(":")
}

pub(crate) fn prefix_runtime_dll_sources() -> &'static [(&'static str, &'static str)] {
    &[
        ("lib/wine/x86_64-windows", "d3d9.dll"),
        ("lib/wine/x86_64-windows", "d3d10.dll"),
        ("lib/wine/x86_64-windows", "d3d10_1.dll"),
        ("lib/dxmt/x86_64-windows", "d3d10core.dll"),
        ("lib/dxmt/x86_64-windows", "d3d11.dll"),
        ("lib/dxmt/x86_64-windows", "d3d12.dll"),
        ("lib/dxmt/x86_64-windows", "dxgi.dll"),
        ("lib/dxmt/x86_64-windows", "dxgi_dxmt.dll"),
        ("lib/dxmt/x86_64-windows", "winemetal.dll"),
        ("lib/dxmt/x86_64-windows", "nvapi64.dll"),
        ("lib/dxmt/x86_64-windows", "nvngx.dll"),
        ("lib/metalsharp/x86_64-windows", "metalsharp_ntdll_hook.dll"),
    ]
}

pub(crate) fn prefix_runtime_unix_sidecars() -> &'static [&'static str] {
    &["winemetal.so", "winemac.so", "ntdll.so", "libc++.1.dylib", "libc++abi.1.dylib", "libunwind.1.dylib"]
}

pub(crate) fn prefix_runtime_unix_source(runtime_wine: &Path, filename: &str) -> Option<PathBuf> {
    [
        runtime_wine.join("lib").join("dxmt").join("x86_64-unix").join(filename),
        runtime_wine.join("lib").join("wine").join("x86_64-unix").join(filename),
    ]
    .into_iter()
    .find(|path| path.is_file())
}

fn m12_pipeline_material_sources(ms_home: &Path) -> Vec<PathBuf> {
    vec![
        ms_home.join("runtime").join("wine").join("share").join("d3d12-metal-sdk").join("shader-corpus"),
        ms_home.join("runtime").join("d3d12-metal-sdk").join("shader-corpus"),
        ms_home.join("scripts").join("tools").join("d3d12-metal-sdk").join("shader-corpus"),
    ]
}

fn count_m12_pipeline_material(path: &Path) -> usize {
    let Ok(entries) = fs::read_dir(path) else {
        return 0;
    };

    let mut count = 0usize;
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            count += count_m12_pipeline_material(&path);
        } else if is_m12_pipeline_material_file(&path) {
            count += 1;
        }
    }
    count
}

fn is_m12_pipeline_material_file(path: &Path) -> bool {
    matches!(
        path.extension().and_then(|ext| ext.to_str()),
        Some("metallib" | "air" | "msl" | "dxbc" | "dxil" | "cso" | "json")
    )
}

fn validate_matching_file(source: &Path, dest: &Path, label: &str) -> Result<(), String> {
    if !dest.is_file() {
        return Err(format!("prefix runtime file missing for {}: {}", label, dest.display()));
    }
    if !files_match(source, dest) {
        return Err(format!(
            "prefix runtime file stale for {}: {} does not match {}",
            label,
            dest.display(),
            source.display()
        ));
    }
    Ok(())
}

fn copy_runtime_file_if_changed(source: &Path, dest: &Path) -> std::io::Result<()> {
    if dest.is_file() && files_match(source, dest) {
        return Ok(());
    }
    if let Some(parent) = dest.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::copy(source, dest)?;
    Ok(())
}

fn files_match(left: &Path, right: &Path) -> bool {
    match (fs::read(left), fs::read(right)) {
        (Ok(left), Ok(right)) => left == right,
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn test_dir(name: &str) -> PathBuf {
        let nonce = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_nanos();
        std::env::temp_dir().join(format!("metalsharp-prefix-runtime-{}-{}", name, nonce))
    }

    fn write_prefix_runtime(ms_home: &Path) {
        let runtime_wine = ms_home.join("runtime").join("wine");
        for (subdir, filename) in prefix_runtime_dll_sources() {
            let path = runtime_wine.join(subdir).join(filename);
            fs::create_dir_all(path.parent().unwrap()).expect("create runtime DLL parent");
            fs::write(path, format!("runtime-{}", filename)).expect("write runtime DLL");
        }
        for filename in prefix_runtime_unix_sidecars() {
            let path = runtime_wine.join("lib").join("wine").join("x86_64-unix").join(filename);
            fs::create_dir_all(path.parent().unwrap()).expect("create runtime Unix parent");
            fs::write(path, format!("wine-{}", filename)).expect("write runtime Unix sidecar");
        }
    }

    #[test]
    fn install_wine_init_surface_stages_and_validates_runtime_files() {
        let home = test_dir("stage-validate");
        let ms_home = home.join(".metalsharp");
        write_prefix_runtime(&ms_home);
        let runtime_wine = ms_home.join("runtime").join("wine");
        let dxmt_winemetal = runtime_wine.join("lib").join("dxmt").join("x86_64-unix").join("winemetal.so");
        fs::create_dir_all(dxmt_winemetal.parent().unwrap()).expect("create DXMT Unix parent");
        fs::write(&dxmt_winemetal, b"new-dxmt-winemetal").expect("write DXMT winemetal");
        let pipeline_material = ms_home
            .join("runtime")
            .join("wine")
            .join("share")
            .join("d3d12-metal-sdk")
            .join("shader-corpus")
            .join("elden-ring-present-vb-pull-20260612")
            .join("metallib")
            .join("pso.json");
        fs::create_dir_all(pipeline_material.parent().unwrap()).expect("create pipeline material parent");
        fs::write(&pipeline_material, b"{}").expect("write pipeline material");
        let proof = ms_home
            .join("runtime")
            .join("wine")
            .join("share")
            .join("d3d12-metal-sdk")
            .join("shader-corpus")
            .join("elden-ring-present-vb-pull-20260612")
            .join("proof")
            .join("SHA256SUMS");
        fs::create_dir_all(proof.parent().unwrap()).expect("create pipeline proof parent");
        fs::write(&proof, b"pso.json").expect("write pipeline proof");

        let prefix = ms_home.join("prefix-steam");
        let copied = stage_prefix_runtime_surface(&runtime_wine, &prefix).expect("stage prefix runtime");
        let (runtime, pipeline) = validate_install_wine_init_surface(&ms_home).expect("validate install surface");

        assert_eq!(copied, prefix_runtime_dll_sources().len() + prefix_runtime_unix_sidecars().len());
        assert_eq!(runtime.windows_files, prefix_runtime_dll_sources().len());
        assert_eq!(runtime.unix_files, prefix_runtime_unix_sidecars().len());
        assert_eq!(pipeline.material_files, 1);
        assert_eq!(
            fs::read(prefix.join(".metalsharp").join("unix").join("winemetal.so")).expect("read staged winemetal"),
            b"new-dxmt-winemetal"
        );
        assert_eq!(prefix_runtime_unix_source(&runtime_wine, "winemetal.so"), Some(dxmt_winemetal));

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn install_wine_init_validation_rejects_stale_prefix_dll() {
        let home = test_dir("stale-prefix-dll");
        let ms_home = home.join(".metalsharp");
        write_prefix_runtime(&ms_home);
        let runtime_wine = ms_home.join("runtime").join("wine");
        let prefix = ms_home.join("prefix-steam");
        stage_prefix_runtime_surface(&runtime_wine, &prefix).expect("stage prefix runtime");
        fs::write(prefix.join("drive_c").join("windows").join("system32").join("d3d12.dll"), b"stale-d3d12")
            .expect("write stale d3d12");

        let error = validate_prefix_runtime_surface(&runtime_wine, &prefix).expect_err("stale d3d12 should fail");

        assert!(error.contains("d3d12.dll"));
        assert!(error.contains("stale"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn install_wine_init_validation_requires_m12_pipeline_material() {
        let home = test_dir("missing-pipeline-material");
        let ms_home = home.join(".metalsharp");
        write_prefix_runtime(&ms_home);
        let runtime_wine = ms_home.join("runtime").join("wine");
        let prefix = ms_home.join("prefix-steam");
        stage_prefix_runtime_surface(&runtime_wine, &prefix).expect("stage prefix runtime");

        let error = validate_install_wine_init_surface(&ms_home).expect_err("missing pipeline material should fail");

        assert!(error.contains("M12 pipeline material missing"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn m12_pipeline_material_requires_corpus_proof() {
        let home = test_dir("pipeline-material-proof");
        let ms_home = home.join(".metalsharp");
        let material = ms_home
            .join("runtime")
            .join("wine")
            .join("share")
            .join("d3d12-metal-sdk")
            .join("shader-corpus")
            .join("baseline")
            .join("seed.metallib");
        fs::create_dir_all(material.parent().unwrap()).expect("create material parent");
        fs::write(&material, b"metallib").expect("write material");

        let error = validate_m12_pipeline_material(&ms_home).expect_err("missing proof should fail");

        assert!(error.contains("SHA256SUMS"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn prefix_runtime_winedllpath_prioritizes_dxmt_before_wine() {
        let runtime = PathBuf::from("/tmp/runtime/wine");

        let path = prefix_runtime_winedllpath(&runtime);
        let parts = path.split(':').collect::<Vec<_>>();

        assert_eq!(parts[0], "/tmp/runtime/wine/lib/dxmt/x86_64-windows");
        assert_eq!(parts[1], "/tmp/runtime/wine/lib/wine/x86_64-windows");
        assert_eq!(parts[2], "/tmp/runtime/wine/lib/metalsharp/x86_64-windows");
    }

    #[test]
    fn prefix_runtime_init_uses_bounded_cmd_probe_not_wineboot() {
        assert_eq!(PREFIX_RUNTIME_INIT_ARGS, &["cmd", "/c", "exit"]);
        assert!(!PREFIX_RUNTIME_INIT_ARGS.contains(&"wineboot"));
        assert_eq!(PREFIX_RUNTIME_INIT_TIMEOUT_SECS, 10);
    }
}
