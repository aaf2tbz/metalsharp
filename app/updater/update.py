#!/usr/bin/env python3
"""MetalSharp update handoff helper.

This helper is intentionally detached from Electron. The app starts it after a
DMG is downloaded, then this process owns the handoff:

1. show "Closing MetalSharp..." through the status file
2. wait for the Electron app to exit, forcing it only if needed
3. stop the backend after the app is gone
4. mount the update DMG
5. open MetalSharp.app directly from the mounted DMG

The launched app/backend version owns migration detection. A post-update marker
is still written as a fallback so the migration wizard opens even if the backend
is slow to answer during startup.
"""

from __future__ import annotations

import argparse
import json
import os
import plistlib
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import List, Optional, Tuple

try:
    os.setsid()
except OSError:
    pass

APP_BUNDLE_ID = "com.metalsharp.app"
BACKEND_PROCESS = "metalsharp-backend"
APP_PROCESS_NAMES = [
    "MetalSharp",
    "MetalSharp Helper",
    "MetalSharp Helper (GPU)",
    "MetalSharp Helper (Renderer)",
]


def write_status(
    status_file: Path,
    phase: str,
    percent: int,
    message: str,
    error: Optional[str] = None,
    new_version: Optional[str] = None,
) -> None:
    data = {
        "phase": phase,
        "percent": percent,
        "message": message,
        "error": error,
        "new_version": new_version,
        "timestamp": time.time(),
    }
    try:
        status_file.parent.mkdir(parents=True, exist_ok=True)
        status_file.write_text(json.dumps(data), encoding="utf-8")
    except Exception:
        pass


def run(cmd: List[str], timeout: int = 30) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def is_pid_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
        return True
    except (ProcessLookupError, OSError):
        return False
    except PermissionError:
        return True


