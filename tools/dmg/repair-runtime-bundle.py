#!/usr/bin/env python3
import argparse
import os
import shutil
import stat
import subprocess
import tarfile
import tempfile
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ARCHIVE = PROJECT_ROOT / "app" / "bundles" / "metalsharp-runtime.tar.zst"
DEFAULT_HOST = PROJECT_ROOT / "app" / "native" / "host"
DEFAULT_BACKEND = PROJECT_ROOT / "app" / "src-rust" / "target" / "release" / "metalsharp-backend"
DEFAULT_METALSHARP_LIB = PROJECT_ROOT / "lib" / "metalsharp"
DEFAULT_MSCOMPATDB_BUILD_SCRIPT = PROJECT_ROOT / "tools" / "wine" / "build-mscompatdb-shim.sh"


def require_file(path: Path, description: str) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        raise FileNotFoundError(f"missing required {description}: {path}")


def require_host_runtime(host_dir: Path) -> None:
    require_file(host_dir / "manifest.json", "host runtime manifest")
    require_file(host_dir / "HostRuntimeABI.h", "host runtime ABI header")
    libraries = [
        host_dir / "libmetalsharp_host_runtime.dylib",
        host_dir / "libmetalsharp_host_runtime.so",
        host_dir / "metalsharp_host_runtime.dll",
    ]
    if not any(path.is_file() and path.stat().st_size > 0 for path in libraries):
        names = ", ".join(str(path) for path in libraries)
        raise FileNotFoundError(f"missing required non-empty host runtime library; checked {names}")


def require_metalsharp_lib(lib_dir: Path) -> None:
    require_file(
        lib_dir / "x86_64-windows" / "metalsharp_ntdll_hook.dll",
        "MetalSharp ntdll hook DLL",
    )


def build_mscompatdb_shim(script: Path, out_dir: Path) -> Path:
    require_file(script, "mscompatdb shim build script")
    subprocess.run([str(script), str(out_dir)], check=True)
    shim = out_dir / "mscompatdb.so"
    require_file(shim, "safe mscompatdb shim")
    return shim


def install_mscompatdb_shim(runtime_root: Path, shim: Path) -> None:
    unix_dir = runtime_root / "wine" / "lib" / "wine" / "x86_64-unix"
    unix_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(shim, unix_dir / "mscompatdb.so")
    require_file(unix_dir / "mscompatdb.so", "safe mscompatdb shim inside archive")


def patch_ntdll_mscompatdb_loader(runtime_root: Path) -> None:
    ntdll = runtime_root / "wine" / "lib" / "wine" / "x86_64-unix" / "ntdll.so"
    if not ntdll.is_file():
        return

    data = bytearray(ntdll.read_bytes())
    if b"mscompatdb.so" not in data:
        return

    offset = 0x25A80
    original = bytes.fromhex("488d05ef12")
    patched = bytes.fromhex("e91a010000")
    if len(data) < offset + len(patched):
        raise RuntimeError(f"ntdll too small for mscompatdb loader patch: {ntdll}")
    current = bytes(data[offset : offset + len(patched)])
    if current == patched:
        return
    if current != original:
        raise RuntimeError(
            f"unexpected ntdll mscompatdb loader bytes at 0x{offset:x}: {current.hex()} in {ntdll}"
        )

    data[offset : offset + len(patched)] = patched
    ntdll.write_bytes(data)
    codesign = shutil.which("codesign")
    if codesign:
        subprocess.run([codesign, "--force", "--sign", "-", str(ntdll)], check=True, stdout=subprocess.DEVNULL)


def copy_tree(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, symlinks=True)


def extract_archive(archive: Path, dest: Path) -> None:
    subprocess.run(["tar", "--use-compress-program=unzstd", "-xf", str(archive), "-C", str(dest)], check=True)


