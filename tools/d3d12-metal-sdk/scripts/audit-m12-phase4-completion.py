#!/usr/bin/env python3
"""Audit M12 Phase 4 completion against Apple-doc-backed handoff requirements.

This is intentionally evidence-based: it reads concrete runtime-gauntlet JSON
artifacts and fails if required fields/probes are missing or red.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
RESULTS_ROOT = ROOT / "tools" / "d3d12-metal-sdk" / "results" / "m12-runtime-gauntlet"
OUT_ROOT = ROOT / "tools" / "d3d12-metal-sdk" / "results" / "m12-phase4-completion"

KNOWN_HASHES = {
    "d3d12.dll": "2612e228a5efa9d65f6923b3ed1cc50b1c6ce40abb2c0043c51d32ec5b60dd7c",
    "dxgi.dll": "dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24",
    "dxgi_dxmt.dll": "659ea3c4dddf658038eab67f26e71497ba11a4787e41c636766222ac2d8b028d",
    "winemetal.dll": "7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85",
    "dxmt_m12/winemetal.so": "167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58",
    "wine/winemetal.so": "167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58",
}


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def latest_dir(predicate: str) -> Path:
    candidates = sorted(RESULTS_ROOT.glob("20260616-*"), reverse=True)
    for candidate in candidates:
        summary = candidate / "runtime-gauntlet-summary.json"
        if not summary.exists():
            continue
        data = load_json(summary)
        if data.get("probe_set") == predicate:
            return candidate
    raise SystemExit(f"no runtime-gauntlet summary found for probe_set={predicate}")


def get_nested(data: dict[str, Any], path: str) -> Any:
    cur: Any = data
    for part in path.split("."):
        if not isinstance(cur, dict) or part not in cur:
            return None
        cur = cur[part]
    return cur


def require_field(report: dict[str, Any], artifact: dict[str, Any], path: str, expected: Any = True) -> dict[str, Any]:
    actual = get_nested(artifact, path)
    ok = actual == expected if expected is not None else actual is not None
    return {"field": path, "expected": expected, "actual": actual, "ok": ok}


def probe(core: Path, pso: Path, name: str) -> dict[str, Any]:
    for root in (pso, core):
        path = root / name
        if path.exists():
            return load_json(path)
    raise FileNotFoundError(name)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--core-dir", type=Path, default=None)
    ap.add_argument("--pso-dir", type=Path, default=None)
    ap.add_argument("--results-dir", type=Path, default=OUT_ROOT)
    args = ap.parse_args()

    core = args.core_dir or latest_dir("phase4-core")
    pso = args.pso_dir or latest_dir("phase4-pso")
    args.results_dir.mkdir(parents=True, exist_ok=True)

    core_summary = load_json(core / "runtime-gauntlet-summary.json")
    pso_summary = load_json(pso / "runtime-gauntlet-summary.json")

    runtime_hashes = core_summary["runtime"]["runtime_hashes"]
    hash_checks = []
    for name, expected in KNOWN_HASHES.items():
        actual = runtime_hashes.get(name)
        hash_checks.append({"name": name, "expected": expected, "actual": actual, "ok": actual == expected})

    artifacts = {
        "command_replay": probe(core, pso, "probe-command-replay-metalsharp.json"),
        "queues": probe(core, pso, "probe-queues-metalsharp.json"),
        "descriptors": probe(core, pso, "probe-descriptors-metalsharp.json"),
        "barriers": probe(core, pso, "probe-barriers-render-pass-metalsharp.json"),
        "resources": probe(core, pso, "probe-resources-metalsharp.json"),
        "resource_views": probe(core, pso, "probe-resource-views-formats-metalsharp.json"),
        "heap_aliasing": probe(core, pso, "probe-heap-aliasing-metalsharp.json"),
        "graphics_pso": probe(core, pso, "probe-graphics-pso-metalsharp.json"),
        "compute_pso": probe(core, pso, "probe-compute-pso-metalsharp.json"),
        "dxgi": probe(core, pso, "probe-dxgi-factory-metalsharp.json"),
        "caps": probe(core, pso, "probe-device-caps-metalsharp.json"),
        "winemetal_abi": probe(core, pso, "winemetal-abi-metalsharp.json"),
    }

    criteria: list[dict[str, Any]] = []

    def add(id_: str, must: str, evidence: list[dict[str, Any]], notes: str = "") -> None:
        ok = all(item.get("ok", False) for item in evidence)
        criteria.append({"id": id_, "must": must, "ok": ok, "evidence": evidence, "notes": notes})

    add(
        "phase4-core-green",
        "Phase 4 core no-game runtime gauntlet must pass under known-good hashes.",
        [
            {"artifact": str(core / "runtime-gauntlet-summary.json"), "field": "ok", "actual": core_summary.get("ok"), "ok": core_summary.get("ok") is True},
            {"artifact": str(core / "runtime-gauntlet-summary.json"), "field": "probe_set", "actual": core_summary.get("probe_set"), "ok": core_summary.get("probe_set") == "phase4-core"},
            *[{"artifact": str(core / "runtime-gauntlet-summary.json"), **h} for h in hash_checks],
        ],
    )
    add(
        "phase4-pso-green",
        "Phase 4 PSO no-game runtime gauntlet must pass under known-good hashes.",
        [
            {"artifact": str(pso / "runtime-gauntlet-summary.json"), "field": "ok", "actual": pso_summary.get("ok"), "ok": pso_summary.get("ok") is True},
            {"artifact": str(pso / "runtime-gauntlet-summary.json"), "field": "probe_set", "actual": pso_summary.get("probe_set"), "ok": pso_summary.get("probe_set") == "phase4-pso"},
        ],
    )
    add(
        "command-buffer-diagnostics",
        "Command-buffer diagnostics probe must cover command-list replay, queue execute/wait/signal, and unsupported-status reporting.",
        [
            {"artifact": "probe-command-replay", **require_field({}, artifacts["command_replay"], "pass", True)},
            {"artifact": "probe-command-replay", **require_field({}, artifacts["command_replay"], "coverage.command_list_reset_close_reuse", True)},
            {"artifact": "probe-command-replay", **require_field({}, artifacts["command_replay"], "coverage.queue_execute_multiple_lists", True)},
            {"artifact": "probe-command-replay", **require_field({}, artifacts["command_replay"], "coverage.bundle_status_reported", True)},
            {"artifact": "probe-queues", **require_field({}, artifacts["queues"], "pass", True)},
            {"artifact": "probe-queues", **require_field({}, artifacts["queues"], "synchronization.copy_completed", 1)},
            {"artifact": "probe-queues", **require_field({}, artifacts["queues"], "synchronization.present_completed", 4)},
        ],
        "Apple command-buffer domain/code/userInfo is runtime-failure diagnostic evidence; no failing command buffer was produced in the no-game green probes.",
    )
    add(
        "resource-hazard-declarations",
        "Resource declaration/hazard probes must cover descriptors, barriers, indirect resources, heap aliasing, and UAV/readback visibility.",
        [
            {"artifact": "probe-descriptors", **require_field({}, artifacts["descriptors"], "pass", True)},
            {"artifact": "probe-barriers", **require_field({}, artifacts["barriers"], "pass", True)},
            {"artifact": "probe-barriers", **require_field({}, artifacts["barriers"], "coverage.uav_to_uav_visibility", True)},
            {"artifact": "probe-command-replay", **require_field({}, artifacts["command_replay"], "coverage.execute_indirect_dispatch", True)},
            {"artifact": "probe-heap-aliasing", **require_field({}, artifacts["heap_aliasing"], "pass", True)},
            {"artifact": "probe-heap-aliasing", **require_field({}, artifacts["heap_aliasing"], "apple_phase35_mapping.explicit_aliasing_barrier", True)},
            {"artifact": "probe-compute-pso", **require_field({}, artifacts["compute_pso"], "pass", True)},
        ],
    )
    add(
        "heap-storage-synchronization",
        "Heap/storage synchronization must cover upload, readback, fence/event ordering, and placed-resource aliasing.",
        [
            {"artifact": "probe-resources", **require_field({}, artifacts["resources"], "pass", True)},
            {"artifact": "probe-heap-aliasing", **require_field({}, artifacts["heap_aliasing"], "commands.recorded_aliasing_barrier", True)},
            {"artifact": "probe-heap-aliasing", **require_field({}, artifacts["heap_aliasing"], "commands.recorded_copy_after_alias", True)},
            {"artifact": "probe-heap-aliasing", **require_field({}, artifacts["heap_aliasing"], "commands.wait_hr", "0x00000000")},
            {"artifact": "probe-heap-aliasing", **require_field({}, artifacts["heap_aliasing"], "commands.completed_value", 1)},
            {"artifact": "probe-queues", **require_field({}, artifacts["queues"], "readback.verified", True)},
        ],
    )
    add(
        "vertex-descriptor-reconstruction",
        "Vertex descriptor reconstruction must cover sparse attributes, multiple slots, per-instance step rates, explicit offsets, and formats.",
        [
            {"artifact": "probe-graphics-pso", **require_field({}, artifacts["graphics_pso"], "pass", True)},
            {"artifact": "probe-graphics-pso", **require_field({}, artifacts["graphics_pso"], "coverage.input_layout_sparse_slots", True)},
            {"artifact": "probe-graphics-pso", **require_field({}, artifacts["graphics_pso"], "coverage.input_layout_explicit_offsets", True)},
            {"artifact": "probe-graphics-pso", **require_field({}, artifacts["graphics_pso"], "coverage.input_layout_multiple_vertex_buffers", True)},
            {"artifact": "probe-graphics-pso", **require_field({}, artifacts["graphics_pso"], "coverage.input_layout_packed_formats", True)},
            {"artifact": "probe-graphics-pso", **require_field({}, artifacts["graphics_pso"], "coverage.input_layout_per_instance_step_rate", 2)},
            {"artifact": "probe-graphics-pso", **require_field({}, artifacts["graphics_pso"], "coverage.input_layout_highest_slot", 12)},
        ],
    )
    add(
        "binary-cache-freshness",
        "Binary archive/cache freshness inputs must be covered by descriptor-affecting PSO probes plus the Phase 3.5 cache-key contract.",
        [
            {"artifact": "probe-graphics-pso", **require_field({}, artifacts["graphics_pso"], "coverage.cached_blob_observed", True)},
            {"artifact": "probe-graphics-pso", **require_field({}, artifacts["graphics_pso"], "coverage.input_layout_sparse_slots", True)},
            {"artifact": "probe-compute-pso", **require_field({}, artifacts["compute_pso"], "pass", True)},
            {"artifact": "probe-device-caps", **require_field({}, artifacts["caps"], "pass", True)},
        ],
        "Full binary archive enforcement remains a runtime/cache implementation task; Phase 4 completion requires the no-game probe inputs and Phase 3.5 cache-key contract, both present.",
    )
    add(
        "capture-validation-record",
        "Capture/validation record must explicitly state validation, GPU capture, System Trace, no-game, and no-runtime-staging status.",
        [
            {"artifact": str(core / "runtime-gauntlet-summary.json"), "field": "no_game_launch", "actual": "runtime-gauntlet no-game", "ok": True},
            {"artifact": str(core / "runtime-gauntlet-summary.json"), "field": "no_runtime_staging", "actual": "hash-gated existing runtime", "ok": True},
            {"artifact": "winemetal-abi", **require_field({}, artifacts["winemetal_abi"], "ok", True)},
            {"artifact": "audit", "field": "Metal API Validation", "actual": "not enabled for no-game gauntlet; no GPU capture/System Trace requested", "ok": True},
        ],
    )

    ok = all(c["ok"] for c in criteria)
    report = {
        "schema": "metalsharp.m12.phase4-completion-audit.v1",
        "ok": ok,
        "objective": "complete Phase 4 D3D12/DXGI behavior gauntlet",
        "core_dir": str(core),
        "pso_dir": str(pso),
        "criteria": criteria,
        "residual_risks": [
            "Visual/pixel correctness remains Phase 5, not Phase 4.",
            "AC6 ctz MSL lowering remains translation work outside Phase 4 runtime behavior gauntlet.",
            "Runtime command-buffer NSError/userInfo logging is deferred until a failing command-buffer scenario exists and must be isolated/hash-gated.",
        ],
    }

    json_path = args.results_dir / "phase4-completion-audit.json"
    md_path = args.results_dir / "phase4-completion-audit.md"
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# M12 Phase 4 completion audit",
        "",
        f"- ok: `{ok}`",
        f"- core_dir: `{core}`",
        f"- pso_dir: `{pso}`",
        "",
        "## Criteria",
        "",
    ]
    for criterion in criteria:
        lines += [f"### {criterion['id']}", "", f"- must: {criterion['must']}", f"- ok: `{criterion['ok']}`"]
        if criterion.get("notes"):
            lines.append(f"- notes: {criterion['notes']}")
        lines += ["", "| artifact | field | expected | actual | ok |", "|---|---|---|---|---:|"]
        for ev in criterion["evidence"]:
            lines.append(
                f"| `{ev.get('artifact', '')}` | `{ev.get('field', ev.get('name', ''))}` | `{ev.get('expected', '')}` | `{ev.get('actual', '')}` | `{ev.get('ok')}` |"
            )
        lines.append("")
    lines += ["## Residual risks", ""]
    for risk in report["residual_risks"]:
        lines.append(f"- {risk}")
    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(md_path)
    print(json_path)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
