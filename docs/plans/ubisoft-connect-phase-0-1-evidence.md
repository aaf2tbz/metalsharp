# Ubisoft Connect phases 0–1 evidence

This record covers the runtime preservation and incremental Unix ntdll work required before the Ubisoft Connect application integration.

## Phase 0: installed runtime preservation

MetalSharp runtime inspected:

```text
/Users/averyfelts/.metalsharp/runtime/wine
```

Active module:

```text
/Users/averyfelts/.metalsharp/runtime/wine/lib/wine/x86_64-unix/ntdll.so
```

Baseline facts:

| Property | Value |
| --- | --- |
| Wine version | `wine-11.5` |
| Architecture | thin Mach-O `x86_64` |
| SHA-256 | `2c65c0aec607ea64abcae733b30b730689a75ba9463b5b676b4dd7f10b792fb3` |
| Size | `741840` bytes |
| Install name | `@rpath/ntdll.so` |
| Rpath | `@loader_path/` |
| Minimum macOS | `26.0` |
| Build SDK | `26.4` |
| Exported symbols | `288` |
| Signature | ad-hoc |

The baseline module, metadata, exported symbols, complete runtime hash manifest, disposable runtime, and smoke-test logs are retained outside the runtime at:

```text
/Volumes/AverySSD/MetalSharp PR Work/runtime-safety/ubisoft-phase0-2c65c0aec607
```

The preserved module is:

```text
/Volumes/AverySSD/MetalSharp PR Work/runtime-safety/ubisoft-phase0-2c65c0aec607/original/ntdll.so
```

Its saved hash matches the installed baseline. A byte-level hash manifest confirmed that the disposable runtime initially matched all regular files in the installed runtime.

The disposable baseline passed:

- `metalsharp-wine --version`;
- `wineboot -u` in a new isolated prefix; and
- `cmd /c echo METALSHARP_DISPOSABLE_BASELINE_OK`.

The baseline write-copy probe returned `PAGE_WRITECOPY` (`0x8`) with the environment unset, with `WINE_SIMULATE_WRITECOPY=1`, and when named `UplayWebCore.exe`. This proves the original runtime did not implement either requested behavior.

Atomic installation and restoration are provided by `tools/wine/swap-metalsharp-ntdll.sh`. Its install/functional-test/restore cycle passed against the disposable runtime, including restoration to the exact baseline hash. Safety tests also confirmed that it rejects backup-directory symlinks resolving inside the runtime, while the build script rejects output paths resolving inside the installed runtime. The retained manual backup can also be restored atomically with:

```bash
cp -p '/Volumes/AverySSD/MetalSharp PR Work/runtime-safety/ubisoft-phase0-2c65c0aec607/original/ntdll.so' \
  "$HOME/.metalsharp/runtime/wine/lib/wine/x86_64-unix/ntdll.so.restore"
mv -f "$HOME/.metalsharp/runtime/wine/lib/wine/x86_64-unix/ntdll.so.restore" \
  "$HOME/.metalsharp/runtime/wine/lib/wine/x86_64-unix/ntdll.so"
```

## Phase 1: incremental x86_64 Unix ntdll

Wine source:

```text
Wine 11.5 commit 1dbc94083d1b508c3708c857b1715f9d379aa78a
```

The existing `/Volumes/AverySSD/Arm64WINE_Build/wine-11.5` configuration was not used as a binary donor because it produces an arm64 Unix ntdll. A separate clean worktree was configured for x86_64.

Only the following Wine sources were changed:

```text
dlls/ntdll/unix/loader.c
dlls/ntdll/unix/unix_private.h
dlls/ntdll/unix/virtual.c
```

The source adaptation is stored at:

```text
tools/wine/patches/ubisoft-writecopy/0001-wine-11.5-ubisoft-writecopy.patch
```

The incremental build invoked:

```bash
make dlls/ntdll/ntdll.so
```

Wine's generated-header tools were built as target prerequisites, but no complete Wine build or installation was performed. `tools/wine/build-ubisoft-ntdll.sh` subsequently completed an independent end-to-end build from a second clean worktree, and its output passed the baseline ABI verifier.

Final candidate:

| Property | Value |
| --- | --- |
| Architecture | thin Mach-O `x86_64` |
| SHA-256 | `223439a2c334b6532bb4235dd0ec91aadbd07923d7c6959c6c71585d9f6319bd` |
| Size | `669120` bytes |
| Install name | `@rpath/ntdll.so` |
| Rpath | `@loader_path/` |
| Minimum macOS | `26.0` |
| Build SDK | `26.5` |
| Exported symbols | same `288`-symbol set as baseline |
| Signature | ad-hoc, strict verification passed |

The candidate has the same dependency identities as the baseline:

```text
@rpath/ntdll.so
IOKit.framework
CoreFoundation.framework
CoreServices.framework
/usr/lib/libSystem.B.dylib
```

The SDK patch level is `26.5` rather than the unavailable baseline SDK `26.4`. The minimum OS, dependency identities, exports, install name, and rpath match; isolated-prefix execution validates the resulting module on the current runtime.

The candidate preserves the installed runtime's `MS_ROOT`/`mscompatdb.so` autoload path and corrects its non-`MS_ROOT` fallback to resolve from the architecture-specific ntdll directory. The existing mscompatdb module continues to report its pre-existing missing `KeServiceDescriptorTable`; that deprecated subsystem was not expanded as part of the Ubisoft fix. The baseline's unconsumed `MS_APPLEGPTK_LIBD3DSHARED_PATH` marker was not treated as a functional compatibility surface.

### Functional results

The disposable patched runtime passed:

- Wine 11.5 version check;
- new-prefix `wineboot -u`;
- Windows command execution; and
- strict code-signature verification.

The write-copy probe produced:

| Invocation | Initial protection | Returned old protection | Result |
| --- | --- | --- | --- |
| ordinary executable, env unset | `0x8` | `0x8` | unchanged baseline behavior |
| ordinary executable, `WINE_SIMULATE_WRITECOPY=1` | `0x8` | `0x4` | explicit simulation passed |
| executable named `UplayWebCore.exe`, env unset | `0x8` | `0x4` | automatic Ubisoft behavior passed |

The same startup and functional probes passed after the atomic swap into the installed MetalSharp runtime.

A complete post-swap runtime hash manifest differs from the Phase 0 manifest at exactly one path:

```text
lib/wine/x86_64-unix/ntdll.so
```

No PE ntdll, Wine loader, Wine server, graphics library, or other runtime file changed.

## Result

Phases 0 and 1 are complete:

- the original installed ntdll is preserved and immediately restorable;
- a disposable baseline was captured and tested;
- the Valve write-copy behavior was adapted to Wine 11.5;
- only x86_64 Unix ntdll was incrementally built;
- explicit and automatic Ubisoft write-copy behavior passed functional probes; and
- only the installed x86_64 Unix ntdll was atomically replaced.
