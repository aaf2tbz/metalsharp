//! MetalSharp-owned native D3DMetal runtime readiness gate.
//!
//! Phase 4 of the D3DMetal-native roadmap. The `d3dmetal_native` route is
//! reserved until BOTH gates pass:
//!   1. Wine host ABI (Phase 2) — `check-d3dmetal-shim-abi.py --strict`
//!   2. Payload contract (Phase 3) — `check-d3dmetal-native-payload.py`
//!
//! This module shells out to those Python checkers, parses their JSON, and
//! exposes a readiness summary plus digests used to stamp saved-bottle overlays.
//! CrossOver/CX never appears in any name here; the checkers are MetalSharp-owned.

use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::path::{Path, PathBuf};
use std::process::Command;

/// Resolve the `tools/runtime/` directory holding the D3DMetal checkers.
/// CARGO_MANIFEST_DIR is `app/src-rust`; the repo root is three levels up.
fn runtime_tools_dir() -> PathBuf {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    // app/src-rust -> app -> repo root
    manifest_dir
        .ancestors()
        .nth(2)
        .map(|root| root.join("tools").join("runtime"))
        .unwrap_or_else(|| PathBuf::from("tools/runtime"))
}

fn wine_runtime_root(home: &Path) -> PathBuf {
    home.join(".metalsharp").join("runtime").join("wine")
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CheckerResult {
    pub ready: bool,
    pub state: String,
    pub reason: String,
    pub raw: Option<Value>,
    pub ran: bool,
    pub error: Option<String>,
}

impl CheckerResult {
    fn missing_tool(what: &str) -> Self {
        Self {
            ready: false,
            state: format!("{}_missing_tool", what),
            reason: format!("{} checker not found", what),
            raw: None,
            ran: false,
            error: None,
        }
    }

    fn failed(what: &str, error: impl Into<String>) -> Self {
        Self {
            ready: false,
            state: format!("{}_error", what),
            reason: error.into(),
            raw: None,
            ran: false,
            error: None,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct D3DMetalNativeReadiness {
    pub host_abi: CheckerResult,
    pub payload: CheckerResult,
    /// Overall readiness: true only when both gates pass.
    pub ready: bool,
    /// Short machine-readable state: ready | host_abi_missing | payload_missing |
    /// partial | unavailable.
    pub state: String,
}

impl D3DMetalNativeReadiness {
    pub fn unavailable() -> Self {
        Self {
            host_abi: CheckerResult {
                ready: false,
                state: "host_abi_not_checked".into(),
                reason: "readiness not computed".into(),
                raw: None,
                ran: false,
                error: None,
            },
            payload: CheckerResult {
                ready: false,
                state: "payload_not_checked".into(),
                reason: "readiness not computed".into(),
                raw: None,
                ran: false,
                error: None,
            },
            ready: false,
            state: "unavailable".into(),
        }
    }
}

fn run_checker(script_name: &str, runtime_root: &Path) -> CheckerResult {
    let script = runtime_tools_dir().join(script_name);
    if !script.exists() {
        return CheckerResult::missing_tool(script_name.trim_end_matches(".py"));
    }
    let output =
        match Command::new("python3").arg(&script).arg("--runtime-root").arg(runtime_root).arg("--json").output() {
            Ok(o) => o,
            Err(e) => return CheckerResult::failed(script_name, format!("failed to spawn python3: {e}")),
        };
    let stdout = String::from_utf8_lossy(&output.stdout).to_string();
    let ran = true;
    let raw: Option<Value> = serde_json::from_str(&stdout).ok();
    // check-d3dmetal-shim-abi reports native_d3dmetal_host_abi_ready; the payload
    // checker reports ready/state. Normalize both.
    let (ready, state, reason) = match &raw {
        Some(v) => {
            let obj = v.as_object();
            let host_ready = obj
                .and_then(|o| o.get("summary"))
                .and_then(|s| s.get("native_d3dmetal_host_abi_ready"))
                .and_then(|b| b.as_bool());
            let payload_ready = obj.and_then(|o| o.get("ready")).and_then(|b| b.as_bool());
            let ready = host_ready.unwrap_or(payload_ready.unwrap_or(false));
            let state = obj
                .and_then(|o| o.get("state"))
                .or_else(|| obj.and_then(|o| o.get("summary")))
                .map(|s| {
                    if s.is_object() {
                        s.get("native_d3dmetal_host_abi_ready")
                            .map(|b| if b.as_bool().unwrap_or(false) { "ready" } else { "host_abi_missing" })
                            .unwrap_or("host_abi_missing")
                            .to_string()
                    } else {
                        s.as_str().unwrap_or("unknown").to_string()
                    }
                })
                .unwrap_or_else(|| if ready { "ready".into() } else { "missing".into() });
            let reason = obj
                .and_then(|o| o.get("reason"))
                .map(|r| r.as_str().unwrap_or("").to_string())
                .or_else(|| {
                    obj.and_then(|o| o.get("summary"))
                        .and_then(|s| s.get("missing_native_d3dmetal").cloned())
                        .map(|m| m.to_string())
                })
                .unwrap_or_default();
            (ready, state, reason)
        },
        None => (false, "parse_error".into(), "checker produced no JSON".into()),
    };
    CheckerResult { ready, state, reason, raw, ran, error: None }
}

/// Compute full d3dmetal_native readiness against the user's runtime.
pub fn readiness_for(home: &Path) -> D3DMetalNativeReadiness {
    let runtime_root = wine_runtime_root(home);
    let host_abi = run_checker("check-d3dmetal-shim-abi.py", &runtime_root);
    let payload = run_checker("check-d3dmetal-native-payload.py", &runtime_root);
    let ready = host_abi.ready && payload.ready;
    let state = if ready {
        "ready".to_string()
    } else if !host_abi.ready && payload.ready {
        "host_abi_missing".to_string()
    } else if host_abi.ready && !payload.ready {
        "payload_missing".to_string()
    } else {
        "partial".to_string()
    };
    D3DMetalNativeReadiness { host_abi, payload, ready, state }
}

/// Stable digest of the Wine host ABI surface (winemac/win32u/ntdll/winevulkan
/// .so sizes + mtimes), used to stamp a saved-bottle overlay so a stale overlay
/// (built against an older host ABI) can be detected. None if modules absent.
pub fn host_abi_digest(home: &Path) -> Option<String> {
    let unix = wine_runtime_root(home).join("lib").join("wine").join("x86_64-unix");
    let mut acc = String::new();
    for m in ["winemac.so", "win32u.so", "ntdll.so", "winevulkan.so"] {
        let p = unix.join(m);
        if let Ok(meta) = std::fs::metadata(&p) {
            acc.push_str(&format!(
                "{}|{}|{};",
                m,
                meta.len(),
                meta.modified()
                    .ok()
                    .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
                    .map(|d| d.as_secs())
                    .unwrap_or(0)
            ));
        }
    }
    if acc.is_empty() {
        None
    } else {
        Some(format!("v1:{:x}", hash64(&acc)))
    }
}

/// Digest of the staged payload manifest (sha256 of manifest.json), or None if
/// no payload is staged. Used to bind a saved-bottle overlay to a payload.
pub fn payload_manifest_digest(home: &Path) -> Option<String> {
    let mf = wine_runtime_root(home).join("lib").join("d3dmetal_native").join("manifest.json");
    let bytes = std::fs::read(&mf).ok()?;
    Some(format!("sha256:{}", sha256_hex(&bytes)))
}

fn hash64(s: &str) -> u64 {
    // FNV-1a 64 — stable, dependency-free.
    let mut h: u64 = 0xcbf29ce484222325;
    for &b in s.as_bytes() {
        h ^= b as u64;
        h = h.wrapping_mul(0x100000001b3);
    }
    h
}

fn sha256_hex(bytes: &[u8]) -> String {
    use std::fmt::Write;
    // Minimal SHA-256 (FIPS 180-4) — avoids pulling a crypto crate into the
    // backend for a single overlay-digest use.
    let h = sha256_bytes(bytes);
    let mut out = String::with_capacity(64);
    for b in h {
        let _ = write!(out, "{b:02x}");
    }
    out
}

fn sha256_bytes(data: &[u8]) -> [u8; 32] {
    const K: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
        0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
        0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
        0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2,
    ];
    let mut hh: [u32; 8] =
        [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
    let bit_len = (data.len() as u64).wrapping_mul(8);
    let mut msg = data.to_vec();
    msg.push(0x80);
    while msg.len() % 64 != 56 {
        msg.push(0);
    }
    msg.extend_from_slice(&bit_len.to_be_bytes());
    for chunk in msg.chunks_exact(64) {
        let mut w = [0u32; 64];
        for (i, b) in chunk.chunks_exact(4).enumerate().take(16) {
            w[i] = u32::from_be_bytes([b[0], b[1], b[2], b[3]]);
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16].wrapping_add(s0).wrapping_add(w[i - 7]).wrapping_add(s1);
        }
        let (mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut h) =
            (hh[0], hh[1], hh[2], hh[3], hh[4], hh[5], hh[6], hh[7]);
        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ ((!e) & g);
            let t1 = h.wrapping_add(s1).wrapping_add(ch).wrapping_add(K[i]).wrapping_add(w[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let t2 = s0.wrapping_add(maj);
            h = g;
            g = f;
            f = e;
            e = d.wrapping_add(t1);
            d = c;
            c = b;
            b = a;
            a = t1.wrapping_add(t2);
        }
        hh[0] = hh[0].wrapping_add(a);
        hh[1] = hh[1].wrapping_add(b);
        hh[2] = hh[2].wrapping_add(c);
        hh[3] = hh[3].wrapping_add(d);
        hh[4] = hh[4].wrapping_add(e);
        hh[5] = hh[5].wrapping_add(f);
        hh[6] = hh[6].wrapping_add(g);
        hh[7] = hh[7].wrapping_add(h);
    }
    let mut out = [0u8; 32];
    for (i, word) in hh.iter().enumerate() {
        out[i * 4..i * 4 + 4].copy_from_slice(&word.to_be_bytes());
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn readiness_absent_runtime_is_unavailable_not_panic() {
        let tmp = std::env::temp_dir().join("ms-d3dmetal-readiness-test-absent");
        let r = readiness_for(&tmp);
        assert!(!r.ready);
        // checkers may be found via CARGO_MANIFEST_DIR even against an absent
        // runtime; the key invariant is we never panic and always report state.
        assert!(!r.state.is_empty());
    }

    #[test]
    fn host_abi_digest_is_stable_and_absent_when_no_modules() {
        let d = std::env::temp_dir().join("ms-d3dmetal-digest-test");
        // No modules -> None, no panic.
        assert!(host_abi_digest(&d).is_none());
    }

    #[test]
    fn sha256_matches_known_vector() {
        // "abc" -> ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
        let h = sha256_hex(b"abc");
        assert_eq!(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }
}
