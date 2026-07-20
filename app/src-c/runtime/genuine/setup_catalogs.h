#ifndef METALSHARP_SETUP_CATALOGS_H
#define METALSHARP_SETUP_CATALOGS_H

static const char SETUP_DEPENDENCIES_TEMPLATE[] =
    "{\"allInstalled\":@ALL@,\"dependencies\":[{\"desc\":\"Package manager — required to install other"
    " dependencies\",\"id\":\"homebrew\",\"installCmd\":\"/bin/bash -c \\\"$(curl -fsSL https://raw.githu"
    "busercontent.com/Homebrew/install/HEAD/install.sh)\\\"\",\"installed\":@HOMEBREW@,\"name\":\"Homeb"
    "rew\",\"required\":true},{\"desc\":\"Provides clang for building native shims (CSteamworks, gdip"
    "lus stub)\",\"id\":\"xcode_cli\",\"installCmd\":\"xcode-select --install\",\"installed\":@XCODE@,\"nam"
    "e\":\"Xcode Command Line Tools\",\"required\":true},{\"desc\":\"x86_64 translation layer — needed "
    "for 32-bit Windows games and x86 mono\",\"id\":\"rosetta\",\"installCmd\":\"softwareupdate --insta"
    "ll-rosetta --agree-to-license\",\"installed\":@ROSETTA@,\"name\":\"Rosetta 2\",\"required\":true},{"
    "\"desc\":\"From-source Wine 11.5 with DXMT Metal D3D11, gnutls TLS, MoltenVK. Runs Windows St"
    "eam and launches games with native Metal rendering.\",\"id\":\"metalsharp_wine\",\"installCmd\":\""
    "metalsharp-setup-wine\",\"installed\":@WINE@,\"name\":\"MetalSharp Wine\",\"required\":true},{\"desc"
    "\":\"Bottle-aware native host service ABI used by Wine shims and launch routes.\",\"id\":\"metal"
    "sharp_host_runtime\",\"installCmd\":\"metalsharp-setup-host-runtime\",\"installed\":@HOST@,\"name\""
    ":\"MetalSharp Host Runtime ABI\",\"required\":true},{\"desc\":\"Bundled D3D9/D3D10/D3D11-to-Metal"
    " runtime (0.56.0-m12-isolated-surface-v1) staged under runtime/wine/lib/dxmt.\",\"id\":\"dxmt_"
    "runtime\",\"installCmd\":\"metalsharp-setup-dxmt\",\"installed\":@DXMT@,\"name\":\"DXMT M9-M11 Runti"
    "me\",\"required\":true,\"status\":{\"current\":@DXMT@,\"filesReady\":@DXMT@,\"installedVersion\":null"
    ",\"manifestPath\":\"@METALSHARP_HOME@/runtime/wine/lib/dxmt/metalsharp-dxmt-runtime.json\",\"pa"
    "th\":\"@METALSHARP_HOME@/runtime/wine/lib/dxmt\",\"requiredVersion\":\"0.56.0-m12-isolated-surfa"
    "ce-v1\"}},{\"desc\":\"Isolated D3D12-to-Metal runtime staged under runtime/wine/lib/dxmt_m12 w"
    "ith its own DLLs and winemetal.so sidecars.\",\"id\":\"dxmt_m12_runtime\",\"installCmd\":\"metalsh"
    "arp-setup-dxmt-m12\",\"installed\":@M12@,\"name\":\"DXMT M12 Runtime\",\"path\":\"@METALSHARP_HOME@/"
    "runtime/wine/lib/dxmt_m12\",\"required\":true,\"status\":{\"current\":@M12@,\"filesReady\":@M12@,\"i"
    "nstalledVersion\":null,\"manifestPath\":\"@METALSHARP_HOME@/runtime/wine/lib/dxmt_m12/metalsha"
    "rp-dxmt-runtime.json\",\"path\":\"@METALSHARP_HOME@/runtime/wine/lib/dxmt_m12\",\"requiredVersio"
    "n\":\"0.56.0-m12-isolated-surface-v1\"}},{\"desc\":\"Required for Terraria and other arm64 FNA/X"
    "NA games\",\"id\":\"mono\",\"installCmd\":\"brew install mono\",\"installed\":@MONO@,\"name\":\"Mono Run"
    "time (arm64)\",\"required\":false},{\"desc\":\"Vulkan→Metal translation. Optional fallback graph"
    "ics backend.\",\"id\":\"moltenvk\",\"installCmd\":\"brew install molten-vk\",\"installed\":@MOLTENVK@"
    ",\"name\":\"MoltenVK\",\"required\":false},{\"desc\":\"Provides native macOS libraries (libsteam_ap"
    "i.dylib) for FNA games. Install Terraria for macOS to get the best compatibility.\",\"id\":\"s"
    "team\",\"installCmd\":\"https://store.steampowered.com/about/\",\"installed\":@STEAM@,\"name\":\"Ste"
    "am Client (macOS)\",\"required\":false}],\"ok\":true,\"platform\":\"macos\"}";

#endif
