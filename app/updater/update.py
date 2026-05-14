#!/usr/bin/env python3
"""MetalSharp Updater — handles app update installation after download.

Spawned by the Electron app with detached=true so it survives the app quitting.
Performs: kill processes → mount DMG → install → relaunch → verify.

Communication: writes JSON status to ~/.metalsharp/update_install_status.json
The new MetalSharp instance reads this file on launch to show success/failure.

Usage:
    python3 update.py --dmg <path> --backend-pid <pid> --target-version <ver> \
                      [--status-file <path>] [--python <path>]
"""

import argparse
import json
import os
import signal
import subprocess
import sys
import time
import urllib.request

APP_PATH = "/Applications/MetalSharp.app"
BACKEND_PORT = 9274


def write_status(status_file, phase, percent, message, error=None, new_version=None):
    data = {
        "phase": phase,
        "percent": percent,
        "message": message,
        "error": error,
        "new_version": new_version,
        "timestamp": time.time(),
    }
    try:
        with open(status_file, "w") as f:
            json.dump(data, f)
    except Exception:
        pass


def read_status(status_file):
    try:
        with open(status_file, "r") as f:
            return json.load(f)
    except Exception:
        return None


def run(cmd, **kwargs):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=30, **kwargs)


def is_pid_alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except (ProcessLookupError, PermissionError, OSError):
        return False


def kill_pid(pid, timeout=10):
    try:
        os.kill(pid, signal.SIGTERM)
    except (ProcessLookupError, PermissionError, OSError):
        return True
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not is_pid_alive(pid):
            return True
        time.sleep(0.3)
    try:
        os.kill(pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError, OSError):
        pass
    time.sleep(0.5)
    return not is_pid_alive(pid)


def kill_by_patterns(patterns):
    for pat in patterns:
        try:
            subprocess.run(["pkill", "-x", pat], capture_output=True, timeout=5)
        except Exception:
            pass
    for pat in patterns:
        try:
            subprocess.run(["pkill", "-f", pat], capture_output=True, timeout=5)
        except Exception:
            pass
    time.sleep(1)


def verify_all_dead(patterns, timeout=15):
    deadline = time.time() + timeout
    while time.time() < deadline:
        all_dead = True
        for pat in patterns:
            r = subprocess.run(["pgrep", "-x", pat], capture_output=True, text=True)
            if r.returncode == 0 and r.stdout.strip():
                all_dead = False
                for pid_str in r.stdout.strip().split("\n"):
                    try:
                        os.kill(int(pid_str.strip()), signal.SIGKILL)
                    except Exception:
                        pass
        if all_dead:
            return True
        time.sleep(0.5)
    return False


def mount_dmg(dmg_path):
    r = run(["hdiutil", "attach", "-nobrowse", "-quiet", dmg_path])
    if r.returncode == 0:
        return _parse_mount(r.stdout)

    apple = (
        'do shell script "hdiutil attach -nobrowse -quiet '
        '\\"' + dmg_path + '\\""'
        " with administrator privileges"
    )
    r = run(["osascript", "-e", apple])
    if r.returncode == 0:
        time.sleep(2)
        info = run(["hdiutil", "info"])
        return _parse_mount_info(info.stdout, dmg_path)

    return None


def _parse_mount(output):
    for line in output.strip().split("\n"):
        parts = line.split()
        if parts and parts[-1].startswith("/Volumes/"):
            return parts[-1]
    return None


def _parse_mount_info(output, dmg_path):
    lines = output.split("\n")
    found = False
    for line in lines:
        if dmg_path in line:
            found = True
        if found and "/Volumes/" in line:
            idx = line.index("/Volumes/")
            rest = line[idx:].strip()
            return rest
    return None


def find_app_in_mount(mount_point):
    try:
        for entry in os.listdir(mount_point):
            if entry.endswith(".app") and "metalsharp" in entry.lower():
                return os.path.join(mount_point, entry)
    except Exception:
        pass
    return None


def admin_rm_rf(path):
    r = run(["rm", "-rf", path])
    if r.returncode == 0:
        return True
    apple = 'do shell script "rm -rf \\"' + path + '\\"" with administrator privileges'
    r = run(["osascript", "-e", apple])
    return r.returncode == 0


def admin_cp_r(src, dst):
    r = run(["cp", "-R", src, dst])
    if r.returncode == 0:
        return True
    apple = (
        'do shell script "cp -R \\"'
        + src
        + '\\" \\"'
        + dst
        + '\\"" with administrator privileges'
    )
    r = run(["osascript", "-e", apple])
    return r.returncode == 0


def wait_for_backend(timeout=45):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            req = urllib.request.Request(
                "http://127.0.0.1:" + str(BACKEND_PORT) + "/status"
            )
            with urllib.request.urlopen(req, timeout=3) as resp:
                data = json.loads(resp.read())
                return data.get("version"), data.get("pid")
        except Exception:
            pass
        time.sleep(1)
    return None, None


