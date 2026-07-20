#ifndef METALSHARP_DIAGNOSTICS_EMPTY_STATE_H
#define METALSHARP_DIAGNOSTICS_EMPTY_STATE_H

static const char __attribute__((unused)) DIAG_DIAGNOSTICS_BINDING_CONTRACT_VALIDATE_JSON[] =
    "{\"error\":\"invalid root signature manifest: invalid type: null, expected struct RootSignatureManifest"
    "\",\"ok\":false}";

static const char __attribute__((unused)) DIAG_DIAGNOSTICS_CACHE_DOCTOR_JSON[] =
    "{\"appid\":0,\"cache_family\":[\"m12\",\"dxmt-metal12\"],\"ok\":true,\"pipeline\":\"m12\",\"pipeline_cache\":{"
    "\"db_fi"
    "les\":[],\"exists\":false,\"newest_mtime_unix\":null,\"oldest_mtime_unix\":null,\"path\":\"@HOME@/pipeline-cac"
    "he/m12/0\",\"total_entries\":0},\"runtime_artifact_hash\":null,\"schema_version\":1,\"shader_cache\":{\"db_fil"
    "es\":[],\"exists\":false,\"newest_mtime_unix\":null,\"oldest_mtime_unix\":null,\"path\":\"@HOME@/shader-cache/"
    "m12/0\",\"total_entries\":0},\"stale_warning\":null}";

static const char __attribute__((unused)) DIAG_DIAGNOSTICS_COMMAND_REPLAY_VALIDATE_JSON[] =
    "{\"encoder_boundaries\":[],\"ok\":true,\"schema_version\":1,\"violations\":[],\"visibility_summary\":{\"read_to"
    "_write_transitions\":0,\"render_passes\":0,\"split_barriers\":0,\"total_transitions\":0,\"unfinished_split_b"
    "arriers\":0,\"write_to_read_transitions\":0}}";

static const char __attribute__((unused)) DIAG_DIAGNOSTICS_FNA_CLASSIFY_JSON[] =
    "{\"appid\":0,\"pinned\":false,\"rationale\":[\"no FNA/MonoGame/XNA assemblies detected; do not force this l"
    "ane\"],\"recommendation\":\"not_fna\",\"signals\":{\"base_flavor\":\"unknown\",\"evidence_files\":[],\"has_managed"
    "_dir\":false,\"indicates_native_mono\":false,\"indicates_x86_mono\":false,\"uses_csteamworks\":false,\"uses_"
    "faudio\":false,\"uses_fmod\":false,\"uses_openal\":false,\"uses_steamworks_net\":false,\"uses_xinput\":false}"
    "}";

static const char __attribute__((unused)) DIAG_DIAGNOSTICS_FNA_EXPLAIN_JSON[] =
    "{\"appid\":0,\"method_label\":\"xna_fna_x86\",\"mono_arch\":\"x86\",\"mono_config\":\"generic-fna-mono.config\","
    "\"p"
    "inned\":false,\"rationale\":[\"no FNA/MonoGame/XNA assemblies detected; do not force this lane\"],\"schema"
    "_version\":1,\"signals\":{\"base_flavor\":\"unknown\",\"evidence_files\":[],\"has_managed_dir\":false,\"indicate"
    "s_native_mono\":false,\"indicates_x86_mono\":false,\"uses_csteamworks\":false,\"uses_faudio\":false,\"uses_f"
    "mod\":false,\"uses_openal\":false,\"uses_steamworks_net\":false,\"uses_xinput\":false}}";

static const char __attribute__((unused)) DIAG_DIAGNOSTICS_FNA_SIGNALS_JSON[] =
    "{\"error\":\"gameDir is not a directory\",\"gameDir\":\"\",\"ok\":false}";

