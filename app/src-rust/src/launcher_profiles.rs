use serde_json::{json, Value};

const SCHEMA: &str = "metalsharp.launcher.profiles.v1";
const PROFILE_SCHEMA: &str = "metalsharp.launcher.profile.v1";

struct LauncherProfile {
    id: &'static str,
    display_name: &'static str,
    family: &'static str,
    runtime_profile: &'static str,
    installer_kind: &'static str,
    arch: &'static str,
    launch_pipeline: &'static str,
    components: &'static [&'static str],
    detection_hints: &'static [&'static str],
    wrapper_policy: &'static str,
    repair_controls: &'static [&'static str],
    known_launchers: &'static [&'static str],
    limitations: &'static [&'static str],
}

fn profiles() -> Vec<LauncherProfile> {
    vec![
        LauncherProfile {
            id: "cef_chromium",
            display_name: "CEF / Chromium Launcher",
            family: "cef",
            runtime_profile: "launcher",
            installer_kind: "electron_or_webview",
            arch: "wow64",
            launch_pipeline: "wine_bare",
            components: &["gecko", "vcrun2019_x64", "vcrun2019_x86", "corefonts"],
            detection_hints: &["libcef.dll", "chrome_*.pak", "icudtl.dat", "vk_swiftshader.dll"],
            wrapper_policy: "preserve original exe as *_real.exe and launch through CEF software-GPU wrapper when Chromium payloads are detected",
            repair_controls: &["redeploy_cef_wrapper", "redeploy_cef_child_hook", "repair_vcrun", "repair_corefonts"],
            known_launchers: &["Minecraft Launcher", "Steam webhelper-like launcher payloads"],
            limitations: &["CEF child-process hook coverage is still under proof for Minecraft blank-surface cases"],
        },
        LauncherProfile {
            id: "webview2_storefront",
            display_name: "WebView2 Storefront Launcher",
            family: "webview2",
            runtime_profile: "webview",
            installer_kind: "webview",
            arch: "wow64",
            launch_pipeline: "wine_bare",
            components: &[
                "gecko",
                "webview2",
                "dotnet48",
                "vcrun2019_x64",
                "vcrun2019_x86",
                "directx_jun2010",
                "openal",
                "corefonts",
            ],
            detection_hints: &["WebView2Loader.dll", "msedgewebview2.exe", "MicrosoftEdgeWebView2RuntimeInstaller"],
            wrapper_policy: "install/bootstrap through bare Wine; keep storefront session state inside the installer bottle",
            repair_controls: &["repair_webview2", "repair_dotnet48", "repair_gecko", "repair_vcrun", "repair_corefonts"],
            known_launchers: &["EA App", "Ubisoft Connect", "Epic Games Launcher", "Rockstar Games Launcher", "Battle.net"],
            limitations: &["Per-machine MSI/service/elevation failures still require launcher-specific evidence reports"],
        },
        LauncherProfile {
            id: "dotnet_wpf",
            display_name: ".NET / WPF Launcher",
            family: "dotnet_wpf",
            runtime_profile: "dotnet",
            installer_kind: "wix_or_dotnet_exe",
            arch: "win64",
            launch_pipeline: "wine_bare",
            components: &["wine-mono", "gecko", "dotnet48", "vcrun2019_x64", "vcrun2019_x86", "corefonts"],
            detection_hints: &["mscoree.dll", ".NETFramework", "PresentationFramework", "WindowsBase"],
            wrapper_policy: "prefer .NET/WebView repair before game-specific graphics routing; avoid M9/M12 during bootstrap",
            repair_controls: &["repair_dotnet48", "repair_wine_mono", "repair_gecko", "repair_corefonts"],
            known_launchers: &[".NET bootstrapper launchers", "WPF patchers"],
            limitations: &["Native .NET custom actions can still require MSI/service diagnostics"],
        },
        LauncherProfile {
            id: "win32_dotnet_wpf",
            display_name: "32-bit .NET / WPF Launcher",
            family: "dotnet_wpf",
            runtime_profile: "win32_dotnet",
            installer_kind: "dotnet_exe",
            arch: "win32",
            launch_pipeline: "m9",
            components: &["wine-mono", "gecko", "dotnet48", "vcrun2019_x64", "vcrun2019_x86", "corefonts"],
            detection_hints: &["32-bit PE", "mscoree.dll", "PresentationFramework"],
            wrapper_policy: "use 32-bit .NET profile only when classifier proves a win32 launcher/bootstrapper",
            repair_controls: &["repair_dotnet48", "repair_wine_mono", "repair_corefonts"],
            known_launchers: &["Microsoft Store style 32-bit bootstrapper packages"],
            limitations: &["Do not treat WebView2 helper executables as user apps"],
        },
        LauncherProfile {
            id: "java_launcher",
            display_name: "Java Launcher",
            family: "java",
            runtime_profile: "java_launcher",
            installer_kind: "java",
            arch: "wow64",
            launch_pipeline: "wine_bare",
            components: &["vcrun2019_x64", "vcrun2019_x86", "corefonts"],
            detection_hints: &["jre", "javaw.exe", ".jar", "MinecraftInstaller.msi"],
            wrapper_policy: "keep launcher-managed Java runtime inside the bottle; do not substitute host Java",
            repair_controls: &["repair_vcrun", "repair_corefonts", "refresh_launcher_evidence"],
            known_launchers: &["Minecraft Launcher"],
            limitations: &["Minecraft CEF rendering remains a proof target after installer success"],
        },
        LauncherProfile {
            id: "electron_launcher",
            display_name: "Electron Launcher",
            family: "electron",
            runtime_profile: "launcher",
            installer_kind: "electron",
            arch: "wow64",
            launch_pipeline: "wine_bare",
            components: &["gecko", "vcrun2019_x64", "vcrun2019_x86", "corefonts"],
            detection_hints: &["app.asar", "electron.exe", "resources.pak", "chrome_100_percent.pak"],
            wrapper_policy: "use Chromium/CEF wrapper policy and preserve original executable before relaunch",
            repair_controls: &["redeploy_cef_wrapper", "redeploy_cef_child_hook", "repair_vcrun", "repair_corefonts"],
            known_launchers: &["Electron patchers", "custom game launchers"],
            limitations: &["Electron GPU-process flags may need app-specific additions after evidence capture"],
        },
        LauncherProfile {
            id: "gog_galaxy_storefront",
            display_name: "GOG Galaxy / Storefront Launcher",
            family: "gog",
            runtime_profile: "webview",
            installer_kind: "webview",
            arch: "wow64",
            launch_pipeline: "wine_bare",
            components: &["gecko", "webview2", "dotnet48", "vcrun2019_x64", "vcrun2019_x86", "corefonts"],
            detection_hints: &["GalaxyClient.exe", "GOG Galaxy", "GOG.com"],
            wrapper_policy: "prefer gogdl source adapter for game launch; Galaxy remains a launcher-profile proof target only",
            repair_controls: &["repair_webview2", "repair_dotnet48", "repair_gecko", "refresh_gog_doctor"],
            known_launchers: &["GOG Galaxy"],
            limitations: &["GOG game launch path is gogdl_wine with dedicated gog-prefix, not prefix-steam"],
        },
    ]
}