def main():
    parser = argparse.ArgumentParser(description="MetalSharp Updater")
    parser.add_argument("--dmg", required=True)
    parser.add_argument("--backend-pid", required=True, type=int)
    parser.add_argument("--target-version", required=True)
    parser.add_argument(
        "--status-file",
        default=os.path.expanduser("~/.metalsharp/update_install_status.json"),
    )
    parser.add_argument("--python", default=sys.executable)
    args = parser.parse_args()

    sf = args.status_file
    tv = args.target_version
    dmg = args.dmg
    bpid = args.backend_pid

    write_status(sf, "starting", 0, "Starting update...", new_version=tv)

    # ── 1. Kill backend ──────────────────────────────────────────────
    write_status(
        sf,
        "killing_backend",
        5,
        "Stopping backend (PID {})...".format(bpid),
        new_version=tv,
    )
    if not kill_pid(bpid, timeout=10):
        write_status(
            sf,
            "error",
            5,
            "Failed to stop backend",
            error="backend_kill_failed",
            new_version=tv,
        )
        sys.exit(1)
    run(["pkill", "-x", "metalsharp-backend"])
    time.sleep(0.5)
    write_status(sf, "killed_backend", 10, "Backend stopped.", new_version=tv)

    # ── 2. Kill steam/wine/webhelper ─────────────────────────────────
    write_status(
        sf, "killing_steam", 15, "Stopping Steam and Wine processes...", new_version=tv
    )
    wine_patterns = [
        "steam",
        "steam.exe",
        "steamwebhelper",
        "steamwebhelper.exe",
        "wine",
        "wine64",
        "wineserver",
    ]
    kill_by_patterns(wine_patterns)
    verify_all_dead(wine_patterns, timeout=15)
    write_status(
        sf, "killed_steam", 20, "Steam/Wine processes stopped.", new_version=tv
    )

    # ── 3. Verify DMG exists ─────────────────────────────────────────
    if not os.path.exists(dmg):
        write_status(
            sf,
            "error",
            20,
            "DMG not found: {}".format(dmg),
            error="dmg_not_found",
            new_version=tv,
        )
        sys.exit(1)

    # ── 4. Kill MetalSharp app ───────────────────────────────────────
    write_status(sf, "closing_app", 25, "Closing MetalSharp...", new_version=tv)
    for pat in [
        "MetalSharp",
        "MetalSharp Helper",
        "MetalSharp Helper (GPU)",
        "MetalSharp Helper (Renderer)",
    ]:
        try:
            subprocess.run(["pkill", "-x", pat], capture_output=True, timeout=5)
        except Exception:
            pass

    deadline = time.time() + 15
    while time.time() < deadline:
        r = subprocess.run(
            ["pgrep", "-x", "MetalSharp"], capture_output=True, text=True
        )
        if r.returncode != 0:
            break
        time.sleep(0.5)

    # Force kill anything remaining
    try:
        subprocess.run(["killall", "-9", "MetalSharp"], capture_output=True, timeout=5)
    except Exception:
        pass
    time.sleep(2)
    write_status(sf, "app_closed", 30, "MetalSharp closed.", new_version=tv)

    # ── 5. Mount DMG (may prompt for password via osascript) ─────────
    write_status(sf, "mounting", 35, "Mounting update disk image...", new_version=tv)
    mount_point = mount_dmg(dmg)
    if not mount_point:
        write_status(
            sf,
            "error",
            35,
            "Failed to mount DMG.",
            error="mount_failed",
            new_version=tv,
        )
        sys.exit(1)
    write_status(sf, "mounted", 45, "Mounted at {}".format(mount_point), new_version=tv)

    # ── 6. Install ───────────────────────────────────────────────────
    write_status(sf, "installing", 50, "Installing new version...", new_version=tv)

    app_source = find_app_in_mount(mount_point)
    if not app_source:
        run(["hdiutil", "detach", mount_point, "-quiet"])
        write_status(
            sf,
            "error",
            50,
            "MetalSharp.app not found in update.",
            error="app_not_found",
            new_version=tv,
        )
        sys.exit(1)

    if os.path.exists(APP_PATH):
        if not admin_rm_rf(APP_PATH):
            run(["hdiutil", "detach", mount_point, "-quiet"])
            write_status(
                sf,
                "error",
                55,
                "Failed to remove old app.",
                error="remove_failed",
                new_version=tv,
            )
            sys.exit(1)

    write_status(
        sf, "installing", 60, "Copying new version to Applications...", new_version=tv
    )
    if not admin_cp_r(app_source, APP_PATH):
        run(["hdiutil", "detach", mount_point, "-quiet"])
        write_status(
            sf,
            "error",
            65,
            "Failed to copy new version.",
            error="copy_failed",
            new_version=tv,
        )
        sys.exit(1)

    write_status(sf, "installed", 80, "New version installed.", new_version=tv)

    # ── 7. Unmount ───────────────────────────────────────────────────
    write_status(sf, "unmounting", 82, "Unmounting update disk...", new_version=tv)
    run(["hdiutil", "detach", mount_point, "-quiet"])

    # ── 8. Relaunch ──────────────────────────────────────────────────
    write_status(sf, "relaunching", 85, "Launching MetalSharp...", new_version=tv)
    time.sleep(1)
    run(["open", APP_PATH])

    # ── 9. Verify version ────────────────────────────────────────────
    write_status(sf, "verifying", 90, "Verifying installation...", new_version=tv)
    version, new_pid = wait_for_backend(timeout=45)

    if version and version != tv:
        write_status(
            sf,
            "deploying_backend",
            92,
            "Backend version mismatch, redeploying...",
            new_version=tv,
        )
        if new_pid:
            kill_pid(new_pid, timeout=10)
        time.sleep(1)
        run(["pkill", "-x", "metalsharp-backend"])
        time.sleep(1)
        run(["open", "-a", "MetalSharp"])
        time.sleep(3)
        version, new_pid = wait_for_backend(timeout=30)

    # ── 10. Report result ────────────────────────────────────────────
    if version == tv:
        write_status(
            sf,
            "complete",
            100,
            "Successfully updated to v{}!".format(tv),
            new_version=tv,
        )
    else:
        write_status(
            sf,
            "complete",
            100,
            "Update installed. Backend: v{} (expected v{})".format(version or "?", tv),
            new_version=tv,
        )


if __name__ == "__main__":
    main()