static const char __attribute__((unused)) DIAG_DIAGNOSTICS_LAUNCH_JSON[] =
    "{\"appid\":0,\"artifact_sources\":[{\"dest_filename\":null,\"filename\":\"d3d12.dll\",\"optional\":false,\"presen"
    "t\":false,\"sha256\":null,\"size_bytes\":null,\"source_path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-wind"
    "ows/d3d12.dll\",\"source_subpath\":\"lib/dxmt_m12/x86_64-windows\"},{\"dest_filename\":null,\"filename\":\"d3d"
    "11.dll\",\"optional\":false,\"present\":false,\"sha256\":null,\"size_bytes\":null,\"source_path\":\"@HOME@/runti"
    "me/wine/lib/dxmt_m12/x86_64-windows/d3d11.dll\",\"source_subpath\":\"lib/dxmt_m12/x86_64-windows\"},{\"des"
    "t_filename\":null,\"filename\":\"dxgi.dll\",\"optional\":false,\"present\":false,\"sha256\":null,\"size_bytes\":n"
    "ull,\"source_path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-windows/dxgi.dll\",\"source_subpath\":\"lib/d"
    "xmt_m12/x86_64-windows\"},{\"dest_filename\":null,\"filename\":\"dxgi_dxmt.dll\",\"optional\":false,\"present\""
    ":false,\"sha256\":null,\"size_bytes\":null,\"source_path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-window"
    "s/dxgi_dxmt.dll\",\"source_subpath\":\"lib/dxmt_m12/x86_64-windows\"},{\"dest_filename\":null,\"filename\":\"d"
    "3d10core.dll\",\"optional\":false,\"present\":false,\"sha256\":null,\"size_bytes\":null,\"source_path\":\"@HOME@"
    "/runtime/wine/lib/dxmt_m12/x86_64-windows/d3d10core.dll\",\"source_subpath\":\"lib/dxmt_m12/x86_64-windo"
    "ws\"},{\"dest_filename\":null,\"filename\":\"winemetal.dll\",\"optional\":false,\"present\":false,\"sha256\":null"
    ",\"size_bytes\":null,\"source_path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-windows/winemetal.dll\",\"so"
    "urce_subpath\":\"lib/dxmt_m12/x86_64-windows\"},{\"dest_filename\":null,\"filename\":\"nvapi64.dll\",\"optiona"
    "l\":true,\"present\":false,\"sha256\":null,\"size_bytes\":null,\"source_path\":\"@HOME@/runtime/wine/lib/dxmt_"
    "m12/x86_64-windows/nvapi64.dll\",\"source_subpath\":\"lib/dxmt_m12/x86_64-windows\"},{\"dest_filename\":nul"
    "l,\"filename\":\"nvngx.dll\",\"optional\":true,\"present\":false,\"sha256\":null,\"size_bytes\":null,\"source_pat"
    "h\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-windows/nvngx.dll\",\"source_subpath\":\"lib/dxmt_m12/x86_64"
    "-windows\"}],\"bundle_hash\":null,\"cache_directories\":[{\"entry_count\":0,\"exists\":false,\"path\":\"@HOME@/s"
    "hader-cache/m12/0\"},{\"entry_count\":0,\"exists\":false,\"path\":\"@HOME@/shader-cache/dxmt-metal12/0\"}],\"e"
    "rror\":\"required runtime artifacts are missing\",\"game_install_path\":null,\"generated_at_unix\":17845642"
    "94,\"metalsharp_version\":\"0.56.1\",\"missing_artifacts\":[{\"filename\":\"d3d12.dll\",\"source_path\":\"@HOME@/"
    "runtime/wine/lib/dxmt_m12/x86_64-windows/d3d12.dll\",\"source_subpath\":\"lib/dxmt_m12/x86_64-windows\"},"
    "{\"filename\":\"d3d11.dll\",\"source_path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-windows/d3d11.dll\",\"s"
    "ource_subpath\":\"lib/dxmt_m12/x86_64-windows\"},{\"filename\":\"dxgi.dll\",\"source_path\":\"@HOME@/runtime/w"
    "ine/lib/dxmt_m12/x86_64-windows/dxgi.dll\",\"source_subpath\":\"lib/dxmt_m12/x86_64-windows\"},{\"filename"
    "\":\"dxgi_dxmt.dll\",\"source_path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-windows/dxgi_dxmt.dll\",\"sou"
    "rce_subpath\":\"lib/dxmt_m12/x86_64-windows\"},{\"filename\":\"d3d10core.dll\",\"source_path\":\"@HOME@/runtim"
    "e/wine/lib/dxmt_m12/x86_64-windows/d3d10core.dll\",\"source_subpath\":\"lib/dxmt_m12/x86_64-windows\"},{\""
    "filename\":\"winemetal.dll\",\"source_path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-windows/winemetal.d"
    "ll\",\"source_subpath\":\"lib/dxmt_m12/x86_64-windows\"}],\"ok\":false,\"pipeline\":\"m12\",\"pipeline_name\":\"M1"
    "2\",\"prefix_exists\":false,\"prefix_path\":\"@HOME@/prefix-steam\",\"runtime_profile\":\"m12\",\"schema_version"
    "\":1,\"wine_binary_exists\":false,\"wine_binary_path\":\"@HOME@/runtime/wine/bin/wine\",\"wine_library_env\":"
    "{\"key\":\"DYLD_FALLBACK_LIBRARY_PATH\",\"value\":\"@HOME@/runtime/wine/lib:@HOME@/runtime/wine/lib/wine/x8"
    "6_64-unix\"}}";

