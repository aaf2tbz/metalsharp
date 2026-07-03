# D3DMetal Host ABI — Wine 11.5 source patches (Phase 2)

These patches port the CrossOver D3DMetal host ABI into MetalSharp's Wine 11.5,
under MetalSharp-owned names. CrossOver is only an oracle for behavior; no
`CX_`/`CrossOver`/`codeweavers` identifiers appear in compiled output.

## Gate

`tools/runtime/check-d3dmetal-shim-abi.py --strict` reports
`native_d3dmetal_host_abi_ready: true` against the rebuilt modules.

## What the patch adds

- **winemac.drv** — `WineMetalLayer` (CAMetalLayer subclass overriding
  `nextDrawable` → posts `CLIENT_SURFACE_PRESENTED`), the
  `macdrv_get/set_view_d3dmetal_client_surface` view helpers, the
  `d3dmetal_client_surfaces` CFArray on `macdrv_win_data`, the
  `macdrv_client_surface_presented` event handler, and the exported
  `macdrv_functions` ABI table (192-byte Wine/DXMT contract).
  Files: `metalsharp_d3dmetal.c`, `metalsharp_d3dmetal_objc.{h,m}` (new);
  `macdrv_cocoa.h`, `macdrv.h`, `event.c`, `Makefile.in` (edits).
- **win32u/d3dkmt.c** — `KMTQAITYPE_WDDM_2_7_CAPS` case gating on
  `MS_ACTIVE_GRAPHICS_BACKEND=d3dmetal_native` (HwSch caps).
- **ntdll/unix/loader.c** — `MS_D3DMETAL_SHARED_PATH` discovery of
  `libd3dshared.dylib` for non-native code region support.

All host-ABI changes are x86_64-only (`#if defined(__x86_64__)`); the D3DMetal
payload stack is Mach-O x86_64.

## Applying + building

This Wine build is out-of-band (the shipped `metalsharp-runtime.tar.zst` is a
prebuilt tree; CI only verifies it). To reproduce the host-ABI modules:

```sh
# 1. Extract wine-11.5 source (matches wine-11.5.tar.xz).
# 2. Apply the patch:
cd wine-11.5
patch -p1 < /path/to/repo/tools/wine-patches/d3dmetal-host-abi/0001-d3dmetal-host-abi.patch
autoreconf -fvi   # only if configure.ac/Makefiles changed

# 3. Out-of-tree build (x86_64-Unix, max-compat PE archs via clang):
mkdir build-x64 && cd build-x64
CC="clang -arch x86_64" CXX="clang++ -arch x86_64" \
  ../configure --host=x86_64-apple-darwin --enable-win64 \
  --enable-archs=i386,x86_64,arm,aarch64 --with-mingw=clang --with-coreaudio \
  --without-freetype --without-fontconfig --without-gnutls --without-gettext \
  --without-cups --without-dbus --without-ffmpeg --without-gphoto --without-gssapi \
  --without-gstreamer --without-krb5 --without-netapi --without-opencl --without-pcap \
  --without-pulse --without-sane --without-udev --without-unwind --without-usb \
  --without-v4l2 --without-x
make -j$(sysctl -n hw.ncpu)

# 4. Verify the host-ABI modules:
cp dlls/winemac.drv/winemac.so dlls/win32u/win32u.so dlls/ntdll/ntdll.so <test-runtime>/lib/wine/x86_64-unix/
../repo/tools/runtime/check-d3dmetal-shim-abi.py --runtime-root <test-runtime> --strict
```

Toolchain: Apple clang + Homebrew LLVM clang 22 (`--with-mingw=clang` builds all
four PE archs), mingw-w64, bison, flex, ninja. Build on an SSD (large).

## Status

Phase 2 host-ABI presence is met. The registry/display thunk callbacks
(`regqueryvalueexa_callback` etc.) are defined but remain 0 until the PE-side
winemac callbacks are wired in Phase 6 (launch); `KeUserDispatchCallback` fails
gracefully until then. The `macdrv_functions` table layout (the actual ABI
contract payloads look up) is correct.
