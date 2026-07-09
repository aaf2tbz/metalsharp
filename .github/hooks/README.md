# Pre-commit Hook

This directory contains shared pre-commit hook(s) for the MetalSharp repo.
Hooks are NOT installed by default — each developer opts in.

## Install (one-time per clone)

```bash
# From the repo root
ln -s ../../.github/hooks/pre-commit .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

To remove: `rm .git/hooks/pre-commit`.

Or, on a fresh clone, run the install helper:

```bash
./install.sh install-hooks
```

(This is currently a no-op stub; the symlink command above is the canonical
install path. A future PR will add an `install-hooks` flag to `install.sh`.)

## What it checks

The `pre-commit` hook runs these checks only when the relevant files are staged:

| Files staged | Check |
|--------------|-------|
| `*.rs`, `*.toml` under `app/` | `cargo fmt --check` |
| `*.rs`, `*.toml` under `app/` | `cargo clippy --all-targets -- -D warnings` |
| `*.c`, `*.cpp`, `*.h`, `*.hpp`, `*.mm`, `*.m` | `clang-format --dry-run --Werror` |
| `app/**/*.{ts,tsx,js,jsx,json,css,html}` | `prettier --check` |
| `configs/mtsp-rules.toml` | `python3 tools/ci/validate-rules-toml.py` |
| `*.md`, `README.md`, `AGENTS.md` | `python3 tools/ci/check-doc-freshness.py` (warn-only) |

Checks skip themselves silently if the relevant toolchain isn't installed
(e.g. `cargo` missing on a frontend-only workstation).

## Skipping

```bash
git commit --no-verify
```

CI still runs the same checks, so skipping locally won't make a bad commit
go through — it just means the failure surfaces in CI instead of locally.

## Why not a project-wide setting?

Hooks live in `.git/hooks/` and aren't tracked by git. A symlink under
`.github/hooks/` lets the team share the script without forcing install —
some developers prefer to run checks via their own tooling (e.g. `lefthook`,
`pre-commit`, or a Makefile target) and the symlink-based approach makes the
shared script easy to swap out.