def add_tree_to_tar(tar: tarfile.TarFile, root: Path) -> None:
    paths = sorted([p for p in root.rglob("*") if p.is_dir()]) + sorted([p for p in root.rglob("*") if p.is_file() or p.is_symlink()])
    for path in paths:
        arcname = path.relative_to(root)
        info = tar.gettarinfo(str(path), arcname=str(arcname))
        info.uid = 0
        info.gid = 0
        info.uname = ""
        info.gname = ""
        info.mtime = 0
        if path.is_symlink():
            info.type = tarfile.SYMTYPE
            info.linkname = os.readlink(path)
            info.mode = 0o777
            tar.addfile(info)
        elif path.is_file():
            mode = stat.S_IMODE(path.stat().st_mode)
            info.mode = 0o755 if mode & stat.S_IXUSR else 0o644
            with path.open("rb") as fh:
                tar.addfile(info, fh)
        else:
            info.mode = 0o755
            tar.addfile(info)


def verify_tar_zst(archive: Path) -> None:
    result = subprocess.run(
        ["tar", "--use-compress-program=unzstd", "-tf", str(archive)],
        capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"archive verification failed for {archive}: {result.stderr.decode(errors='replace')}"
        )


def write_archive(source_root: Path, output: Path) -> None:
    with tempfile.NamedTemporaryFile(suffix=".tar", delete=False) as tmp:
        tar_path = Path(tmp.name)
    try:
        with tarfile.open(tar_path, "w") as tar:
            add_tree_to_tar(tar, source_root)
        for threads in ["0", "1"]:
            subprocess.run(
                ["zstd", "-q", "-19", f"-T{threads}", "-f", str(tar_path), "-o", str(output)],
                check=True,
            )
            output.chmod(0o644)
            try:
                verify_tar_zst(output)
                return
            except RuntimeError:
                if threads == "0":
                    print("MT zstd archive corrupt, retrying single-threaded...")
                    continue
                raise
    finally:
        tar_path.unlink(missing_ok=True)


def repair_runtime_bundle(archive: Path, host_dir: Path, backend: Path, metalsharp_lib: Path, mscompatdb_script: Path) -> None:
    require_file(archive, "runtime bundle archive")
    require_host_runtime(host_dir)
    require_file(backend, "runtime backend")
    require_metalsharp_lib(metalsharp_lib)

    with tempfile.TemporaryDirectory(prefix="metalsharp-runtime-repair-") as tmp_name:
        tmp = Path(tmp_name)
        extracted = tmp / "extracted"
        extracted.mkdir()
        extract_archive(archive, extracted)

        runtime_root = extracted / "runtime"
        if not runtime_root.is_dir():
            raise FileNotFoundError(f"runtime archive does not contain runtime/: {archive}")

        mscompatdb = build_mscompatdb_shim(mscompatdb_script, tmp / "mscompatdb")
        copy_tree(host_dir, runtime_root / "host")
        shutil.copy2(backend, runtime_root / "metalsharp-backend")
        copy_tree(metalsharp_lib, runtime_root / "wine" / "lib" / "metalsharp")
        install_mscompatdb_shim(runtime_root, mscompatdb)
        patch_ntdll_mscompatdb_loader(runtime_root)
        require_host_runtime(runtime_root / "host")
        require_file(runtime_root / "metalsharp-backend", "runtime backend inside archive")
        require_metalsharp_lib(runtime_root / "wine" / "lib" / "metalsharp")

        repaired = tmp / archive.name
        write_archive(extracted, repaired)
        shutil.move(str(repaired), str(archive))


def main() -> None:
    parser = argparse.ArgumentParser(description="Replace runtime bundle host ABI assets with the current build outputs.")
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument("--host-dir", type=Path, default=DEFAULT_HOST)
    parser.add_argument("--backend", type=Path, default=DEFAULT_BACKEND)
    parser.add_argument("--metalsharp-lib", type=Path, default=DEFAULT_METALSHARP_LIB)
    parser.add_argument("--mscompatdb-script", type=Path, default=DEFAULT_MSCOMPATDB_BUILD_SCRIPT)
    args = parser.parse_args()

    repair_runtime_bundle(args.archive, args.host_dir, args.backend, args.metalsharp_lib, args.mscompatdb_script)
    print(f"repaired runtime bundle: {args.archive}")


if __name__ == "__main__":
    main()
