# Vulkan Lane Sources

MetalSharp Wine 2.0 keeps DXVK/VKD3D/MoltenVK source acquisition separate from runtime installation.

Fetch pinned source checkouts with:

```bash
tools/runtime/fetch-vulkan-lane-sources.sh
```

Default tags:

- DXVK: `v3.0`
- VKD3D-Proton: `v3.0.1`
- MoltenVK: `v1.4.1`

Override pins when intentionally testing a different source revision:

```bash
DXVK_TAG=v3.0 VKD3D_PROTON_TAG=v3.0.1 MOLTENVK_TAG=v1.4.1 \
  tools/runtime/fetch-vulkan-lane-sources.sh --refresh
```

The script writes shallow source checkouts under:

```text
.cache/runtime-sources/
```

and records exact commits in:

```text
.cache/runtime-sources/vulkan-lane-sources.json
```

The fetch step does **not** build, install, launch Wine, mutate prefixes, or replace the current MetalSharp install. Runtime promotion remains gated by diagnostics and later explicit build/stage steps.

## Runtime payload archive contract

The split-bundle builder stages Vulkan-family runtime payloads only when these optional binary/runtime archives exist under `app/bundles/`:

```text
app/bundles/dxvk.tar.zst
app/bundles/vkd3d-proton.tar.zst
```

Validate those archives without installing or running Wine:

```bash
tools/runtime/check-vulkan-lane-payloads.sh
# or from app/: npm run wine20:vulkan-payloads
```

Expected DXVK runtime payload layout:

```text
dxvk/
  x86_64-windows/d3d9.dll
  i386-windows/d3d9.dll
  i386-windows/dxgi.dll
  x86_64-windows/d3d10core.dll
  x86_64-windows/d3d11.dll
  x86_64-windows/dxgi.dll
  x86_64-unix/libMoltenVK.dylib
```

Expected VKD3D-Proton runtime payload layout:

```text
vkd3d-proton/
  x86_64-windows/d3d12.dll
  x86_64-windows/dxgi.dll
  x86_64-unix/libvkd3d-shader.dylib
  x86_64-unix/libMoltenVK.dylib
```

These archives are runtime payloads, not source snapshots. Source checkouts under `.cache/runtime-sources/` prove provenance but do not by themselves make DXVK/VKD3D lanes ready.
