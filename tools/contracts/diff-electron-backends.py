#!/usr/bin/env python3
"""Behaviorally compare the legacy and hand-written MetalSharp backends.

This is an oracle test, not a route-inventory or schema check. Cases run in
order against isolated homes. Responses and persistent filesystem state are
compared after normalizing only process-specific values.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import signal
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class Case:
    method: str
    path: str
    body: dict[str, Any] | None = None
    fixture: str | None = None


CASES = [
    Case("GET", "/status"),
    Case("GET", "/setup/state"),
    Case("GET", "/setup/device-name"),
    Case("GET", "/setup/dependencies"),
    Case("GET", "/setup/agility-versions"),
    Case("GET", "/setup/install-progress"),
    Case("GET", "/setup/installing"),
    Case("GET", "/steam/status"),
    Case("GET", "/steam/library"),
    Case("GET", "/steam/api-key"),
    Case("GET", "/steam/is-running"),
    Case("GET", "/steam/stop-targets"),
    Case("GET", "/steam/bridge-status"),
    Case("GET", "/steam/watch-steamapps"),
    Case("GET", "/scan"),
    Case("GET", "/config"),
    Case("GET", "/metalfx/state", fixture="metalfx-conf"),
    Case("POST", "/metalfx/toggle", {"enabled": True, "factor": 1.5}),
    Case("GET", "/metalfx/state"),
    Case("POST", "/metalfx/toggle", {"factor": 0.5}),
    Case("GET", "/metalfx/state"),
    Case("GET", "/metalfx/state", fixture="restart-backends"),
    Case("GET", "/wine-mono/status?prefix=steam"),
    Case("GET", "/sharp-library"),
    Case("GET", "/sharp-library/gog/status"),
    Case("GET", "/sharp-library/gog/games"),
    Case("GET", "/sharp-library/gog/games", fixture="gog-library"),
    Case("POST", "/sharp-library/gog/progress", {"productId": "1234"}, "gog-progress"),
    Case("POST", "/sharp-library/gog/uninstall", {"productId": "1234"}, "gog-uninstall"),
    Case("POST", "/sharp-library/gog/stop", {"productId": "1234"}, "gog-stop"),
    Case("POST", "/sharp-library/gog/import", {"productId": "1234"}, "gog-import"),
    Case("POST", "/sharp-library/gog/install", {"productId": "1234", "platform": "windows", "language": "en"}, "gog-install"),
    Case("POST", "/sharp-library/gog/progress", {"productId": "1234"}),
    Case("POST", "/sharp-library/gog/install", {"productId": "5678", "platform": "windows"}, "gog-install-success"),
    Case("POST", "/sharp-library/gog/progress", {"productId": "5678"}),
    Case("POST", "/sharp-library/gog/play", {"productId": "1234"}, "gog-play"),
    Case("POST", "/sharp-library/gog/initialize-prefix", {}, "fake-gog"),
    Case("POST", "/sharp-library/gog/auth-code", {"code": "parity-code"}, "fake-gog-auth-command"),
    Case("POST", "/sharp-library/gog/auth-code", {"code": "bad-code"}, "fake-gog-auth-failure"),
    Case("POST", "/sharp-library/gog/logout"),
    Case("POST", "/sharp-library/gog/remove-prefix"),
    Case("POST", "/wine-mono/install", {"prefix": "steam"}, "mono-installer"),
    Case("GET", "/wine-mono/status?prefix=gog", fixture="mono-installed"),
    Case("POST", "/wine-mono/install", {"prefix": "gog"}),
    Case("POST", "/wine-mono/reset", {"prefix": "gog"}),
    Case("POST", "/wine-mono/reset", {"prefix": "invalid"}),
    Case("GET", "/wine-mono/status?prefix=gog"),
    Case("GET", "/wine-mono/status?prefix=invalid"),
    Case("GET", "/bottles"),
    Case("GET", "/bottles/profiles"),
    Case("GET", "/bottles/redist-sources"),
    Case("GET", "/bottles/route-contracts"),
    Case("GET", "/bottles/compatibility-matrix"),
    Case("POST", "/bottles/set-windows-version", {"id": "parity-utility", "version": "win10"}, "bottle-action"),
    Case("POST", "/bottles/apply-font-subs", {"id": "parity-utility"}, "bottle-action"),
    Case("POST", "/bottles/seed-post-wineboot", {"id": "parity-utility"}, "bottle-action"),
    Case("GET", "/cache/size", fixture="cache-state"),
    Case("POST", "/cache/clear", {"type": "pipeline"}),
    Case("GET", "/cache/size"),
    Case("GET", "/logs"),
    Case("GET", "/logs/crash-reports", fixture="crash-reports"),
    Case("GET", "/logs/stream?after=0"),
    Case("GET", "/game/running"),
    Case("GET", "/game/dual-info?appid=4242", fixture="dual-game"),
    Case("GET", "/game/dual-info?appid=invalid"),
    Case("GET", "/update/dmg-path", fixture="fake-update-dmg"),
    Case("POST", "/update/cleanup"),
    Case("GET", "/update/migrate/check"),
    Case("GET", "/update/migrate/check", fixture="migrate-setup-incomplete"),
    Case("GET", "/update/migrate/check", fixture="migrate-schema-current"),
    Case("GET", "/update/migrate/check", fixture="migrate-prefix-repair"),
    Case("GET", "/update/migrate/check", fixture="migrate-setup-completed"),
    Case("GET", "/update/migrate/check", fixture="migrate-marker-newer"),
    Case("GET", "/update/migrate/progress"),
    Case("GET", "/update/migrate/report"),
    Case("GET", "/update/migrate/report", fixture="migration-report"),
    Case("GET", "/mtsp/pipelines"),
    Case("GET", "/mtsp/default-rules"),
    Case("GET", "/diagnostics/pipeline/dry-run?appid=0"),
    Case("GET", "/diagnostics/m12/dry-run?appid=0"),
    Case(
        "POST",
        "/setup/save",
        {"step": 2, "deviceName": 'Parity "Test" \\ Device', "completed": True},
    ),
    Case("GET", "/setup/state"),
    Case("POST", "/launch", {"exePath": "C:/Games/Parity.exe"}, "fake-wine"),
    Case("POST", "/kill", {"pid": 99999999}),
    Case("POST", "/cache/clear", {"type": "shader"}),
    Case("GET", "/cache/size"),
]

_DYNAMIC_KEY = re.compile(
    r"(^|_)(pid|port|timestamp|ts|started_at|updated_at|last_activity)$|(?:Pid|Port|Timestamp|Address)$|AtUnix$|_at$|(?:_at|_mtime)_unix$",
    re.I,
)
_TIMESTAMP_TEXT = re.compile(r"\[\d{4}-\d\d-\d\d[^\]]*\]")
_PIP_BUILD_ENV = re.compile(r"pip-build-env-[^/]+")
_WINE_INSTALLER_MSI = re.compile(r"(?<=/Installer/)[0-9a-fA-F]{4}(?=\.msi$)")
_WINE_TEMP_DLL = re.compile(r"^prefix-[^/]+/drive_c/windows/(?:system32|syswow64)/dll[0-9a-fA-F]+\.tmp$")
_MIGRATION_CACHE_META = re.compile(r"^cache/meta/")
_TEXT_SUFFIXES = {".json", ".toml", ".txt", ".log", ".conf", ".ini"}
_FS_IGNORES = {"metalsharp.db", "metalsharp.db-shm", "metalsharp.db-wal"}


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def json_shape(value: Any) -> Any:
    if isinstance(value, dict):
        return {key: json_shape(value[key]) for key in sorted(value)}
    if isinstance(value, list):
        return [json_shape(item) for item in value]
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, (int, float)):
        return "number"
    if isinstance(value, str):
        return "string"
    return type(value).__name__


def normalize(value: Any, home: Path, *, key: str = "") -> Any:
    if isinstance(value, dict):
        return {k: normalize(v, home, key=k) for k, v in sorted(value.items())}
    if isinstance(value, list):
        return [normalize(v, home, key=key) for v in value]
    if _DYNAMIC_KEY.search(key) and value is not None and isinstance(value, (int, float, str)):
        return "<dynamic-value>"
    if isinstance(value, str):
        value = value.replace(str(home), "<METALSHARP_HOME>")
        value = _TIMESTAMP_TEXT.sub("[<timestamp>]", value)
        value = re.sub(r"127\.0\.0\.1:\d+", "127.0.0.1:<port>", value)
        value = re.sub(r"\bpid \d+\b", "pid <dynamic-number>", value)
    return value


def normalize_response(value: Any, home: Path, case: Case) -> Any:
    value = normalize(value, home)
    if case.path == "/setup/device-name" and isinstance(value, dict):
        name = value.get("name")
        if isinstance(name, str) and re.fullmatch(r"[A-Za-z]+-[A-Za-z]+", name):
            value["name"] = "<generated-device-name>"
    if case.path == "/kernel-translation/handle/snapshot-all" and isinstance(value, dict):
        value["processes"] = "<live-process-snapshot>"
        value["totalProcesses"] = "<dynamic-number>"
    if case.path == "/steam/stop-targets" and isinstance(value, dict) and isinstance(value.get("excluded"), list):
        deduplicated = []
        seen_commands = set()
        for row in value["excluded"]:
            command = row.get("command") if isinstance(row, dict) else None
            if command in seen_commands:
                continue
            seen_commands.add(command)
            deduplicated.append(row)
        value["excluded"] = deduplicated
    return value


def prepare_case_fixture(home: Path, case: Case) -> None:
    if case.fixture == "gog-install-success":
        gogdl = home / "tools/gogdl"
        gogdl.parent.mkdir(parents=True, exist_ok=True)
        gogdl.write_text(
            "#!/bin/sh\n"
            "case \" $* \" in\n"
            "  *\" import \"*) printf '%s\\n' '{\"title\":\"Installed Parity\",\"tasks\":[{\"name\":\"Play\",\"path\":\"game.exe\",\"isPrimary\":true}]}' ;;\n"
            "  *) mkdir -p \"$METALSHARP_HOME/gog-games/5678/Installed Parity\"; printf '%s' '{}' > \"$METALSHARP_HOME/gog-games/5678/Installed Parity/goggame-5678.info\"; printf '%s\\n' 'Progress: 100 MiB' ;;\n"
            "esac\n"
        )
        gogdl.chmod(0o755)
        return
    if case.fixture == "gog-play":
        gogdl = home / "tools/gogdl"
        gogdl.parent.mkdir(parents=True, exist_ok=True)
        gogdl.write_text("#!/bin/sh\nprintf \"%s\\n\" \"$@\" > \"$METALSHARP_HOME/gog-play-argv.txt\"\n")
        gogdl.chmod(0o755)
        wine = home / "runtime/wine/bin/metalsharp-wine"
        wine.parent.mkdir(parents=True, exist_ok=True)
        wine.write_text("#!/bin/sh\nexit 0\n")
        wine.chmod(0o755)
        (home / "bottles/gog-prefix/prefix/drive_c").mkdir(parents=True, exist_ok=True)
        game_folder = home / "gog-games/1234/Parity Game"
        game_folder.mkdir(parents=True, exist_ok=True)
        directory = home / "gog"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "library.json").write_text(
            json.dumps(
                {
                    "games": [
                        {
                            "productId": "1234",
                            "title": "Parity GOG Game",
                            "platform": "windows",
                            "slug": None,
                            "imageUrl": None,
                            "iconUrl": None,
                            "installRoot": str(game_folder.parent),
                            "gameFolder": str(game_folder),
                            "primaryExe": "game.exe",
                            "primaryTaskName": "Play",
                            "installed": True,
                            "running": False,
                            "status": "installed",
                            "downloadSizeBytes": None,
                            "diskSizeBytes": None,
                            "lastInstallPid": None,
                            "lastLaunchPid": None,
                            "lastLogPath": None,
                            "lastError": None,
                        }
                    ],
                    "lastSyncAt": 123,
                }
            )
        )
        return
    if case.fixture == "gog-install":
        gogdl = home / "tools/gogdl"
        gogdl.parent.mkdir(parents=True, exist_ok=True)
        gogdl.write_text(
            "#!/bin/sh\n"
            'printf "%s\\n" "$@" > "$METALSHARP_HOME/gog-install-argv.txt"\n'
            "printf '%s\\n' 'Progress: 12.5 MiB'\n"
        )
        gogdl.chmod(0o755)
        return
    if case.fixture == "gog-import":
        folder = home / "gog-games/1234/Parity Game"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "goggame-1234.info").write_text("{}")
        gogdl = home / "tools/gogdl"
        gogdl.parent.mkdir(parents=True, exist_ok=True)
        gogdl.write_text(
            "#!/bin/sh\n"
            'printf "%s\\n" "$@" > "$METALSHARP_HOME/gog-import-argv.txt"\n'
            "printf '%s\\n' '{\"title\":\"Imported Parity\",\"tasks\":[{\"name\":\"Play\",\"path\":\"game.exe\",\"isPrimary\":true}]}'\n"
        )
        gogdl.chmod(0o755)
        return
    if case.fixture == "gog-stop":
        directory = home / "gog"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "library.json").write_text(
            json.dumps(
                {
                    "games": [
                        {
                            "productId": "1234",
                            "title": "Parity GOG Game",
                            "platform": "windows",
                            "slug": None,
                            "imageUrl": None,
                            "iconUrl": None,
                            "installRoot": None,
                            "gameFolder": None,
                            "primaryExe": None,
                            "primaryTaskName": None,
                            "installed": True,
                            "running": True,
                            "status": "running",
                            "downloadSizeBytes": None,
                            "diskSizeBytes": None,
                            "lastInstallPid": None,
                            "lastLaunchPid": None,
                            "lastLogPath": None,
                            "lastError": None,
                        }
                    ],
                    "lastSyncAt": 123,
                }
            )
        )
        return
    if case.fixture == "gog-uninstall":
        directory = home / "gog"
        game_folder = home / "gog-games/Parity GOG Game"
        game_folder.mkdir(parents=True, exist_ok=True)
        (game_folder / "goggame-1234.info").write_text("{}")
        (game_folder / "game.exe").write_bytes(b"MZ")
        (home / "gogdl/gog-support/1234").mkdir(parents=True, exist_ok=True)
        (home / "gogdl/gog-support/1234/state").write_text("support")
        (home / "gogdl/heroic_gogdl/manifests/1234").mkdir(parents=True, exist_ok=True)
        (home / "gogdl/heroic_gogdl/manifests/1234/state").write_text("manifest")
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "library.json").write_text(
            json.dumps(
                {
                    "games": [
                        {
                            "productId": "1234",
                            "title": "Parity GOG Game",
                            "platform": "windows",
                            "slug": None,
                            "imageUrl": None,
                            "iconUrl": None,
                            "installRoot": str(game_folder.parent),
                            "gameFolder": str(game_folder),
                            "primaryExe": "game.exe",
                            "primaryTaskName": "Play",
                            "installed": True,
                            "running": False,
                            "status": "installed",
                            "downloadSizeBytes": 10,
                            "diskSizeBytes": 20,
                            "lastInstallPid": None,
                            "lastLaunchPid": None,
                            "lastLogPath": None,
                            "lastError": None,
                        }
                    ],
                    "lastSyncAt": 123,
                }
            )
        )
        return
    if case.fixture == "gog-progress":
        directory = home / "gog"
        directory.mkdir(parents=True, exist_ok=True)
        log_path = directory / "install-1234.log"
        log_path.write_text("Progress: 42.5 MiB\ngogdl exited with Some(0)\n")
        (directory / "library.json").write_text(
            json.dumps(
                {
                    "games": [
                        {
                            "productId": "1234",
                            "title": "Parity GOG Game",
                            "platform": "windows",
                            "slug": None,
                            "imageUrl": None,
                            "iconUrl": None,
                            "installRoot": None,
                            "gameFolder": None,
                            "primaryExe": None,
                            "primaryTaskName": None,
                            "installed": True,
                            "running": False,
                            "status": "installed",
                            "downloadSizeBytes": None,
                            "diskSizeBytes": None,
                            "lastInstallPid": 99999999,
                            "lastLaunchPid": None,
                            "lastLogPath": str(log_path),
                            "lastError": None,
                        }
                    ],
                    "lastSyncAt": 123,
                }
            )
        )
        return
    if case.fixture == "gog-library":
        directory = home / "gog"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "library.json").write_text(
            json.dumps(
                {
                    "games": [
                        {
                            "productId": "1234",
                            "title": "Parity GOG Game",
                            "platform": "windows",
                            "slug": None,
                            "imageUrl": None,
                            "iconUrl": None,
                            "installRoot": None,
                            "gameFolder": None,
                            "primaryExe": None,
                            "primaryTaskName": None,
                            "installed": True,
                            "running": False,
                            "status": "installed",
                            "downloadSizeBytes": None,
                            "diskSizeBytes": None,
                            "lastInstallPid": None,
                            "lastLaunchPid": None,
                            "lastLogPath": None,
                            "lastError": None,
                        }
                    ],
                    "lastSyncAt": 123,
                }
            )
        )
        return
    if case.fixture == "bottle-action":
        bottle_dir = home / "bottles/parity-utility"
        prefix = bottle_dir / "prefix"
        (prefix / "drive_c").mkdir(parents=True, exist_ok=True)
        bottle_dir.mkdir(parents=True, exist_ok=True)
        (bottle_dir / "bottle.json").write_text(
            json.dumps(
                {
                    "id": "parity-utility",
                    "name": "Parity Utility",
                    "bottle_type": "utility",
                    "steam_app_id": None,
                    "prefix_path": str(prefix),
                    "arch": "wow64",
                    "runtime_profile": "plain",
                    "installed_components": [],
                    "source_installer_path": None,
                    "installer_kind": None,
                    "game_install_path": None,
                    "runtime_assets": [],
                    "installed_app_detections": [],
                    "health": "new",
                    "last_launch_log": None,
                    "last_launch_pid": None,
                    "last_launch_status": None,
                    "last_launch_finished_at": None,
                    "created_at": "1",
                    "updated_at": "1",
                }
            )
        )
        wine = home / "runtime/wine/bin/metalsharp-wine"
        wine.parent.mkdir(parents=True, exist_ok=True)
        wine.write_text(
            "#!/bin/sh\n"
            'printf "%s\\n" "$@" > "$METALSHARP_HOME/bottle-wine-argv.txt"\n'
            'printf "%s\\n" "$WINEPREFIX,$WINEDEBUG,$WINEDEBUGGER" > "$METALSHARP_HOME/bottle-wine-env.txt"\n'
        )
        wine.chmod(0o755)
        return
    if case.fixture == "restart-backends":
        return
    if case.fixture == "migrate-setup-incomplete":
        (home / "setup.json").write_text(
            json.dumps(
                {
                    "completed": False,
                    "runtime_migration_schema": 2,
                    "last_migrated_version": "0.50.0",
                }
            )
        )
        return
    if case.fixture == "migrate-schema-current":
        (home / "setup.json").write_text(
            json.dumps(
                {
                    "completed": False,
                    "runtime_migration_schema": 4,
                    "last_migrated_version": "0.56.1",
                }
            )
        )
        return
    if case.fixture == "migrate-prefix-repair":
        (home / "prefix-steam").mkdir(parents=True, exist_ok=True)
        return
    if case.fixture == "migrate-setup-completed":
        (home / "setup.json").write_text(
            json.dumps(
                {
                    "completed": True,
                    "runtime_migration_schema": 2,
                    "last_migrated_version": "0.50.0",
                }
            )
        )
        return
    if case.fixture == "migrate-marker-newer":
        (home / ".post-update-migration").write_text(
            json.dumps({"needed": True, "target_version": "9.0.0"})
        )
        return
    if case.fixture == "migration-report":
        report = home / "logs/migration-report-latest.json"
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "version": "0.56.1",
                    "generated_at_unix": 1_700_000_000,
                    "entries": [
                        {
                            "phase": "preserve",
                            "outcome": "preserved",
                            "category": "setup_preferences",
                            "path": "setup.json",
                            "reason": "fixture",
                        }
                    ],
                }
            )
        )
        return
    if case.fixture == "dual-game":
        mac_steamapps = home / "Library/Application Support/Steam/steamapps"
        wine_steamapps = home / ".metalsharp/prefix-steam/drive_c/Program Files (x86)/Steam/steamapps"
        for steamapps in (mac_steamapps, wine_steamapps):
            steamapps.mkdir(parents=True, exist_ok=True)
            (steamapps / "appmanifest_4242.acf").write_text(
                '\"AppState\"\n{\n\t\"appid\"\t\t\"4242\"\n\t\"installdir\"\t\t\"Dual Game\"\n}\n'
            )
        (mac_steamapps / "common/Dual Game/Dual Game.app/Contents/MacOS").mkdir(parents=True)
        wine_game = wine_steamapps / "common/Dual Game"
        wine_game.mkdir(parents=True)
        (wine_game / "DualGame.exe").write_bytes(b"MZ")
        return
    if case.fixture == "cache-state":
        files = [
            (home / "shader-cache/family/620/a.bin", b"abc"),
            (home / "shader-cache/root.bin", b"12345"),
            (home / "pipeline-cache/family/100/pipeline.bin", b"pipeline"),
        ]
        for path, content in files:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        for root in (home / "shader-cache", home / "pipeline-cache"):
            for path in sorted(root.rglob("*"), reverse=True):
                os.utime(path, (1_700_000_000, 1_700_000_000))
            os.utime(root, (1_700_000_000, 1_700_000_000))
        return
    if case.fixture == "crash-reports":
        files = [
            (home / "games/demo/logs/game_crash.dmp", b"game-crash", 1_700_000_001),
            (home / "games/620/logs/numeric_crash.dmp", b"numeric-game-crash", 1_700_000_002),
            (home / "bottles/custom/logs/engine_crash.log", b"bottle-crash", 1_700_000_003),
            (home / "bottles/steam_620/logs/steam_bottle_crash.log", b"steam-bottle", 1_700_000_004),
            (
                home / "prefix-steam/drive_c/Program Files (x86)/Steam/dumps/crash_demo.dmp",
                b"steam-dump",
                1_700_000_005,
            ),
            (
                home / "prefix-steam/drive_c/users/steamuser/AppData/Local/CrashDumps/system.mdmp",
                b"system-dump",
                1_700_000_006,
            ),
        ]
        for path, content, timestamp in files:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
            os.utime(path, (timestamp, timestamp))
        return
    if case.fixture == "mono-installer":
        (home / "prefix-steam/drive_c").mkdir(parents=True, exist_ok=True)
        cache = home / "cache/wine-mono/wine-mono-11.2.0-x86.msi"
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_bytes(b"m" * 1_000_001)
        wine = home / "runtime/wine/bin/wine"
        wine.parent.mkdir(parents=True, exist_ok=True)
        wine.write_text(
            "#!/bin/sh\n"
            'ROOT="${WINEPREFIX%/prefix-steam}"\n'
            'printf "%s\\n" "$@" > "$ROOT/mono-argv.txt"\n'
            'printf "%s\\n" "$WINEPREFIX" > "$ROOT/mono-prefix.txt"\n'
            "sleep 30\n"
        )
        wine.chmod(0o755)
        wineserver = home / "runtime/wine/bin/wineserver"
        wineserver.write_text("#!/bin/sh\nexit 0\n")
        wineserver.chmod(0o755)
        return
    if case.fixture == "mono-installed":
        marker = home / "bottles/gog-prefix/prefix/drive_c/windows/mono/wine-mono-11.2.0"
        marker.mkdir(parents=True, exist_ok=True)
        cache = home / "cache/wine-mono/wine-mono-11.2.0-x86.msi"
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_bytes(b"fixture-msi")
        return
    if case.fixture == "fake-gog-auth-failure":
        gogdl = home / "tools/gogdl-venv/bin/gogdl"
        gogdl.write_text("#!/bin/sh\nprintf 'fixture-out\\n'\nprintf 'fixture-error\\n' >&2\nexit 7\n")
        gogdl.chmod(0o755)
        return
    if case.fixture == "fake-gog-auth-command":
        gogdl = home / "tools/gogdl-venv/bin/gogdl"
        gogdl.parent.mkdir(parents=True, exist_ok=True)
        gogdl.write_text(
            "#!/bin/sh\n"
            "auth=\n"
            "while [ $# -gt 0 ]; do\n"
            '  if [ "$1" = "--auth-config-path" ]; then auth="$2"; shift 2; else shift; fi\n'
            "done\n"
            'mkdir -p "$(dirname "$auth")"\n'
            'printf \'{"access_token":"parity-token"}\\n\' > "$auth"\n'
            "printf '{}\\n'\n"
        )
        gogdl.chmod(0o755)
        return
    if case.fixture == "metalfx-conf":
        conf = home / "runtime/wine/etc/dxmt.conf"
        conf.parent.mkdir(parents=True, exist_ok=True)
        conf.write_text(
            "d3d11.preferredMaxFrameRate = 60\n"
            "# parity comment\n"
            "d3d11.metalSpatialUpscaleFactor = 2.0\n"
            "d3d11.maxFeatureLevel = 12_1\n"
        )
        return
    if case.fixture == "fake-update-dmg":
        update_dir = home / "cache/updates"
        update_dir.mkdir(parents=True, exist_ok=True)
        (update_dir / "MetalSharp-9.9.9-arm64.dmg").write_bytes(b"parity-dmg")
        (update_dir / "keep.txt").write_text("keep")
        return
    if case.fixture not in {"fake-wine", "fake-gog"}:
        return
    wine = home / "runtime/wine/bin/metalsharp-wine"
    wine.parent.mkdir(parents=True, exist_ok=True)
    if case.fixture == "fake-gog":
        gogdl = home / "tools/gogdl-venv/bin/gogdl"
        gogdl.parent.mkdir(parents=True, exist_ok=True)
        gogdl.write_text("#!/bin/sh\nexit 0\n")
        gogdl.chmod(0o755)
        wine.write_text(
            "#!/bin/sh\n"
            'printf "%s\\n" "$@" > "$METALSHARP_HOME/gog-wine-argv.txt"\n'
            'printf "%s\\n" "$WINEPREFIX" > "$METALSHARP_HOME/gog-wine-prefix.txt"\n'
            'printf "%s\\n" "$WINEMSYNC,$WINEDEBUG,$MS_FWD_COMPAT_GL_CTX" > "$METALSHARP_HOME/gog-wine-env.txt"\n'
            'mkdir -p "$WINEPREFIX/drive_c"\n'
        )
    else:
        wine.write_text(
            "#!/bin/sh\n"
            'if [ "$1" = wineboot ]; then\n'
            '  mkdir -p "$WINEPREFIX/drive_c/windows/system32"\n'
            "  exit 0\n"
            "fi\n"
            'printf "%s\\n" "$@" > "$METALSHARP_HOME/launch-argv.txt"\n'
            'printf "%s\\n" "$WINEPREFIX" > "$METALSHARP_HOME/launch-prefix.txt"\n'
        )
    wine.chmod(0o755)


def request(port: int, case: Case) -> tuple[int | str, Any]:
    payload = json.dumps(case.body or {}).encode() if case.method == "POST" else None
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}{case.path}",
        data=payload,
        method=case.method,
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            status, raw = response.status, response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as error:
        status, raw = error.code, error.read().decode("utf-8", "replace")
    except Exception as error:  # backend crash/timeout is part of observable behavior
        return "error", str(error)
    try:
        return status, json.loads(raw)
    except json.JSONDecodeError:
        return status, raw


def wait_ready(port: int, proc: subprocess.Popen[bytes]) -> None:
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"backend exited during startup with {proc.returncode}")
        status, body = request(port, Case("GET", "/status"))
        if status == 200 and isinstance(body, dict):
            return
        time.sleep(0.1)
    raise RuntimeError("backend did not become ready")


def start_backend(binary: Path, home: Path, port: int) -> subprocess.Popen[bytes]:
    home.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update({"HOME": str(home), "METALSHARP_HOME": str(home), "METALSHARP_PORT": str(port)})
    proc = subprocess.Popen(
        [str(binary)],
        env=env,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    wait_ready(port, proc)
    return proc


def stop_backend(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return
    os.killpg(proc.pid, signal.SIGTERM)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)


def filesystem_snapshot(home: Path) -> dict[str, Any]:
    result: dict[str, Any] = {}
    if not home.exists():
        return result
    home_aliases = tuple(dict.fromkeys((str(home), str(home.resolve()))))

    def normalize_path_text(value: str) -> str:
        normalized = value
        for alias in home_aliases:
            normalized = normalized.replace(alias, "<METALSHARP_HOME>")
            normalized = normalized.replace(alias.lstrip("/"), "<METALSHARP_HOME>")
            normalized = normalized.replace(alias.replace("/", "\\"), "<METALSHARP_HOME>")
            normalized = normalized.replace(alias.replace("/", "\\\\"), "<METALSHARP_HOME>")
        normalized = _PIP_BUILD_ENV.sub("pip-build-env-<temporary>", normalized)
        return _WINE_INSTALLER_MSI.sub("<temporary>", normalized)

    def normalize_file_bytes(data: bytes) -> bytes:
        normalized = data
        for alias in home_aliases:
            normalized = normalized.replace(alias.encode(), b"<METALSHARP_HOME>")
            normalized = normalized.replace(alias.replace("/", "\\").encode(), b"<METALSHARP_HOME>")
        return normalized

    for path in sorted(home.rglob("*")):
        rel = normalize_path_text(path.relative_to(home).as_posix())
        if (
            path.name in _FS_IGNORES
            or rel.startswith("logs/")
            or rel.startswith("Library/Caches/")
            or "/INetCache/Content.IE5/" in rel
            or _WINE_TEMP_DLL.match(rel)
            or _MIGRATION_CACHE_META.match(rel)
        ):
            continue
        if path.is_symlink():
            result[rel] = {"type": "symlink", "target": normalize_path_text(os.readlink(path))}
        elif path.is_dir():
            result[rel] = {"type": "dir"}
        elif path.is_file():
            data = path.read_bytes()
            normalized_data = normalize_file_bytes(data)
            item: dict[str, Any] = {
                "type": "file",
                "size": len(normalized_data),
                "sha256": hashlib.sha256(normalized_data).hexdigest(),
            }
            # Python bytecode/cache payloads and Git reflogs contain compilation or
            # wall-clock metadata even when two runs produce the same installed tree.
            # Preserve their exact path inventory, but do not treat volatile bytes as
            # backend behavior.
            volatile_generated = (
                rel.startswith("Library/Caches/")
                or rel.endswith(".pyc")
                or rel.endswith(".reg")
                or "/.git/" in rel
                or rel.endswith(".dist-info/RECORD")
                or (rel.startswith("tools/gogdl-venv/") and rel.endswith(".so"))
            )
            if volatile_generated:
                item = {"type": "file"}
            looks_text = path.suffix.lower() in _TEXT_SUFFIXES or data.startswith(b"#!") or b"\0" not in data[:8192]
            if not volatile_generated and looks_text and len(data) <= 1_000_000:
                text = normalize_path_text(data.decode("utf-8", "replace"))
                text = _TIMESTAMP_TEXT.sub("[<timestamp>]", text)
                if rel.endswith("metalsharp-post-wineboot-seeded") and text.strip().isdigit():
                    text = "<timestamp>"
                if rel.endswith("/.update-timestamp") and text.strip().isdigit():
                    text = "<timestamp>"
                if path.suffix.lower() == ".json":
                    try:
                        item["json"] = normalize(json.loads(text), home)
                        item.pop("size")
                    except json.JSONDecodeError:
                        item["text"] = text
                else:
                    item["text"] = text
                    item.pop("size")
                item.pop("sha256")
            result[rel] = item
    return result


def inventory_cases(contract: Path) -> list[Case]:
    rows = json.loads(contract.read_text())["route_inventory"]
    cases = []
    for row in rows:
        if isinstance(row, str):
            method, path = row.split(" ", 1)
        else:
            method, path = row["method"], row["path"]
        cases.append(Case(method, path, {} if method == "POST" else None))
    return cases


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("legacy", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--strict", action="store_true", help="fail on any behavioral mismatch")
    parser.add_argument("--schema-only", action="store_true", help="compare JSON types instead of values")
    parser.add_argument("--all-baselines", action="store_true", help="probe empty input on all frozen routes")
    parser.add_argument("--start-index", type=int, default=0)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--contract", type=Path, default=Path("contracts/electron-backend.v1.json"))
    args = parser.parse_args()

    for binary in (args.legacy, args.candidate):
        if not binary.is_file() or not os.access(binary, os.X_OK):
            parser.error(f"not an executable backend: {binary}")
    if args.start_index < 0 or (args.limit is not None and args.limit < 0):
        parser.error("case bounds must be non-negative")

    cases = inventory_cases(args.contract) if args.all_baselines else CASES
    cases = cases[args.start_index :]
    if args.limit is not None:
        cases = cases[: args.limit]

    def compare_case(case: Case, legacy_home: Path, candidate_home: Path, legacy_port: int, candidate_port: int) -> dict[str, Any]:
        prepare_case_fixture(legacy_home, case)
        prepare_case_fixture(candidate_home, case)
        # Launch both oracle calls together. Sequential requests give the
        # legacy backend an entire candidate-request head start for detached
        # workers and child processes, producing race-dependent filesystem
        # snapshots rather than equal-age behavior.
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
            legacy_future = executor.submit(request, legacy_port, case)
            candidate_future = executor.submit(request, candidate_port, case)
            legacy_status, legacy_body = legacy_future.result()
            candidate_status, candidate_body = candidate_future.result()
        if case.fixture in {"fake-wine", "fake-gog", "mono-installer", "bottle-action", "gog-install", "gog-install-success", "gog-play"}:
            # Both backends return immediately after the child handoff. Give the
            # deterministic fixture process time to record its inherited argv/env.
            if case.fixture in {"bottle-action", "gog-install", "gog-install-success", "gog-play"}:
                time.sleep(0.50)
            else:
                time.sleep(0.10 if case.fixture == "mono-installer" else 0.05)
        legacy_value = normalize_response(legacy_body, legacy_home, case)
        candidate_value = normalize_response(candidate_body, candidate_home, case)
        if args.schema_only:
            legacy_value, candidate_value = json_shape(legacy_value), json_shape(candidate_value)
        response_match = legacy_status == candidate_status and legacy_value == candidate_value
        legacy_fs, candidate_fs = filesystem_snapshot(legacy_home), filesystem_snapshot(candidate_home)
        fs_match = legacy_fs == candidate_fs
        row: dict[str, Any] = {
            "method": case.method,
            "path": case.path,
            "match": response_match and fs_match,
            "response_match": response_match,
            "filesystem_match": fs_match,
            "legacy_status": legacy_status,
            "candidate_status": candidate_status,
        }
        if not row["match"]:
            row.update({"legacy": legacy_value, "candidate": candidate_value})
            if not fs_match:
                row.update({"legacy_filesystem": legacy_fs, "candidate_filesystem": candidate_fs})
        return row

    with tempfile.TemporaryDirectory(prefix="metalsharp-parity-") as tmp:
        root = Path(tmp)
        rows: list[dict[str, Any]] = []
        if args.all_baselines:
            for index, case in enumerate(cases):
                case_root = root / f"case-{args.start_index + index:03d}"
                legacy_home, candidate_home = case_root / "legacy", case_root / "candidate"
                legacy_port, candidate_port = free_port(), free_port()
                legacy_proc = start_backend(args.legacy.resolve(), legacy_home, legacy_port)
                candidate_proc = start_backend(args.candidate.resolve(), candidate_home, candidate_port)
                try:
                    rows.append(compare_case(case, legacy_home, candidate_home, legacy_port, candidate_port))
                finally:
                    stop_backend(candidate_proc)
                    stop_backend(legacy_proc)
        else:
            legacy_home, candidate_home = root / "legacy", root / "candidate"
            legacy_port, candidate_port = free_port(), free_port()
            legacy_proc = start_backend(args.legacy.resolve(), legacy_home, legacy_port)
            candidate_proc = start_backend(args.candidate.resolve(), candidate_home, candidate_port)
            try:
                for case in cases:
                    if case.fixture == "restart-backends":
                        stop_backend(candidate_proc)
                        stop_backend(legacy_proc)
                        legacy_port, candidate_port = free_port(), free_port()
                        legacy_proc = start_backend(args.legacy.resolve(), legacy_home, legacy_port)
                        candidate_proc = start_backend(args.candidate.resolve(), candidate_home, candidate_port)
                    rows.append(compare_case(case, legacy_home, candidate_home, legacy_port, candidate_port))
            finally:
                stop_backend(candidate_proc)
                stop_backend(legacy_proc)

    mismatches = [row for row in rows if not row["match"]]
    report = {
        "mode": "schema-only" if args.schema_only else "behavioral",
        "scope": "all-empty-baselines" if args.all_baselines else "electron-critical",
        "cases": len(rows),
        "matches": len(rows) - len(mismatches),
        "mismatches": mismatches,
        "results": rows,
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    label = "schemas" if args.schema_only else "behaviors"
    print(f"backend parity: {report['matches']}/{report['cases']} route {label} match")
    for row in mismatches:
        print(
            f"  mismatch: {row['method']} {row['path']} "
            f"(response={row['response_match']}, filesystem={row['filesystem_match']})"
        )
    return 1 if args.strict and mismatches else 0


if __name__ == "__main__":
    raise SystemExit(main())
