# Runtime Manifest

MetalSharp exposes a read-only runtime manifest report:

```http
GET /runtime/manifest
```

This is the Phase 2 foundation for MetalSharp Wine 2.0. It reports the expected runtime manifest, any persisted manifest, and validation status for the installed Wine/DXMT runtime surfaces.

## Scope

The manifest report covers:

- MetalSharp backend version;
- Wine version when `metalsharp-wine --version` is available;
- runtime info helper path and presence;
- runtime root;
- Wine root;
- host architecture/translation note;
- Windows support model (`win32`, `win64`, `wow64`);
- canonical M12 runtime surface: `dxmt_m12`;
- known runtime surfaces and installed paths;
- required artifact report for `dxmt` and `dxmt_m12`;
- Vulkan-family artifact reports for `dxvk` and `vkd3d` surfaces, so missing payloads are visible by filename before fallback lanes are used.

## Canonical M12 Surface

The installed M12 surface must be reported as:

```text
dxmt_m12
```

Installed path:

```text
~/.metalsharp/runtime/wine/lib/dxmt_m12/
```

The release graphics bundle may still contain:

```text
Graphics/dll/dxmt-m12/
```

That is bundle/archive terminology. Runtime manifest and runtime doctors use `dxmt_m12`.

## Validation Checks

The read-only report validates:

- manifest schema;
- Wine binary presence;
- canonical M12 surface name;
- canonical installed `dxmt_m12` path;
- required M12 sidecars and PE DLLs via the existing installer artifact checks.

Vulkan-family surfaces are reported under the legacy-compatible `artifacts.planned` bucket. The bucket name is schema history; lane availability is determined by `/runtime/contracts` and `/runtime/diagnostics`:

```text
artifacts.planned.dxvk.d9.entries[]
artifacts.planned.dxvk.d11.entries[]
artifacts.planned.vkd3d.d12.entries[]
```

Each entry includes `filename`, `subdir`, `path`, `present`, `sha256`, and `size_bytes`, matching the required DXMT/M12 artifact-report shape.

## Runtime Info Helper

When the backend writes the persisted manifest it also installs a filesystem-only helper:

```text
~/.metalsharp/runtime/wine/bin/metalsharp-runtime-info
```

The helper is the command-line equivalent of `metalsharp-wine --metalsharp-runtime-info` without requiring the Wine binary to support that flag. It prints the persisted `metalsharp-runtime-manifest.json` with `/bin/cat`, supports `--json`, `--path`, and `--help`, and intentionally does not invoke Wine or launch any Windows process.

## Writer Helper

The backend includes an atomic writer helper for installer integration:

```text
write_expected_runtime_manifest_for(home)
```

The endpoint itself is read-only. Installer/migration work calls the writer after runtime installation or repair completes.