fn profile_to_json(profile: LauncherProfile) -> Value {
    json!({
        "schema": PROFILE_SCHEMA,
        "id": profile.id,
        "displayName": profile.display_name,
        "family": profile.family,
        "runtimeProfile": profile.runtime_profile,
        "installerKind": profile.installer_kind,
        "arch": profile.arch,
        "launchPipeline": profile.launch_pipeline,
        "components": profile.components,
        "detectionHints": profile.detection_hints,
        "wrapperPolicy": profile.wrapper_policy,
        "repairControls": profile.repair_controls,
        "knownLaunchers": profile.known_launchers,
        "limitations": profile.limitations,
    })
}

pub fn report() -> Value {
    let profiles: Vec<Value> = profiles().into_iter().map(profile_to_json).collect();
    json!({
        "ok": true,
        "schema": SCHEMA,
        "readOnly": true,
        "profiles": profiles,
        "invariants": [
            "Launcher bootstrap uses bare Wine unless a dedicated profile explicitly says otherwise.",
            "Storefront session state stays inside the source/bottle-owned prefix.",
            "WebView2 and CEF helper executables are runtime components, not Sharp Library apps.",
            "GOG game launch remains gogdl_wine on the dedicated gog-prefix, not prefix-steam."
        ],
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn report_includes_required_launcher_families() {
        let report = report();
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        assert_eq!(report.get("readOnly").and_then(|value| value.as_bool()), Some(true));
        let profiles = report.get("profiles").and_then(|value| value.as_array()).expect("profiles");
        for family in ["cef", "webview2", "dotnet_wpf", "java", "electron", "gog"] {
            assert!(
                profiles.iter().any(|profile| profile.get("family").and_then(|value| value.as_str()) == Some(family)),
                "missing launcher family {family}"
            );
        }
        let webview = profiles
            .iter()
            .find(|profile| profile.get("id").and_then(|value| value.as_str()) == Some("webview2_storefront"))
            .expect("webview storefront profile");
        let components = webview.get("components").and_then(|value| value.as_array()).expect("components");
        assert!(components.iter().any(|component| component.as_str() == Some("webview2")));
        assert!(components.iter().any(|component| component.as_str() == Some("dotnet48")));
    }
}