def wait_for_pid_exit(pid: int, timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not is_pid_alive(pid):
            return True
        time.sleep(0.25)
    return not is_pid_alive(pid)


def signal_pid(pid: int, sig: signal.Signals) -> None:
    if pid <= 0:
        return
    try:
        os.kill(pid, sig)
    except (ProcessLookupError, PermissionError, OSError):
        pass


def pgrep_exact(name: str) -> List[int]:
    try:
        result = run(["pgrep", "-x", name], timeout=5)
    except Exception:
        return []
    if result.returncode != 0:
        return []
    pids: List[int] = []
    for raw in result.stdout.splitlines():
        try:
            pids.append(int(raw.strip()))
        except ValueError:
            pass
    return pids


def pkill_exact(name: str, sig: str = "TERM") -> None:
    try:
        run(["pkill", f"-{sig}", "-x", name], timeout=5)
    except Exception:
        pass


def wait_for_names_gone(names: List[str], timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not any(pgrep_exact(name) for name in names):
            return True
        time.sleep(0.3)
    return not any(pgrep_exact(name) for name in names)


def quit_app(app_pid: int, status_file: Path, target_version: str) -> None:
    write_status(status_file, "closing_app", 25, "Closing MetalSharp...", new_version=target_version)

    # Give the renderer poller a chance to display the closing message before
    # the external helper tells Electron to exit.
    time.sleep(2)

    try:
        run(["osascript", "-e", f'tell application id "{APP_BUNDLE_ID}" to quit'], timeout=8)
    except Exception:
        pass

    if app_pid > 0 and not wait_for_pid_exit(app_pid, 15):
        signal_pid(app_pid, signal.SIGTERM)
        wait_for_pid_exit(app_pid, 8)
    if app_pid > 0 and is_pid_alive(app_pid):
        signal_pid(app_pid, signal.SIGKILL)
        wait_for_pid_exit(app_pid, 3)

    for name in APP_PROCESS_NAMES:
        pkill_exact(name, "TERM")
    if not wait_for_names_gone(APP_PROCESS_NAMES, 8):
        for name in APP_PROCESS_NAMES:
            pkill_exact(name, "KILL")
        wait_for_names_gone(APP_PROCESS_NAMES, 3)


def stop_backend(backend_pid: int, status_file: Path, target_version: str) -> None:
    write_status(status_file, "killing_backend", 35, "Stopping backend...", new_version=target_version)
    if backend_pid > 0:
        signal_pid(backend_pid, signal.SIGTERM)
        wait_for_pid_exit(backend_pid, 8)
        if is_pid_alive(backend_pid):
            signal_pid(backend_pid, signal.SIGKILL)
            wait_for_pid_exit(backend_pid, 3)

    pkill_exact(BACKEND_PROCESS, "TERM")
    if not wait_for_names_gone([BACKEND_PROCESS], 8):
        pkill_exact(BACKEND_PROCESS, "KILL")
        wait_for_names_gone([BACKEND_PROCESS], 3)


def verify_dmg(dmg_path: Path) -> bool:
    if not dmg_path.is_file():
        return False
    try:
        result = run(["hdiutil", "verify", str(dmg_path)], timeout=120)
        return result.returncode == 0
    except Exception:
        return False


def mount_dmg(dmg_path: Path) -> Optional[str]:
    try:
        result = run(["hdiutil", "attach", "-plist", "-nobrowse", str(dmg_path)], timeout=120)
    except Exception:
        result = subprocess.CompletedProcess([], 1, "", "")

    if result.returncode == 0:
        mount_point = parse_attach_plist(result.stdout.encode("utf-8"))
        if mount_point:
            return mount_point

    # Rarely, attaching a downloaded image may require a system prompt.
    escaped = str(dmg_path).replace('"', '\\"')
    script = f'do shell script "hdiutil attach -nobrowse \\"{escaped}\\"" with administrator privileges'
    try:
        result = run(["osascript", "-e", script], timeout=180)
    except Exception:
        return None
    if result.returncode != 0:
        return None
    time.sleep(2)
    return find_mount_for_dmg(dmg_path)


def parse_attach_plist(raw: bytes) -> Optional[str]:
    try:
        data = plistlib.loads(raw)
    except Exception:
        return None
    for entity in data.get("system-entities", []):
        mount_point = entity.get("mount-point")
        if isinstance(mount_point, str) and mount_point.startswith("/Volumes/"):
            return mount_point
    return None


def find_mount_for_dmg(dmg_path: Path) -> Optional[str]:
    try:
        result = run(["hdiutil", "info", "-plist"], timeout=30)
        data = plistlib.loads(result.stdout.encode("utf-8"))
    except Exception:
        return None
    for image in data.get("images", []):
        image_path = image.get("image-path")
        if image_path and Path(image_path) != dmg_path:
            continue
        for entity in image.get("system-entities", []):
            mount_point = entity.get("mount-point")
            if isinstance(mount_point, str) and mount_point.startswith("/Volumes/"):
                return mount_point
    return None


def detach_mount(mount_point: Optional[str]) -> None:
    if not mount_point:
        return
    try:
        run(["hdiutil", "detach", mount_point, "-quiet"], timeout=30)
    except Exception:
        pass


def find_app_in_mount(mount_point: str) -> Optional[Path]:
    root = Path(mount_point)
    try:
        for entry in root.iterdir():
            if entry.suffix == ".app" and "metalsharp" in entry.name.lower():
                return entry
    except Exception:
        return None
    return None


def read_app_version(app_path: Path) -> str:
    plist_path = app_path / "Contents" / "Info.plist"
    try:
        with plist_path.open("rb") as handle:
            value = plistlib.load(handle).get("CFBundleShortVersionString", "")
            return str(value)
    except Exception:
        return ""


def clean_version(value: str) -> str:
    raw = value.strip().lstrip("v").split("-", 1)[0].split("+", 1)[0]
    parts = []
    for part in raw.split("."):
        digits = ""
        for char in part:
            if not char.isdigit():
                break
            digits += char
        if digits:
            parts.append(digits)
    return ".".join(parts)


def verify_app_bundle(app_path: Path, target_version: str) -> Tuple[bool, str]:
    required = [
        app_path / "Contents" / "Info.plist",
        app_path / "Contents" / "MacOS" / "MetalSharp",
        app_path / "Contents" / "Resources" / "runtime" / "metalsharp-backend",
    ]
    for path in required:
        if not path.is_file() or path.stat().st_size <= 0:
            return False, f"Update app is missing {path}"

    updater_dir = app_path / "Contents" / "Resources" / "scripts" / "tools" / "updater"
    if not (updater_dir / "update.py").is_file() and not (updater_dir / "update.sh").is_file():
        return False, "Update app is missing updater handoff tools"

    actual = clean_version(read_app_version(app_path))
    expected = clean_version(target_version)
    if not actual:
        return False, "Update app version could not be read"
    if actual != expected:
        return False, f"Update app version {actual} does not match target {expected}"
    return True, ""


def write_migration_marker(metalsharp_home: Path, target_version: str) -> None:
    try:
        metalsharp_home.mkdir(parents=True, exist_ok=True)
        marker = metalsharp_home / ".post-update-migration"
        marker.write_text(
            json.dumps({"needed": True, "target_version": target_version, "timestamp": time.time()}),
            encoding="utf-8",
        )
    except Exception:
        pass


def open_dmg_app(app_path: Path) -> bool:
    try:
        result = run(["open", "-n", str(app_path)], timeout=30)
        return result.returncode == 0
    except Exception:
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description="MetalSharp update handoff helper")
    parser.add_argument("--dmg", required=True)
    parser.add_argument("--backend-pid", required=True, type=int)
    parser.add_argument("--target-version", required=True)
    parser.add_argument("--status-file", default=os.path.expanduser("~/.metalsharp/update_install_status.json"))
    parser.add_argument("--metalsharp-home", default=os.path.expanduser("~/.metalsharp"))
    parser.add_argument("--app-pid", default=0, type=int)
    args = parser.parse_args()

    status_file = Path(args.status_file)
    metalsharp_home = Path(args.metalsharp_home)
    target_version = args.target_version
    dmg_path = Path(args.dmg).expanduser().resolve()
    mount_point: Optional[str] = None

    write_status(status_file, "starting", 0, "Starting update handoff...", new_version=target_version)

    quit_app(args.app_pid, status_file, target_version)
    write_status(status_file, "app_closed", 30, "MetalSharp closed.", new_version=target_version)

    stop_backend(args.backend_pid, status_file, target_version)
    write_status(status_file, "backend_stopped", 42, "Backend stopped.", new_version=target_version)

    write_status(status_file, "verifying_dmg", 50, "Verifying update disk image...", new_version=target_version)
    if not verify_dmg(dmg_path):
        write_status(
            status_file,
            "error",
            50,
            f"DMG failed verification: {dmg_path}",
            "dmg_verify_failed",
            target_version,
        )
        return 1

    write_status(status_file, "mounting", 62, "Mounting update disk image...", new_version=target_version)
    mount_point = mount_dmg(dmg_path)
    if not mount_point:
        write_status(status_file, "error", 62, "Failed to mount update DMG.", "mount_failed", target_version)
        return 1

    write_status(status_file, "mounted", 75, f"Mounted update at {mount_point}", new_version=target_version)
    app_source = find_app_in_mount(mount_point)
    if not app_source:
        detach_mount(mount_point)
        write_status(
            status_file,
            "error",
            75,
            "MetalSharp.app not found in update DMG.",
            "app_not_found",
            target_version,
        )
        return 1

    ok, error = verify_app_bundle(app_source, target_version)
    if not ok:
        detach_mount(mount_point)
        write_status(status_file, "error", 78, error, "app_bundle_invalid", target_version)
        return 1

    write_migration_marker(metalsharp_home, target_version)

    write_status(
        status_file,
        "launching_dmg_app",
        88,
        f"Opening MetalSharp v{clean_version(target_version)} from update DMG...",
        new_version=target_version,
    )
    if not open_dmg_app(app_source):
        detach_mount(mount_point)
        write_status(
            status_file,
            "error",
            88,
            "Failed to open MetalSharp from update DMG.",
            "open_failed",
            target_version,
        )
        return 1

    # Do not detach the DMG on success. The new app is running from this volume
    # and can eject it after migration/restart if appropriate.
    write_status(
        status_file,
        "complete",
        100,
        "Update handoff complete. Opening migration wizard...",
        new_version=target_version,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
