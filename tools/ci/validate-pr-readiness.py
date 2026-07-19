#!/usr/bin/env python3
"""Validate PR body follows the project PR readiness format.

Checks:
  - All 6 required sections are present
  - Type section has at least one checked item
  - PR Readiness (MANDATORY) exists and all its checkboxes are [x]
  - AI disclosure section is present
  - At least one checked box exists in the body (catches empty PRs)
"""

import argparse
import re
import sys
from typing import List, Optional

REQUIRED_SECTIONS = [
    "Summary",
    "Type",
    "PR Readiness \\(MANDATORY\\)",
    "Changes",
    "Testing",
    "AI disclosure",
]


def extract_section(body: str, heading: str) -> Optional[str]:
    """Extract body of a ## heading until the next ## heading or EOF."""
    pattern = rf"^## {heading}\s*\n(.*?)(?=^## |\Z)"
    m = re.search(pattern, body, re.MULTILINE | re.DOTALL)
    if m:
        return m.group(1).strip()
    return None


def find_unchecked(body: str) -> List[str]:
    """Find all unchecked checkboxes in the body."""
    unchecked = []
    for m in re.finditer(r"^- \[ \] (.+)$", body, re.MULTILINE):
        unchecked.append(m.group(1).strip())
    return unchecked


def validate_pr_body(body: str) -> bool:
    ok = True

    # 1. Check required sections exist
    for heading in REQUIRED_SECTIONS:
        section = extract_section(body, heading)
        if section is None:
            print(f"ERROR: Missing required section '## {heading.replace(chr(92), '')}'", file=sys.stderr)
            ok = False

    # 2. Type section must have at least one [x]
    type_section = extract_section(body, "Type")
    if type_section is not None and not re.search(r"- \[x\]", type_section):
        print("ERROR: Type section has no checked items", file=sys.stderr)
        ok = False

    # 3. PR Readiness section must have NO unchecked boxes
    pr_section = extract_section(body, "PR Readiness \\(MANDATORY\\)")
    if pr_section is not None:
        unchecked = find_unchecked(pr_section)
        if unchecked:
            print(f"ERROR: PR readiness has {len(unchecked)} unchecked gate(s):", file=sys.stderr)
            for item in unchecked:
                print(f"  - [ ] {item}", file=sys.stderr)
            ok = False
        else:
            checked = len(re.findall(r"- \[x\]", pr_section))
            total = len(re.findall(r"- \[[ x]\]", pr_section))
            print(f"  PR Readiness: {checked}/{total} gates checked", file=sys.stderr)
    else:
        print("ERROR: Could not parse PR Readiness section", file=sys.stderr)
        ok = False

    # 4. At least one [x] anywhere in the body
    if not re.search(r"- \[x\]", body):
        print("ERROR: No checked items found anywhere in PR body", file=sys.stderr)
        ok = False

    if ok:
        print("OK: PR readiness format passes", file=sys.stderr)
        return True
    return False


def main():
    parser = argparse.ArgumentParser(description="Validate PR readiness format")
    parser.add_argument(
        "--body-file",
        help="Path to file containing PR body",
    )
    args = parser.parse_args()

    if args.body_file:
        with open(args.body_file, "r") as f:
            body = f.read()
    else:
        body = sys.stdin.read()

    if not body.strip():
        print("ERROR: PR body is empty", file=sys.stderr)
        sys.exit(1)

    ok = validate_pr_body(body)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
