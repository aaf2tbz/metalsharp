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
