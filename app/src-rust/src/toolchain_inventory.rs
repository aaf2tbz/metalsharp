use serde_json::{json, Value};
use std::path::{Path, PathBuf};

const SCHEMA: &str = "metalsharp.toolchain.inventory.v1";

pub fn report() -> Value {
    let home = dirs::home_dir().unwrap_or_default();
    report_for(&home)
}

pub fn report_for(home: &Path) -> Value {
    let llvm_root = std::env::var("METALSHARP_X86_LLVM_ROOT")
        .map(PathBuf::from)
        .unwrap_or_else(|_| home.join(".metalsharp/toolchains/llvm-x86_64"));
    let entries = vec![
        tool("xcode_select", "Xcode selector", PathBuf::from("/usr/bin/xcode-select"), true),
        tool("xcrun", "Xcode tool runner", PathBuf::from("/usr/bin/xcrun"), true),
        tool("clang", "Apple clang", PathBuf::from("/usr/bin/clang"), true),
        tool("lipo", "Mach-O lipo", PathBuf::from("/usr/bin/lipo"), true),
        tool("otool", "Mach-O dependency inspector", PathBuf::from("/usr/bin/otool"), true),
        tool("install_name_tool", "Mach-O install name editor", PathBuf::from("/usr/bin/install_name_tool"), true),
        tool("codesign", "Code signing tool", PathBuf::from("/usr/bin/codesign"), false),
        tool(
            "python3",
            "Python 3",
            first_existing(&[
                PathBuf::from("/usr/bin/python3"),
                PathBuf::from("/opt/homebrew/bin/python3"),
                PathBuf::from("/usr/local/bin/python3"),
            ]),
            false,
        ),
        tool(
            "git",
            "Git",
            first_existing(&[
                PathBuf::from("/usr/bin/git"),
                PathBuf::from("/opt/homebrew/bin/git"),
                PathBuf::from("/usr/local/bin/git"),
            ]),
            false,
        ),
        tool(
            "brew",
            "Homebrew",
            first_existing(&[PathBuf::from("/opt/homebrew/bin/brew"), PathBuf::from("/usr/local/bin/brew")]),
            false,
        ),
        tool(
            "metal_shaderconverter",
            "Apple Metal Shader Converter",
            first_existing(&[
                PathBuf::from("/usr/local/bin/metal-shaderconverter"),
                PathBuf::from("/opt/homebrew/bin/metal-shaderconverter"),
                PathBuf::from("/opt/metal-shaderconverter/bin/metal-shaderconverter"),
            ]),
            false,
        ),
        tool("llvm_config_x86", "x86_64 LLVM llvm-config", llvm_root.join("bin/llvm-config"), false),
        tool("winebuild", "MetalSharp Wine winebuild", home.join(".metalsharp/runtime/wine/bin/winebuild"), false),
        tool(
            "vulkan_sources",
            "Fetched Vulkan lane sources",
            PathBuf::from(".cache/runtime-sources/vulkan-lane-sources.json"),
            false,
        ),
    ];
    let required_total =
        entries.iter().filter(|entry| entry.get("required").and_then(|v| v.as_bool()) == Some(true)).count();
    let required_present = entries
        .iter()
        .filter(|entry| {
            entry.get("required").and_then(|v| v.as_bool()) == Some(true)
                && entry.get("present").and_then(|v| v.as_bool()) == Some(true)
        })
        .count();
    json!({
        "ok": required_present == required_total,
        "schema": SCHEMA,
        "readOnly": true,
        "llvmRoot": llvm_root.to_string_lossy(),
        "summary": {
            "total": entries.len(),
            "requiredTotal": required_total,
            "requiredPresent": required_present,
            "optionalPresent": entries.iter().filter(|entry| entry.get("required").and_then(|v| v.as_bool()) != Some(true) && entry.get("present").and_then(|v| v.as_bool()) == Some(true)).count(),
        },
        "entries": entries,
        "invariants": [
            "Toolchain inventory is read-only and checks paths only; it does not build, download, sign, or invoke Wine.",
            "Optional toolchains are required only for local payload rebuilds, shader conversion, or release packaging."
        ],
    })
}

fn tool(id: &str, label: &str, path: PathBuf, required: bool) -> Value {
    json!({
        "id": id,
        "label": label,
        "path": path.to_string_lossy(),
        "present": path.exists(),
        "required": required,
    })
}

fn first_existing(paths: &[PathBuf]) -> PathBuf {
    paths.iter().find(|path| path.exists()).cloned().unwrap_or_else(|| paths.first().cloned().unwrap_or_default())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn toolchain_inventory_has_release_gate_tools() {
        let home = std::env::temp_dir().join(format!("metalsharp-toolchain-inventory-{}", std::process::id()));
        let report = report_for(&home);
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        let entries = report.get("entries").and_then(|value| value.as_array()).expect("entries");
        for id in [
            "xcode_select",
            "xcrun",
            "clang",
            "lipo",
            "otool",
            "install_name_tool",
            "llvm_config_x86",
            "metal_shaderconverter",
        ] {
            assert!(
                entries.iter().any(|entry| entry.get("id").and_then(|value| value.as_str()) == Some(id)),
                "missing {id}"
            );
        }
    }
}
