# Research: GPTK Wine internals and D3DMetal dxgi/d3d12 initialization failures

## Summary
D3DMetal `dxgi.dll` failures with status `c0000142` are best treated as native D3DMetal/`libd3dshared.dylib` load or initialization failures, not ordinary game DirectX errors: Microsoft defines `0xC0000142` as `STATUS_DLL_INIT_FAILED`. Evidence points to three common root causes: an incomplete or path-broken D3DMetal payload, mixing GPTK 1.1/Wine 7.7 with newer GPTK2/3/4 D3DMetal payloads, or running a payload on an unsupported macOS/Metal runtime.

## Findings
1. **`c0000142` means `dxgi.dll` initialization failed, which matches D3DMetal bridge load failures.** Microsoft documents `0xC0000142` as `STATUS_DLL_INIT_FAILED` / “DLL Initialization Failed.” In Wine/GPTK logs this can surface as “dxgi.dll failed to initialize” when the builtin PE `dxgi.dll` enters Apple’s native D3DMetal bridge and that bridge aborts or cannot load its dependencies. [Microsoft NTSTATUS values](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55)

2. **Known concrete failure mode: `libd3dshared.dylib` cannot `dlopen` `D3DMetal.framework`.** Multiple troubleshooting reports show the assertion `Failed to dlopen D3DMetal` from Apple’s `libd3dshared.dylib`; one issue specifically notes that `libd3dshared.dylib` uses `@rpath/D3DMetal.framework/D3DMetal` and lacks an LC_RPATH, so loading it from a directory where sibling `D3DMetal.framework` is not resolvable breaks initialization. This directly explains `dxgi` init failure and “no adapters” symptoms. [Whisky issue #124](https://github.com/Whisky-App/Whisky/issues/124), [Sikarugir issue #222](https://github.com/Sikarugir-App/Sikarugir/issues/222)

3. **The D3DMetal payload is a matched set, not a single DLL/framework.** Install/upgrade guidance consistently copies or replaces `D3DMetal.framework` together with `libd3dshared.dylib` and sibling libraries from the mounted GPTK “Evaluation environment” `External` folder. AppleInsider describes the GPTK image containing command-line tools plus a `lib` folder with `D3DMetal.framework` and `libd3dshared`; AppleGamingWiki/CrossOver community instructions replace both files under Wine/CrossOver `lib/external`. Mixing only `D3DMetal.framework` from GPTK4 with older `libd3dshared.dylib` or older PE `dxgi/d3d12` DLLs is therefore a likely incompatibility. [AppleInsider GPTK install guide](https://appleinsider.com/inside/macos-sonoma/tips/how-to-install-and-use-game-porting-toolkit-in-xcode), [AppleGamingWiki GPTK](https://www.applegamingwiki.com/wiki/Game_Porting_Toolkit), [CodeWeavers forum GPTK2 instructions](https://www.codeweavers.com/support/forums/general?t=27;msg=304647)

4. **GPTK 1.1 is an old Wine 7.7-era runtime; newer D3DMetal generations are paired with newer runtimes.** Apple’s Homebrew formula is still `GamePortingToolkit < Formula version "1.1"` and is x86_64-only. Apple Developer Forum snippets and community debugging identify the evaluation environment as Wine 7.7-based. By contrast, CodeWeavers shipped D3DMetal 2.0 together with Wine 9.17 in CrossOver Preview, indicating newer D3DMetal payloads expect newer Wine integration patches, loader layout, or thunk behavior than stock GPTK 1.1/Wine 7.7. [Apple Homebrew formula](https://raw.githubusercontent.com/apple/homebrew-apple/main/Formula/game-porting-toolkit.rb), [Apple Developer Forums GPTK tag](https://developer.apple.com/forums/tags/game-porting-toolkit), [CodeWeavers Wine 9.17 + D3DMetal 2.0](https://www.codeweavers.com/blog/mjohnson/2024/9/25/but-wait-theres-more-preview-wine-917-d3dmetal-20-now-available)

5. **GPTK4 appears even more OS/runtime-coupled.** Apple’s current GPTK page says the evaluation environment now supports Metal 4, and community GPTK4 reports say GPTK4 requires current beta macOS builds and that only the `External` payload should be copied as a matched set. This makes GPTK4 D3DMetal payloads especially risky in a Sonoma-era GPTK 1.1 Wine tree: the framework may load but fail during Metal feature/device initialization, producing “D3DMetal no adapters found” or `dxgi` init failure. [Apple GPTK page](https://developer.apple.com/games/game-porting-toolkit/), [macgaming GPTK4 discussion](https://www.reddit.com/r/macgaming/comments/1u0grv8/new_gtpk_4_beta_1_has_been_released/)

6. **“D3DMetal no adapters found” is usually downstream of D3DMetal failing to create/see a Metal device.** DXGI adapter enumeration is the normal API path for Direct3D devices; if D3DMetal fails to load, fails OS/Metal feature checks, or is invoked through an unsupported DirectX path, DXGI can report no usable adapters. GPTK/D3DMetal is documented by AppleGamingWiki as supporting DirectX 11 and 12, not DirectX 9, so D3D9 games should route to DXVK/DXMT/D3D9 rather than D3DMetal. [Microsoft DXGI adapter enumeration](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-enum), [AppleGamingWiki GPTK](https://www.applegamingwiki.com/wiki/Game_Porting_Toolkit)

7. **Homebrew caveats relevant to troubleshooting: architecture and install location matter.** The Apple formula is x86_64-only and installs a Wine tree with an `external` library directory; common install guides use `$(brew --prefix game-porting-toolkit)/lib/external` as the target for the D3DMetal payload. Running the wrong Homebrew architecture, copying payloads outside `lib/external`, or launching Wine without the expected loader paths can reproduce the `Failed to dlopen D3DMetal` class of failures. [Apple Homebrew formula](https://raw.githubusercontent.com/apple/homebrew-apple/main/Formula/game-porting-toolkit.rb), [Kiran GPTK guide](https://blog.lynkos.dev/posts/play-windows-games/)

## Likely root causes to check first
1. **Incomplete payload copy:** `D3DMetal.framework` present but matching `libd3dshared.dylib`/sibling dylibs missing, stale, or from another GPTK version.
2. **Wrong load path:** `libd3dshared.dylib` is not loaded from the directory containing sibling `D3DMetal.framework`, so `@rpath/D3DMetal.framework/D3DMetal` cannot resolve.
3. **Version skew:** GPTK 1.1/Wine 7.7 PE DLLs and loader are combined with GPTK2/3/4 native frameworks, or vice versa.
4. **Unsupported macOS/Metal runtime:** GPTK4/Metal 4-era framework used on Sonoma or older Sequoia builds without required Metal/OS support.
5. **Wrong rendering route:** D3D9/32-bit or Vulkan/DXVK workloads forced through D3DMetal, which is intended for D3D11/D3D12.
6. **Architecture/toolchain mismatch:** ARM Homebrew vs x86_64 formula/Rosetta Wine path confusion, leading to libraries being installed in one prefix and loaded from another.

## Practical diagnostics
- Log `DYLD_PRINT_LIBRARIES=1` / Wine loader traces around `dxgi`, `d3d12`, `libd3dshared`, and `D3DMetal.framework`.
- Verify all D3DMetal-native files come from the same GPTK image/release and live together in `.../wine/lib/external` or the wrapper’s equivalent external directory.
- Prefer replacing the whole `External` payload as a unit; do not cherry-pick only `D3DMetal.framework`.
- For GPTK1.1/Wine 7.7, test with the matching GPTK1.1 payload first; only test GPTK2/3/4 payloads in a runtime known to support them.
- If “no adapters” persists after load-path fixes, record macOS build, Metal feature set, chip model, and D3D feature level requested by the game.

## Sources
- Kept: Microsoft NTSTATUS Values (https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55) — authoritative meaning of `0xC0000142`.
- Kept: Apple Game Porting Toolkit page (https://developer.apple.com/games/game-porting-toolkit/) — official GPTK/Metal 4 statement.
- Kept: Apple Homebrew formula raw (https://raw.githubusercontent.com/apple/homebrew-apple/main/Formula/game-porting-toolkit.rb) — primary evidence for GPTK formula version/architecture/install tree.
- Kept: Whisky issue #124 (https://github.com/Whisky-App/Whisky/issues/124) — concrete `Failed to dlopen D3DMetal`/RPATH evidence.
- Kept: Sikarugir issue #222 (https://github.com/Sikarugir-App/Sikarugir/issues/222) — recent wrapper failure showing the same load-path problem.
- Kept: CodeWeavers Wine 9.17 + D3DMetal 2.0 blog (https://www.codeweavers.com/blog/mjohnson/2024/9/25/but-wait-theres-more-preview-wine-917-d3dmetal-20-now-available) — evidence that newer D3DMetal shipped paired with newer Wine.
- Kept: AppleGamingWiki GPTK (https://www.applegamingwiki.com/wiki/Game_Porting_Toolkit) — practical install paths and D3D11/D3D12 scope.
- Dropped: YouTube install/tutorial results — useful for users but not strong evidence.
- Dropped: generic Windows `dxgi.dll` repair pages — unrelated to Wine/D3DMetal initialization.
- Dropped: unrelated Reddit performance reports — anecdotal and not tied to initialization failure modes.

## Gaps
Apple does not publish D3DMetal ABI/loader compatibility details, so the exact binary incompatibility between GPTK 1.1 Wine and GPTK4 payloads cannot be proven from primary documentation. Next best step is an A/B matrix: GPTK1.1 Wine with GPTK1.1, GPTK2, GPTK3, and GPTK4 `External` payloads while capturing `DYLD_PRINT_LIBRARIES`, `otool -L`, `otool -l` RPATHs, and Wine `+loaddll,+seh,+dxgi,+d3d12` logs.
