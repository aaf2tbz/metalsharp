#!/usr/bin/env python3
"""Warn about stale docs.

A doc is considered stale if it has no `Updated:` / `Created:` / `Date:` line in
its first 10 lines, or if the `Updated:` line is older than `STALE_DAYS`.

By default this is a warning-only check (exit 0) so it doesn't break CI while
docs are being refreshed. Pass `--strict` to make it an error.

Usage:
    python3 tools/ci/check-doc-freshness.py [--strict] [--stale-days=120]
"""

from __future__ import annotations

import argparse
import re
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"
README = ROOT / "README.md"
AGENTS = ROOT / "AGENTS.md"

DATE_RE = re.compile(
    r"(?i)(?:updated|created|last\s+updated|date|modified)[^0-9\n]{0,15}(\d{4}-\d{2}-\d{2})"
)

# Days at which a doc is considered stale (4 months)
DEFAULT_STALE_DAYS = 120

# Files that are exempt from this check
EXEMPT = {
    "docs/archive",  # Archived docs are historical
    "docs/README.md",  # Repo map
    "docs/compatibility/GAMES-SUPPORTED.md",  # Updated frequently
}


def is_exempt(path: Path) -> bool:
    rel = str(path.relative_to(ROOT))
    if rel.startswith("docs/archive/"):
        return True
    return rel in EXEMPT


def doc_date(path: Path) -> str | None:
    """Return the most recent date stamp found in the first 20 lines."""
    head = "\n".join(path.read_text(errors="replace").splitlines()[:20])
    matches = DATE_RE.findall(head)
    if not matches:
        return None
    # Return the most recent date among matched stamps
    return max(matches)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Exit non-zero if any stale docs are found.",
    )
    parser.add_argument(
        "--stale-days",
        type=int,
        default=DEFAULT_STALE_DAYS,
        help=f"Days before a doc is considered stale (default: {DEFAULT_STALE_DAYS}).",
    )
    args = parser.parse_args()

    today = date.today()
    cutoff = today.toordinal() - args.stale_days

    missing_date: list[Path] = []
    stale: list[tuple[Path, str, int]] = []  # (path, date, days_old)

    targets = list(DOCS.rglob("*.md"))
    for extra in (README, AGENTS):
        if extra.exists():
            targets.append(extra)

    for md in sorted(targets):
        if is_exempt(md):
            continue
        d = doc_date(md)
        if d is None:
            missing_date.append(md)
            continue
        try:
            doc_d = date.fromisoformat(d)
        except ValueError:
            missing_date.append(md)
            continue
        age = today.toordinal() - doc_d.toordinal()
        if doc_d.toordinal() < cutoff:
            stale.append((md, d, age))

    if missing_date:
        print(
            f"docs without `Updated:` header ({len(missing_date)}):",
            file=sys.stderr,
        )
        for p in missing_date[:20]:
            print(f"  {p.relative_to(ROOT)}", file=sys.stderr)
        if len(missing_date) > 20:
            print(
                f"  ... and {len(missing_date) - 20} more",
                file=sys.stderr,
            )

    if stale:
        print(
            f"docs older than {args.stale_days} days ({len(stale)}):",
            file=sys.stderr,
        )
        for p, d, age in stale[:20]:
            print(
                f"  {p.relative_to(ROOT)} (updated {d}, {age} days old)",
                file=sys.stderr,
            )
        if len(stale) > 20:
            print(
                f"  ... and {len(stale) - 20} more",
                file=sys.stderr,
            )

    if not missing_date and not stale:
        print(
            f"doc freshness OK: all docs have `Updated:` headers and are within "
            f"{args.stale_days} days.",
        )
        return 0

    if args.strict and (missing_date or stale):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
