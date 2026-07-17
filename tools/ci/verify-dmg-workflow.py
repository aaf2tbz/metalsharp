#!/usr/bin/env python3
"""Validate the DMG build/publish contract without building a DMG."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def fail(message: str) -> None:
    print(f"DMG workflow check failed: {message}", file=sys.stderr)
    sys.exit(1)


def read(path: str) -> str:
    full = ROOT / path
    if not full.exists():
        fail(f"missing required file: {path}")
    return full.read_text()


def manifest_assets() -> list[str]:
    assets: list[str] = []
    for line in read("tools/bundles/asset-manifest.tsv").splitlines():
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) < 3:
            fail(f"invalid manifest row: {line}")
        asset, _root, platforms = fields[:3]
        if "mac" in platforms.split(","):
            assets.append(asset)
    if not assets:
        fail("bundle manifest has no mac assets")
    return assets


def check_package_resources(assets: list[str]) -> None:
    package = json.loads(read("app/package.json"))
    build = package.get("build", {})
    resources = build.get("extraResources", [])
    if not isinstance(resources, list):
        fail("app/package.json build.extraResources must be a list")

    pairs = {
        (entry.get("from"), entry.get("to"))
        for entry in resources
        if isinstance(entry, dict)
    }
    required_pairs = {
        ("build/c-backend/metalsharp-backend", "runtime/metalsharp-backend"),
        ("native/host", "runtime/host"),
        ("updater", "scripts/tools/updater"),
        ("../dist/bundles/metalsharp-bundle-manifest.tsv", "bundles/metalsharp-bundle-manifest.tsv"),
    }
    required_pairs.update((f"bundles/{asset}", f"bundles/{asset}") for asset in assets)

    missing = sorted(required_pairs - pairs)
    if missing:
        fail(f"app/package.json missing extraResources entries: {missing}")

    if build.get("afterPack") != "build/adhoc-deep-sign.cjs":
        fail("app/package.json must keep afterPack=build/adhoc-deep-sign.cjs")
    if build.get("afterSign") != "build/notarize.cjs":
        fail("app/package.json must keep afterSign=build/notarize.cjs")


def check_dmg_verifier(assets: list[str]) -> None:
    verifier = read("tools/dmg/verify-dmg-runtime-assets.sh")
    for needle in [
        "Contents/Resources",
        "runtime/metalsharp-backend",
        "runtime/host",
        "scripts/tools/updater/update.py",
        "scripts/tools/updater/update.sh",
        "tools/bundles/verify-bundles.sh",
        "tools/bundles/verify-bundle-manifest.py",
        "metalsharp-bundle-manifest.tsv",
    ]:
        if needle not in verifier:
            fail(f"DMG verifier no longer checks {needle}")

    for asset in assets:
        if asset not in verifier:
            fail(f"DMG verifier no longer checks bundle asset {asset}")


def check_updater_handoff() -> None:
    python_updater = read("app/updater/update.py")
    shell_updater = read("app/updater/update.sh")
    for path, updater in [("app/updater/update.py", python_updater), ("app/updater/update.sh", shell_updater)]:
        for needle in ["hdiutil", "attach", "-mountpoint", "metalsharp-update-mount", "detach_mount(mount_point" if path.endswith(".py") else "detach_mount"]:
            if needle not in updater:
                fail(f"{path} no longer mounts the downloaded DMG on a private update mount point before install")
        for needle in ["--backend-port", "METALSHARP_PORT", ".backend-port", "contract_version"]:
            if needle not in updater:
                fail(f"{path} must preserve and verify the C backend launch port: {needle}")
        if "127.0.0.1:9274" in updater:
            fail(f"{path} must not use the retired fixed backend port")

    if '"complete",\n            100,\n            "Update installed. C backend contract v1 verified' not in python_updater:
        fail("Python updater must report verified C-backend handoff as complete")
    if '"error",\n            95,\n            "Update installed, but the C backend contract was not ready' not in python_updater:
        fail("Python updater must report a failed C-backend handoff as error")

    electron_main = read("app/src/main/index.ts")
    for needle in [
        "backendNeedsMigration || marker.needed",
        'requestMigrationBackend("GET", "/update/migrate/check")',
        'requestMigrationBackend("POST", "/update/migrate/start"',
        'requestMigrationBackend("GET", "/update/migrate/progress")',
    ]:
        if needle not in electron_main:
            fail(f"Electron migration handoff is missing: {needle}")

    migration_view = read("app/src/renderer/components/MigrationView.vue")
    for needle in ["pollInFlight", "Retry Migration", "Reconnecting to the migration backend"]:
        if needle not in migration_view:
            fail(f"migration wizard recovery is missing: {needle}")

    native_migrator = read("tools/migrator/main.m")
    if "METALSHARP_PORT" not in native_migrator or "127.0.0.1:9274" in native_migrator:
        fail("native migrator must follow the private C-backend port")


def check_bundle_scripts() -> None:
    create_bundles = read("tools/dmg/create-bundles.sh")
    for needle in [
        "tools/dmg/repair-runtime-bundle.py",
        "repair_assets_fnalibs_bundle",
        "tools/bundles/verify-bundles.sh",
        "--bundle-dir \"$BUNDLE_DIR\" \"$asset\"",
        "verify-bundle-manifest.py",
        "source-metalsharp-bundle-manifest.tsv",
        "Refreshing stale or unlocked bundle",
        "metalsharp-bundle-manifest.tsv",
        "verify-bundle-manifest.py",
        "GRAPHICS_SHA_BEFORE",
        "verify-dxmt-surfaces.py",
        "Frozen graphics bundle changed during release staging",
    ]:
        if needle not in create_bundles:
            fail(f"create-bundles.sh no longer performs {needle}")

    runtime_repair = read("tools/dmg/repair-runtime-bundle.py")
    if 'DEFAULT_BACKEND = PROJECT_ROOT / "app" / "build" / "c-backend" / "metalsharp-backend"' not in runtime_repair:
        fail("runtime-bundle repair must default to the C backend")

    split_bundles = read("tools/bundles/create-split-bundles.py")
    if 'APP_DIR / "build" / "c-backend" / "metalsharp-backend"' not in split_bundles:
        fail("split bundle staging must use the C backend")
    bridge = read("app/src/main/backend-bridge.ts")
    if '"build", "c-backend", "metalsharp-backend"' not in bridge:
        fail("Electron backend selection must use the Clang-built C artifact")

    stage_bundles = read("tools/dmg/stage-release-bundles.sh")
    if "asset-manifest.tsv" not in stage_bundles or "tar --use-compress-program=unzstd" not in stage_bundles:
        fail("stage-release-bundles.sh no longer stages bundle manifest archives")
    if "verify-dxmt-surfaces.py" not in stage_bundles:
        fail("stage-release-bundles.sh must byte-verify the frozen DXMT surface before extraction")
    for forbidden in [
        "repair_graphics_m12_bundle",
        "METALSHARP_REPAIR_GRAPHICS_BUNDLE",
        "METALSHARP_DXMT_M12_ROOT",
    ]:
        if forbidden in create_bundles:
            fail(f"create-bundles.sh must not mutate the frozen graphics bundle: {forbidden}")


def check_workflows(assets: list[str]) -> None:
    pr = read(".github/workflows/pr-ci.yml")
    main = read(".github/workflows/ci.yml")
    release = read(".github/workflows/release.yml")
    if "tools/bundles/verify-production-dxmt-conformance.sh" not in release:
        fail("release workflow must reject nonconforming production DXMT archives before packaging")

    production_gate = read("tools/bundles/verify-production-dxmt-conformance.sh")
    for needle in [
        "verify-dxmt-surfaces.py",
        "metalsharp-bottle-deployment-tests",
        "metalsharp-runtime.tar.zst",
        "metalsharp-graphics-dll.tar.zst",
    ]:
        if needle not in production_gate:
            fail(f"production DXMT conformance gate no longer checks {needle}")
    for forbidden in ["run-probes.sh", "--winemetal-abi-only", "--loader-only", "--mini-only"]:
        if forbidden in production_gate:
            fail(f"production DXMT conformance gate must not execute Wine probes: {forbidden}")

    clean_setup = read("tools/dmg/verify-dmg-clean-setup.sh")
    for needle in [
        "Graphics/dll/dxmt_m12",
        "Graphics/dll/dxmt",
        "runtime/wine/lib/wine/x86_64-unix/winemetal.so",
        "drive_c/windows/system32",
        "clean-dmg-game-m12",
        "clean-dmg-game-m11",
    ]:
        if needle not in clean_setup:
            fail(f"clean-DMG verifier no longer byte-checks {needle}")

    if "DMG Workflow CI" not in pr:
        fail("PR CI must keep a lightweight DMG Workflow CI job")
    for forbidden in ["electron-builder --mac dmg", "Verify mounted DMG runtime assets"]:
        if forbidden in pr:
            fail(f"PR CI should not run the full DMG build path: {forbidden}")

    for required in ["Shell CI", "Metal CI", "Vue CI", "C Backend CI", "Electron CI", "C/C++/Obj-C CI", "DMG Workflow CI"]:
        if required not in main:
            fail(f"main CI missing validation job: {required}")
    if "make -C app/src-c verify" not in main:
        fail("main CI must validate the direct C backend")
    for forbidden in [
        "Verify Developer SDK Bundle",
        "Build DMG",
        "Package DMG",
        "Verify DMG runtime assets",
        "metalsharp-build-artifacts",
    ]:
        if forbidden in main:
            fail(f"main CI should not run the full DMG build path: {forbidden}")
    if "group: metalsharp-developer-sdk-bundles" in main:
        fail("main CI verifier must not share the release SDK publish concurrency group")

    for required in [
        "make -C app/src-c verify",
        "app/build/c-backend/metalsharp-backend",
        "electron-builder --mac dmg --arm64 --publish never",
    ]:
        if required not in release:
            fail(f"release workflow must build, validate, and package the C backend: {required}")

    for required in [
        "Publish Developer SDK Bundle",
        "Publish developer SDK package",
        "Publish complete release bundle set",
        "Build DMG",
        "Check Apple signing credentials",
        "Verify Apple notarization",
        "Mark unsigned DMG",
        "Create GitHub Release",
        "Record release build provenance",
    ]:
        if required not in release:
            fail(f"release workflow missing publish step: {required}")
    if "vars.METALSHARP_DEVELOPER_SDK_CI == '1'" not in release:
        fail("release Developer SDK job must be opt-in with METALSHARP_DEVELOPER_SDK_CI=1")
    build_job = release.split("\n  build:\n", 1)[1].split("\n  release:\n", 1)[0]
    if "needs: [developer-sdk]" in build_job or "needs.developer-sdk" in build_job:
        fail("the optional Developer SDK job must not be in the DMG dependency graph")
    for asset in assets:
        publish_path = (
            f"dist/developer-sdk/{asset}"
            if asset == "metalsharp-d3d12-developer-sdk.tar.zst"
            else f"dist/bundles/{asset}"
        )
        if publish_path not in release:
            fail(f"release workflow no longer publishes complete bundle asset: {publish_path}")
    for required in ["dist/developer-sdk/metalsharp-bundle-manifest.tsv"]:
        if required not in release:
            fail(f"release workflow missing immutable bundle contract: {required}")
    for forbidden in [
        "Prepare release Wine runtime for M12 build",
        "Install DXMT build tools",
        "Build staged M12 DXMT runtime",
        "prepare-dxmt-x86-llvm15.sh",
        "stage-dxmt-runtime.py",
        "METALSHARP_DXMT_M12_ROOT",
    ]:
        if forbidden in release:
            fail(f"release workflow must package the frozen DXMT surface without rebuilding it: {forbidden}")
    for required in ["Build host runtime ABI", "make -C app/src-c verify"]:
        if required not in release:
            fail(f"release workflow must preserve the v0.45.5 build shape with the C backend: {required}")
    if release.count('verify-version-alignment.py --tag "$GITHUB_REF_NAME"') != 2:
        fail("developer SDK and DMG jobs must both reject tag/C-backend version drift")
    for required in [
        "tools/dmg/check-apple-signing-readiness.sh",
        "steps.apple-signing.outputs.ready == 'true'",
        "DMG-SIGNING.txt",
        "release-provenance.tsv",
    ]:
        if required not in release:
            fail(f"release workflow missing signing fallback contract: {required}")
    if "CSC_IDENTITY_AUTO_DISCOVERY=false" not in read("tools/dmg/check-apple-signing-readiness.sh"):
        fail("unsigned DMG fallback must disable Electron Builder certificate discovery")
    adhoc_sign = read("app/build/adhoc-deep-sign.cjs")
    for required in [
        "METALSHARP_UNSIGNED_DMG",
        "codesign",
        "--deep",
        "--timestamp=none",
        "--verify",
    ]:
        if required not in adhoc_sign:
            fail(f"ad-hoc deep-sign hook missing hardening contract: {required}")
    notarization = read("tools/dmg/verify-notarization.sh")
    for required in [
        "Authority=Developer ID Application",
        "xcrun stapler validate",
        "hdiutil verify",
        "spctl -a -vvv --type open",
    ]:
        if required not in notarization:
            fail(f"notarization verifier missing hardening check: {required}")


def main() -> int:
    assets = manifest_assets()
    check_package_resources(assets)
    check_dmg_verifier(assets)
    check_updater_handoff()
    check_bundle_scripts()
    check_workflows(assets)
    print(f"DMG workflow contract verified ({len(assets)} mac bundle assets).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
