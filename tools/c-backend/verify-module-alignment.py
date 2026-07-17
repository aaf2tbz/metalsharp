#!/usr/bin/env python3
"""Verify the repository and shipped backend are C-only and module-complete."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
C_ROOT = ROOT / "app/src-c/runtime/c"
TEST_C_ROOT = ROOT / "app/src-c/tests/c"
MODULES = ROOT / "app/src-c/manifests/backend-modules.txt"
MAINTAINED = ROOT / "app/src-c/manifests/maintained-sources.txt"
MAINTAINED_TESTS = ROOT / "app/src-c/manifests/maintained-test-sources.txt"


def module_parts(module: str) -> list[str]:
    parts = module.split("/")
    if parts[-1] == "main":
        return []
    if parts[-1] == "mod":
        parts.pop()
    return parts


def module_pattern(parts: list[str]) -> re.Pattern[str]:
    expression = r"metalsharp_backend"
    for part in parts:
        expression += rf".*\d+{re.escape(part)}"
    return re.compile(expression)


def main() -> int:
    rust_files = sorted(ROOT.rglob("*.rs"))
    c_files = sorted(C_ROOT.glob("*.c"))
    if rust_files:
        print("Rust sources are forbidden; backend implementation must remain C-only:", file=sys.stderr)
        for path in rust_files:
            print(f"  {path.relative_to(ROOT)}", file=sys.stderr)
        return 1
    if not c_files:
        print("committed C runtime units are missing", file=sys.stderr)
        return 1

    modules = [line.strip() for line in MODULES.read_text().splitlines() if line.strip() and not line.startswith("#")]

    c_lines: list[str] = []
    for path in c_files:
        c_lines.extend(path.read_text(errors="ignore").splitlines())

    missing = []
    for module in modules:
        pattern = module_pattern(module_parts(module))
        if not any(pattern.search(line) for line in c_lines):
            missing.append(module)

    if missing:
        print("Backend modules missing compiled C implementation symbols:", file=sys.stderr)
        for path in missing:
            print(f"  {path}", file=sys.stderr)
        return 1

    makefile = (ROOT / "app/src-c/Makefile").read_text()
    generated = "\n".join(c_lines)
    forbidden_encoded_paths = {
        "src-rust": "0x73, 0x72, 0x63, 0x2d, 0x72, 0x75, 0x73, 0x74",
        ".cargo/bin": "0x2e, 0x63, 0x61, 0x72, 0x67, 0x6f, 0x2f, 0x62, 0x69, 0x6e",
    }
    for label, encoded in forbidden_encoded_paths.items():
        if encoded in generated:
            print(f"generated C retains forbidden Rust-era path: {label}", file=sys.stderr)
            return 1

    maintained = [line.strip() for line in MAINTAINED.read_text().splitlines() if line.strip()]
    maintained_tests = [line.strip() for line in MAINTAINED_TESTS.read_text().splitlines() if line.strip()]
    requirements = [
        (set(maintained) == {"bottles.c", "installer.c", "launcher.c", "migration.c", "runtime_surface.c"},
         "maintained C runtime manifest"),
        ("tests/policy_test.c" in maintained_tests, "maintained bottle/launcher policy tests"),
        ("MAINTAINED_OBJS" in makefile, "maintained C backend linkage"),
        ("metalsharp_m12_runtime_complete" in generated, "generated-to-native installer bridge"),
        ("metalsharp_launcher_runtime_ready" in (ROOT / "app/src-c/installer.c").read_text(),
         "generated bottle/launch readiness to maintained C policy bridge"),
        ("metalsharp_migration_refresh_saved_bottles" in generated,
         "generated migration to maintained bottle refresh bridge"),
    ]
    for present, label in requirements:
        if not present:
            print(f"missing maintained C ownership contract: {label}", file=sys.stderr)
            return 1

    # This C-only build includes ureq's native-tls connector, not Rustls. The
    # converted default is a one-byte TlsProvider discriminant: 0 = Rustls and
    # 1 = NativeTls. Leaving it at zero makes every HTTPS-backed route panic,
    # including update checks and M12 Agility SDK repair.
    tls_default = re.compile(
        r"_t2947 = .*?24ULL.*?"
        r"_t2948 = \(int8_t\)\(1ULL\);.*?"
        r"\*\(int8_t \*\)_t2947 = _t2948;",
        re.DOTALL,
    )
    for source_root, label in ((C_ROOT, "runtime"), (TEST_C_ROOT, "test")):
        ureq_units = sorted(source_root.glob("ureq-*.c"))
        if not any(tls_default.search(path.read_text(errors="ignore")) for path in ureq_units):
            print(f"{label} ureq default must select NativeTls", file=sys.stderr)
            return 1

    print(f"C-only backend alignment verified for {len(modules)} modules and {len(c_files)} C units.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