static const char __attribute__((unused)) DIAG_DIAGNOSTICS_LAUNCH_TIMING_JSON[] =
    "{\"appid\":0,\"bottle_id\":\"steam_0\",\"error\":\"no launch timing recorded for this bottle yet\",\"ok\":false}";

static const char __attribute__((unused)) DIAG_DIAGNOSTICS_PSO_MANIFESTS_JSON[] =
    "{\"appid\":0,\"count\":0,\"manifests\":[],\"ok\":true,\"pipeline\":\"m12\"}";

static const char __attribute__((unused)) DIAG_DIAGNOSTICS_RUNTIME_ARTIFACTS_JSON[] =
    "{\"dxmt\":{\"all_present\":false,\"entries\":[{\"filename\":\"winemetal.so\",\"label\":\"dxmt\",\"path\":\"@HOME@/"
    "run"
    "time/wine/lib/dxmt/x86_64-unix/winemetal.so\",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir"
    "\":\"x86_64-unix\"},{\"filename\":\"d3d10core.dll\",\"label\":\"dxmt\",\"path\":\"@HOME@/runtime/wine/lib/dxmt/x86"
    "_64-windows/d3d10core.dll\",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-windows\""
    "},{\"filename\":\"d3d11.dll\",\"label\":\"dxmt\",\"path\":\"@HOME@/runtime/wine/lib/dxmt/x86_64-windows/d3d11.d"
    "ll\",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-windows\"},{\"filename\":\"d3d12.dl"
    "l\",\"label\":\"dxmt\",\"path\":\"@HOME@/runtime/wine/lib/dxmt/x86_64-windows/d3d12.dll\",\"present\":false,\"sh"
    "a256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-windows\"},{\"filename\":\"dxgi.dll\",\"label\":\"dxmt\","
    "\"path\""
    ":\"@HOME@/runtime/wine/lib/dxmt/x86_64-windows/dxgi.dll\",\"present\":false,\"sha256\":null,\"size_bytes\":n"
    "ull,\"subdir\":\"x86_64-windows\"},{\"filename\":\"dxgi_dxmt.dll\",\"label\":\"dxmt\",\"path\":\"@HOME@/runtime/"
    "win"
    "e/lib/dxmt/x86_64-windows/dxgi_dxmt.dll\",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x"
    "86_64-windows\"},{\"filename\":\"winemetal.dll\",\"label\":\"dxmt\",\"path\":\"@HOME@/runtime/wine/lib/dxmt/x86_"
    "64-windows/winemetal.dll\",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-windows\"}"
    ",{\"filename\":\"nvapi64.dll\",\"label\":\"dxmt\",\"path\":\"@HOME@/runtime/wine/lib/dxmt/x86_64-windows/nvapi6"
    "4.dll\",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-windows\"},{\"filename\":\"nvngx"
    ".dll\",\"label\":\"dxmt\",\"path\":\"@HOME@/runtime/wine/lib/dxmt/x86_64-windows/nvngx.dll\",\"present\":false,"
    "\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-windows\"}]},\"dxmt_m12\":{\"all_present\":false,\"entrie"
    "s\":[{\"filename\":\"winemetal.so\",\"label\":\"dxmt_m12\",\"path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-un"
    "ix/winemetal.so\",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-unix\"},{\"filename\""
    ":\"libc++.1.dylib\",\"label\":\"dxmt_m12\",\"path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-unix/libc++.1.d"
    "ylib\",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-unix\"},{\"filename\":\"libc++abi"
    ".1.dylib\",\"label\":\"dxmt_m12\",\"path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-unix/libc++abi.1.dylib\""
    ",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-unix\"},{\"filename\":\"libunwind.1.dy"
    "lib\",\"label\":\"dxmt_m12\",\"path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-unix/libunwind.1.dylib\",\"pre"
    "sent\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-unix\"},{\"filename\":\"d3d10core.dll\",\"lab"
    "el\":\"dxmt_m12\",\"path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-windows/d3d10core.dll\",\"present\":fals"
    "e,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-windows\"},{\"filename\":\"d3d11.dll\",\"label\":\"dxmt_"
    "m"
    "12\",\"path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-windows/d3d11.dll\",\"present\":false,\"sha256\":null"
    ",\"size_bytes\":null,\"subdir\":\"x86_64-windows\"},{\"filename\":\"d3d12.dll\",\"label\":\"dxmt_m12\",\"path\":"
    "\"@HO"
    "ME@/runtime/wine/lib/dxmt_m12/x86_64-windows/d3d12.dll\",\"present\":false,\"sha256\":null,\"size_bytes\":n"
    "ull,\"subdir\":\"x86_64-windows\"},{\"filename\":\"dxgi.dll\",\"label\":\"dxmt_m12\",\"path\":\"@HOME@/runtime/"
    "wine"
    "/lib/dxmt_m12/x86_64-windows/dxgi.dll\",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86"
    "_64-windows\"},{\"filename\":\"dxgi_dxmt.dll\",\"label\":\"dxmt_m12\",\"path\":\"@HOME@/runtime/wine/lib/dxmt_m1"
    "2/x86_64-windows/dxgi_dxmt.dll\",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-win"
    "dows\"},{\"filename\":\"winemetal.dll\",\"label\":\"dxmt_m12\",\"path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_6"
    "4-windows/winemetal.dll\",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-windows\"},"
    "{\"filename\":\"nvapi64.dll\",\"label\":\"dxmt_m12\",\"path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-windows"
    "/nvapi64.dll\",\"present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-windows\"},{\"filename\""
    ":\"nvngx.dll\",\"label\":\"dxmt_m12\",\"path\":\"@HOME@/runtime/wine/lib/dxmt_m12/x86_64-windows/nvngx.dll\",\""
    "present\":false,\"sha256\":null,\"size_bytes\":null,\"subdir\":\"x86_64-windows\"}]},\"ok\":false,\"schema_versi"
    "on\":1}";

static const char __attribute__((unused)) DIAG_DIAGNOSTICS_WINEBOOT_STATE_JSON[] =
    "{\"appid\":0,\"is_prefix_updating\":false,\"is_verifying\":false,\"ok\":true,\"prefix_path\":\"@HOME@/prefix-st"
    "eam\",\"wineboot_state\":\"prefix_missing\"}";

#endif
