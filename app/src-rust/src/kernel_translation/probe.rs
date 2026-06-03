use serde_json::{json, Map, Value};

pub fn handle_kernel_probe(_body: &Map<String, Value>) -> Value {
    #[cfg(unix)]
    {
        let mmap_result = probe_anonymous_exec();
        let task_for_pid_result = probe_task_for_pid();
        let csops_result = probe_csops();

        json!({
            "ok": true,
            "host": { "os": "macos", "arch": std::env::consts::ARCH },
            "probes": {
                "anonymousExecutableMapping": mmap_result,
                "taskForPid": task_for_pid_result,
                "csops": csops_result,
            },
            "summary": "Kernel translation host capability probe complete.",
        })
    }

    #[cfg(not(unix))]
    {
        json!({"ok": false, "error": "Kernel translation probe requires macOS"})
    }
}

#[cfg(unix)]
fn probe_anonymous_exec() -> Value {
    unsafe {
        let len = 4096;
        let ptr = libc::mmap(
            std::ptr::null_mut(),
            len,
            libc::PROT_READ | libc::PROT_WRITE,
            libc::MAP_PRIVATE | libc::MAP_ANON,
            -1,
            0,
        );
        if ptr == libc::MAP_FAILED {
            return json!({"ok": false, "stage": "mmap_rw", "errno": std::io::Error::last_os_error().raw_os_error()});
        }
        std::ptr::write_bytes(ptr, 0x90, len);
        let rwx = libc::mprotect(ptr, len, libc::PROT_READ | libc::PROT_EXEC);
        let _ = libc::munmap(ptr, len);
        json!({
            "ok": rwx == 0,
            "stage": "mprotect_rx",
            "note": if rwx == 0 { "Anonymous RW→RX transition works" } else { "Anonymous RW→RX transition blocked (PAC/JIT)" },
        })
    }
}

#[cfg(unix)]
fn probe_task_for_pid() -> Value {
    let pid = std::process::id() as i64;
    let mut task: u64 = 0;
    #[allow(deprecated)]
    let kr = unsafe { libc::syscall(50, libc::mach_task_self(), pid, &mut task) };
    json!({
        "ok": kr >= 0,
        "kr": kr,
        "note": if kr >= 0 { "task_for_pid works for own process (expected)" } else { "task_for_pid requires entitlement for cross-process" },
    })
}

#[cfg(unix)]
fn probe_csops() -> Value {
    let pid = std::process::id();
    let mut buf: u32 = 0;
    let r = unsafe { libc::syscall(169, pid, 0u32, &mut buf as *mut u32, std::mem::size_of::<u32>()) };
    json!({
        "ok": r == 0,
        "return": r,
        "note": if r == 0 { "csops works for own process" } else { "csops returned error (may need elevated privileges)" },
    })
}
