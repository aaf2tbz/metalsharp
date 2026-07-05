#!/usr/bin/env python3
"""Run the D3DMetal native split loader/create probes against a local runtime.

This is a developer validation helper. It uses only locally staged GPTK/D3DMetal
payloads and does not download or redistribute Apple binaries.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable


LOAD_ONLY_C = r'''
#include <windows.h>
#include <stdio.h>
static int probe(const char *dll, const char *sym) {
    HMODULE h = LoadLibraryA(dll);
    char path[MAX_PATH];
    printf("LoadLibrary(%s)=%p err=%lu\n", dll, h, GetLastError()); fflush(stdout);
    if (!h) return 10;
    if (GetModuleFileNameA(h, path, sizeof(path))) printf("%s module=%p path=%s\n", dll, h, path);
    FARPROC p = GetProcAddress(h, sym);
    printf("GetProcAddress(%s)=%p err=%lu\n", sym, p, GetLastError()); fflush(stdout);
    return p ? 0 : 11;
}
int main(void) {
    int rc = 0;
    rc |= probe("dxgi.dll", "CreateDXGIFactory2");
    rc |= probe("d3d12.dll", "D3D12CreateDevice");
    rc |= probe("d3d11.dll", "D3D11CreateDevice");
    printf("LOAD_ONLY_RC=%d\n", rc); fflush(stdout);
    return rc ? 1 : 0;
}
'''

SINGLE_PROBE_C = r'''
#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <initguid.h>
#include <dxgi.h>
#include <d3d12.h>
#include <d3d11.h>

typedef HRESULT (WINAPI *PFN_CreateDXGIFactory2)(UINT, REFIID, void **);
typedef HRESULT (WINAPI *PFN_D3D12CreateDevice)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
typedef HRESULT (WINAPI *PFN_D3D11CreateDevice)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
static void print_module_path(const char *name, HMODULE h) { char path[MAX_PATH]; DWORD n = GetModuleFileNameA(h, path, sizeof(path)); printf("%s module=%p path=%s\n", name, h, n ? path : "<unknown>"); fflush(stdout); }
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "dxgi";
    printf("mode=%s\n", mode); fflush(stdout);
    if (!strcmp(mode, "dxgi")) {
        HMODULE h = LoadLibraryA("dxgi.dll"); printf("LoadLibrary(dxgi.dll)=%p err=%lu\n", h, GetLastError()); fflush(stdout); if (!h) return 10; print_module_path("dxgi.dll", h);
        PFN_CreateDXGIFactory2 p = (PFN_CreateDXGIFactory2)GetProcAddress(h, "CreateDXGIFactory2"); printf("GetProcAddress(CreateDXGIFactory2)=%p err=%lu\n", p, GetLastError()); fflush(stdout); if (!p) return 11;
        static const GUID iid = {0x50c83a1c,0xe072,0x4c48,{0x87,0xb0,0x36,0x30,0xfa,0x36,0xa6,0xd0}}; IUnknown *factory = NULL; HRESULT hr = p(0, &iid, (void **)&factory);
        printf("CreateDXGIFactory2 hr=0x%08lx factory=%p\n", (unsigned long)hr, factory); fflush(stdout); if (factory) IUnknown_Release(factory); return hr ? 12 : 0;
    }
    if (!strcmp(mode, "d3d12")) {
        HMODULE h = LoadLibraryA("d3d12.dll"); printf("LoadLibrary(d3d12.dll)=%p err=%lu\n", h, GetLastError()); fflush(stdout); if (!h) return 20; print_module_path("d3d12.dll", h);
        PFN_D3D12CreateDevice p = (PFN_D3D12CreateDevice)GetProcAddress(h, "D3D12CreateDevice"); printf("GetProcAddress(D3D12CreateDevice)=%p err=%lu\n", p, GetLastError()); fflush(stdout); if (!p) return 21;
        ID3D12Device *dev = NULL; HRESULT hr = p(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev); printf("D3D12CreateDevice hr=0x%08lx dev=%p\n", (unsigned long)hr, dev); fflush(stdout); if (dev) ID3D12Device_Release(dev); return hr ? 22 : 0;
    }
    if (!strcmp(mode, "d3d11")) {
        HMODULE h = LoadLibraryA("d3d11.dll"); printf("LoadLibrary(d3d11.dll)=%p err=%lu\n", h, GetLastError()); fflush(stdout); if (!h) return 30; print_module_path("d3d11.dll", h);
        PFN_D3D11CreateDevice p = (PFN_D3D11CreateDevice)GetProcAddress(h, "D3D11CreateDevice"); printf("GetProcAddress(D3D11CreateDevice)=%p err=%lu\n", p, GetLastError()); fflush(stdout); if (!p) return 31;
        ID3D11Device *dev = NULL; ID3D11DeviceContext *ctx = NULL; D3D_FEATURE_LEVEL lvl = 0; HRESULT hr = p(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &lvl, &ctx);
        printf("D3D11CreateDevice hr=0x%08lx dev=%p ctx=%p level=0x%x\n", (unsigned long)hr, dev, ctx, (unsigned)lvl); fflush(stdout); if (ctx) ID3D11DeviceContext_Release(ctx); if (dev) ID3D11Device_Release(dev); return hr ? 32 : 0;
    }
    return 2;
}
'''


def compile_probe(cc: str, source: str, out: Path) -> None:
    src = out.with_suffix(".c")
    src.write_text(source)
    proc = subprocess.run([cc, "-O2", "-o", str(out), str(src), "-ldxgi", "-ld3d12", "-ld3d11"], capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"compile {out.name} failed: {proc.stderr.strip()}")


def run_step(name: str, cmd: list[str], env: dict[str, str], log_dir: Path) -> dict:
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
    (log_dir / f"{name}.out").write_text(proc.stdout)
    (log_dir / f"{name}.err").write_text(proc.stderr)
    return {"name": name, "returncode": proc.returncode, "stdout": proc.stdout.strip().splitlines()[-8:]}


def main(argv: Iterable[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--runtime-root", type=Path, required=True)
    p.add_argument("--prefix", type=Path, required=True)
    p.add_argument("--payload-dir", type=Path)
    p.add_argument("--log-dir", type=Path)
    p.add_argument("--cc", default=shutil.which("x86_64-w64-mingw32-gcc") or "x86_64-w64-mingw32-gcc")
    args = p.parse_args(list(argv))

    runtime = args.runtime_root.expanduser().resolve()
    payload = (args.payload_dir or runtime / "lib" / "d3dmetal_native").expanduser().resolve()
    framework = payload / "external" / "D3DMetal.framework" / "Versions" / "A" / "D3DMetal"
    shared = payload / "external" / "libd3dshared.dylib"
    log_dir = (args.log_dir or Path(tempfile.mkdtemp(prefix="ms-d3dmetal-probes-"))).resolve()
    log_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="ms-d3dmetal-probes-build-") as td:
        td_path = Path(td)
        load_exe = td_path / "ms-gptk4-load-only-probe.exe"
        single_exe = td_path / "ms-gptk4-single-probe.exe"
        compile_probe(args.cc, LOAD_ONLY_C, load_exe)
        compile_probe(args.cc, SINGLE_PROBE_C, single_exe)

        env = os.environ.copy()
        env.update({
            "WINEPREFIX": str(args.prefix.expanduser().resolve()),
            "WINEDEBUG": "-all",
            "MS_GRAPHICS_BACKEND": "d3dmetal_native",
            "MS_ACTIVE_GRAPHICS_BACKEND": "d3dmetal_native",
            "MS_D3DMETAL_PAYLOAD_DIR": str(payload),
            "MS_D3DMETAL_SHARED_PATH": str(shared),
            "MS_D3DMETAL_FRAMEWORK_PATH": str(framework),
            "D3DMETAL_FRAMEWORK_PATH": str(framework),
            "WINEDLLPATH": f"{payload}:{runtime / 'lib' / 'wine'}",
            "WINEDLLOVERRIDES": "d3d10,d3d11,d3d12,dxgi,nvapi64,nvngx-on-metalfx=b,n;winedbg.exe=d",
        })
        wine = str(runtime / "bin" / "wine")
        wineboot = str(runtime / "bin" / "wineboot")
        steps = [
            run_step("wineboot", [wineboot, "-u"], env, log_dir),
            run_step("load-only", [wine, str(load_exe)], env, log_dir),
            run_step("dxgi-create", [wine, str(single_exe), "dxgi"], env, log_dir),
            run_step("d3d12-create", [wine, str(single_exe), "d3d12"], env, log_dir),
            run_step("d3d11-create", [wine, str(single_exe), "d3d11"], env, log_dir),
        ]

    summary = {"log_dir": str(log_dir), "steps": steps, "ok": all(s["returncode"] == 0 for s in steps)}
    (log_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
