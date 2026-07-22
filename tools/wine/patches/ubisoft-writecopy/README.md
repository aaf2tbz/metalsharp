# Wine 11.5 Ubisoft write-copy patch

`0001-wine-11.5-ubisoft-writecopy.patch` is the MetalSharp Wine 11.5 adaptation of:

- Valve Wine commit [`334f7a78190a`](https://github.com/ValveSoftware/wine/commit/334f7a78190a0a0a4abede0d598b28a2743f468a), which adds `WINE_SIMULATE_WRITECOPY`.
- Valve Wine commit [`10a46db3c808`](https://github.com/ValveSoftware/wine/commit/10a46db3c80835d7a6038dbbac5163531dab6158), which automatically enables the behavior for `UplayWebCore.exe`.

Wine 11.5 no longer contains the Proton-side `VPROT_COPIED` support assumed by the historical patches, so the adaptation also restores that bit and its `get_win32_prot()` conversion.

The patch preserves the `MS_ROOT`-scoped `mscompatdb.so` autoload behavior present in MetalSharp's installed ntdll and fixes its direct-Wine fallback to search beside the architecture-specific Unix ntdll. The current `mscompatdb.so` still reports that `KeServiceDescriptorTable` is unavailable; this patch intentionally preserves the installed behavior rather than expanding the Ubisoft runtime change into the separate, deprecated mscompatdb implementation.

## Incremental build

Use a clean, unconfigured Wine 11.5 worktree whose path contains no whitespace:

```bash
tools/wine/build-ubisoft-ntdll.sh \
  --source /path/to/wine-11.5-clean \
  --output /path/to/candidate/ntdll.so
```

The script invokes only:

```bash
make dlls/ntdll/ntdll.so
```

Wine may build generated-header tools such as `widl` as prerequisites, but it does not build or install the complete Wine runtime.

The resulting module is:

- thin x86_64 Mach-O;
- ad-hoc signed;
- verified against the installed ntdll's exports, dependency identities, install name, rpath, and deployment target; and
- checked for the Ubisoft write-copy feature markers.

Run the functional probe against a disposable runtime before installation:

```bash
tools/wine/test-ubisoft-ntdll.sh /path/to/disposable/runtime/wine
```

Install and preserve an atomic rollback path with a new backup directory whose parent already exists:

```bash
tools/wine/swap-metalsharp-ntdll.sh install \
  --candidate /path/to/candidate/ntdll.so \
  --backup-dir /path/outside/the/runtime/ntdll-backup
```

Restore with the command printed by the installer, or:

```bash
tools/wine/swap-metalsharp-ntdll.sh restore \
  --backup-dir /path/outside/the/runtime/ntdll-backup
```
