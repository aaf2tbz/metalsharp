# M12 AC6 Slice-2 Stable Stage/Launch Runbook

Status: roadmap/runbook only. Do not treat this as permission to change launch code, rebuild runtime, restart Wine, or launch a game without an explicit operator step.

## Stable baseline

This is the baseline that successfully launched Armored Core VI visually on 2026-06-18/19.

- Repo: `/Users/alexmondello/metalsharp-m12-lab`
- Branch: `fix/m12-shader-probe-lab`
- Required git HEAD/local/origin:
  - `0573c54caaca70fd384f5f23becb14d7007bd6d4`
  - `chore: restore m12 queue depth slice`
- Backend route:
  - `http://127.0.0.1:9277`
  - env var is `METALSHARP_PORT=9277`, not `PORT`
- Launch route:
  - `POST /steam/launch-game`
  - Do not use `/mtsp/prepare` for this baseline.
- Canonical launch method:
  - `m12`
  - `dxmt_metal12` maps to M12, but use `m12` in scripts to avoid ambiguity.
- AC6 appid:
  - `1888160`
- Game executable:
  - `/Volumes/AverySSD/SteamLibrary/steamapps/common/ARMORED CORE VI FIRES OF RUBICON/Game/start_protected_game.exe`
- Launch args:
  - `-windowed -ResX=1280 -ResY=720 -ForceRes`
- Prefix:
  - `~/.metalsharp/prefix-steam`
- Runtime root:
  - `~/.metalsharp/runtime/wine`
- M12 runtime lanes:
  - Windows DLLs: `~/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-windows`
  - Unix sidecars: `~/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-unix`
- M12 must route through `dxmt_m12`, not the generic `dxmt` lane.
- `mscompatdb` must not be loaded. The stable launch had `mscompatdb.so` absent from the Wine unix route and the launch log showed `mscompatdb: not found`.

## Stable rebuilt artifact hashes

These are the rebuilt Slice-2 artifacts that were build/runtime/game-local synced before the successful launch.

| Artifact | SHA-256 |
|---|---|
| `d3d12.dll` | `dd370840a88f89bd1f294c31503c50a97050c014fd4143779d09b8b766cd5ac3` |
| `d3d11.dll` | `d913f104049aac786aa8287b46a04f0f4733f58e47b0ac5160074bd115a81e97` |
| `dxgi.dll` | `21379e4610fe99019feba6ff27399b3b76bf8eb05287da0c1e1374657ede63e5` |
| `dxgi_dxmt.dll` | `78fb4e713bd475032c1d9862b2755b02e50c953e82a8011a29eb6049fea11fbe` |
| `d3d10core.dll` | `e4c59c7e829f47c14c32b14ec8fc8e5b3cbdacadf5357c8f1290c99c589c2fdb` |
| `winemetal.dll` | `5c14870e651df8805cea291e0b15209795c46fc99259b5c9b1be590ecbab5a9b` |
| `winemetal.so` | `3da472832bfa5fd70dc04bdc46373d7a31b99a1b3b58f319c5f30da56f50bc75` |
| `libm12core.dylib` | `ff515ec40bb085cd1524f6024fa89842b25cbe33a83d9aeb3497a039a621dc69` |
| `metalsharp-backend` | `6fdface8f06740091d0047669ae6abf91ddd8088606ec2ad458ba978b04a7c0b` |

Hash rule: all Windows DLLs above must match between build output, M12 runtime lane, and AC6 game directory. Unix sidecars must match between build output and M12 unix runtime lane.

## Absolute invariants

Do not regress these.

1. Never launch until git, runtime, game-local DLLs, backend, env, and `mscompatdb` state all pass verification.
2. Never infer readiness from git HEAD or a partial six-file hash match alone.
3. Never stage M12 into `~/.metalsharp/runtime/wine/lib/dxmt`; M12 staging target is `lib/dxmt_m12`.
4. Never use `tools/d3d12-metal-sdk/scripts/deploy-m12-runtime.sh` for this baseline. It was removed/retired and previously regressed launch state.
5. Never use `/mtsp/prepare` for this baseline.
6. Never let `WINEDLLOVERRIDES` contain `mscompatdb`.
7. Never let `mscompatdb.so` be present on the active Wine unix route for this launch baseline.
8. Never enable trace/log-heavy defaults for the visual launch check.
9. Preserve live shader caches unless the operator explicitly asks to validate source-compile/cache behavior.
10. Do not kill Steam, Wine, wineserver, MetalSharp app backend, or game processes unless the operator explicitly approves that specific action.
11. Do not commit generated binaries, logs, raw payloads, shader caches, or result corpora.
12. Do not commit anything without explicit operator approval.

## One-shot launch command

Only run after verification passes and the operator explicitly approves launch.

```bash
curl -sS -X POST 'http://127.0.0.1:9277/steam/launch-game' \
  -H 'Content-Type: application/json' \
  -d '{
    "appid": 1888160,
    "launchMethod": "m12",
    "envOverrides": {
      "METALSHARP_M12_LOG_LEVEL": "none",
      "METALSHARP_M12_LOG_PATH": "none",
      "METALSHARP_M12_TRACE_CAPTURE": "0",
      "METALSHARP_M12_FORCE_DXIL_SOURCE_COMPILE": "0",
      "METALSHARP_M12CORE_ENABLE": "1",
      "METALSHARP_M12CORE_REQUIRED": "0"
    }
  }'
```

Expected launch response fields:

- `ok: true`
- `pid: <wine/game pid>`
- `pipeline: "m12"`
- `gameType: "dxmt"`
- `recipe.exe_path` points to `start_protected_game.exe`
- `recipe.launch_args` equals `['-windowed', '-ResX=1280', '-ResY=720', '-ForceRes']`
- `env_overrides` includes all six requested no-log/M12Core keys.

Expected early process state:

```bash
ps -p "$PID" -o pid,ppid,state,etime,command
```

The process should remain alive after a short delay. Do not treat a spawned PID alone as visual success; the operator must confirm the game window is visible/running.

## Current manual baseline procedure

### 0. Stop and require approval gates

Before touching runtime or launching:

- Ask whether mutation is allowed.
- Ask separately before any visual/game launch.
- Ask separately before killing Wine/Steam/backend/processes.

### 1. Verify git baseline

```bash
cd /Users/alexmondello/metalsharp-m12-lab

git rev-parse HEAD
git ls-remote origin refs/heads/fix/m12-shader-probe-lab
git status --branch --short
```

Required:

- local HEAD is `0573c54caaca70fd384f5f23becb14d7007bd6d4`
- origin branch is the same commit
- worktree has no unexpected tracked changes before rebuild/stage

### 2. Backup mutable runtime state

Use a timestamped backup root under `/tmp`, for example:

```bash
BACKUP=/tmp/metalsharp-m12-slice2-rebuild-backup-$(date +%Y%m%d-%H%M%S)
mkdir -p "$BACKUP"
```

Back up at minimum:

- `vendor/dxmt/build-metalsharp-x64`
- `~/.metalsharp/runtime/wine/lib/dxmt_m12`
- AC6 game-local M12 DLLs
- `app/src-rust/target/release/metalsharp-backend`
- any `mscompatdb` files found under `~/.metalsharp/runtime/wine`

### 3. Clean rebuild Slice-2 DXMT/M12 runtime

```bash
cd /Users/alexmondello/metalsharp-m12-lab
rm -rf vendor/dxmt/build-metalsharp-x64
tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime
```

If the build does not produce `d3d11.dll` or `d3d10core.dll`, build them explicitly in the same Meson/Ninja build dir before staging. Do not stage a partial runtime.

### 4. Stage only to `dxmt_m12`

```bash
tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime
```

Then verify/copy the full deployed DLL set into:

- `~/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-windows`
- AC6 game dir

Required Windows DLL set:

- `d3d12.dll`
- `d3d11.dll`
- `dxgi.dll`
- `dxgi_dxmt.dll`
- `d3d10core.dll`
- `winemetal.dll`
- preserve `nvapi64.dll` / `nvngx.dll` if needed from the previous valid runtime backup

Required Unix sidecar set:

- `winemetal.so`
- `libm12core.dylib`

### 5. Quarantine `mscompatdb`

Before launch verification, active runtime must not expose `mscompatdb.so`.

Check:

```bash
find "$HOME/.metalsharp/runtime/wine" -name 'mscompatdb*' -print
```

Quarantine any active files into the backup root, preserving paths. Do not delete without a backup.

Known bad active paths include:

- `~/.metalsharp/runtime/wine/lib/wine/x86_64-unix/mscompatdb.so`
- `~/.metalsharp/runtime/wine/etc/mscompatdb_rules.toml`
- `~/.metalsharp/runtime/wine/etc/etc/mscompatdb_rules.toml`

### 6. Rebuild backend from Slice-2 source

```bash
cd /Users/alexmondello/metalsharp-m12-lab/app/src-rust
cargo build --release
shasum -a 256 target/release/metalsharp-backend
```

Expected backend hash for the stable rebuilt baseline:

```text
6fdface8f06740091d0047669ae6abf91ddd8088606ec2ad458ba978b04a7c0b
```

### 7. Restart only the PR backend on 9277

Use `METALSHARP_PORT`, not `PORT`.

```bash
cd /Users/alexmondello/metalsharp-m12-lab/app/src-rust
nohup env METALSHARP_PORT=9277 METALSHARP_HOME="$HOME/.metalsharp" \
  ./target/release/metalsharp-backend \
  > /tmp/metalsharp-m12-slice2-backend-9277.log 2>&1 < /dev/null &
echo $! > /tmp/metalsharp-m12-slice2-backend-9277.pid
```

Verify:

```bash
curl -sS --max-time 3 http://127.0.0.1:9277/status
lsof -nP -iTCP:9277 -sTCP:LISTEN
```

Expected:

- JSON has `ok: true`
- `pid` is the new backend PID
- listener is `127.0.0.1:9277`

If a previous backend is already listening on 9277, do not kill it unless the operator approved replacing that exact backend process.

### 8. Verify hash sync

For each Windows DLL, hash these three paths and require all three to match:

1. build output under `vendor/dxmt/build-metalsharp-x64/src/...`
2. M12 runtime lane under `~/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-windows`
3. AC6 game directory

For each Unix sidecar, hash these two paths and require both to match:

1. build output under `vendor/dxmt/build-metalsharp-x64/src/...`
2. M12 runtime lane under `~/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-unix`

Do not launch if any hash is missing or mismatched.

### 9. Verify no-log effective launch env without launching

Use the backend dry-run as the base route/env source, then apply the exact POST overrides mentally or via a verifier script.

Dry-run base:

```bash
curl -sS --max-time 20 \
  'http://127.0.0.1:9277/diagnostics/pipeline/dry-run?appid=1888160&pipeline=m12'
```

Required effective env after POST overrides:

```text
WINEDLLOVERRIDES=winemetal,d3d12,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d
WINEDLLPATH=.../.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-windows
DYLD_LIBRARY_PATH starts with .../.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-unix
DYLD_FALLBACK_LIBRARY_PATH starts with .../.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-unix
DXMT_WINEMETAL_UNIXLIB=winemetal.so
DXMT_M12CORE_ENABLE=1
DXMT_M12CORE_REQUIRED=0
DXMT_LOG_LEVEL=none
DXMT_LOG_PATH=none
DXMT_D3D12_TRACE=0
DXMT_WINEMETAL_DEBUG=0
DXMT_D3D12_PSO_TRACE=0
DXMT_D3D12_TRACE_MAX_MB=0
DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE=0
```

Required negative check:

```text
WINEDLLOVERRIDES must not contain mscompatdb
```

### 10. Run preflight

```bash
M12_DEV_RESULTS_DIR="$BACKUP/results-final" \
  tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight
```

Required passes:

- D3D12 Metal SDK contracts
- M12 pipeline contract
- runtime preflight
- shader engine contract

### 11. Launch only after explicit approval

Use the one-shot launch command above.

Immediately capture response and process status, but do not kill or restart anything unless explicitly approved.

### 12. Post-launch sanity check

From the launch response, inspect:

- PID
- launch log path
- recipe exe path
- recipe launch args
- env override list

Then:

```bash
ps -p "$PID" -o pid,ppid,state,etime,command
sleep 2
ps -p "$PID" -o pid,ppid,state,etime,command
```

Launch log should contain route identity and should not show `mscompatdb` loaded. Stable baseline accepted `mscompatdb: not found` because the sidecar was absent.

## Shell script roadmap

The goal is to split dangerous actions into small scripts with explicit `--yes-*` gates. No script should silently rebuild, stage, restart backend, and launch all at once.

### 1. `m12-slice2-env.sh`

Purpose: shared constants only; no mutation.

Exports:

```bash
REPO=/Users/alexmondello/metalsharp-m12-lab
APPID=1888160
BACKEND_URL=http://127.0.0.1:9277
METALSHARP_PORT=9277
GAME_DIR=/Volumes/AverySSD/SteamLibrary/steamapps/common/ARMORED CORE VI FIRES OF RUBICON/Game
GAME_EXE=start_protected_game.exe
REQUIRED_HEAD=0573c54caaca70fd384f5f23becb14d7007bd6d4
M12_RUNTIME_WIN=$HOME/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-windows
M12_RUNTIME_UNIX=$HOME/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-unix
```

Rules:

- Must not run commands except simple path normalization.
- Must be sourced by all other scripts.

### 2. `m12-slice2-verify-git.sh`

Purpose: read-only baseline check.

Checks:

- cwd/repo exists
- current branch is expected branch
- local HEAD equals required Slice-2 commit
- origin branch equals same commit
- tracked worktree status is either clean or explicitly allowed via `--allow-dirty-docs`

Exit nonzero on mismatch.

### 3. `m12-slice2-backup-runtime.sh`

Purpose: backup mutable state before any rebuild/stage/quarantine.

Inputs:

- optional `--backup-root PATH`

Outputs:

- timestamped backup root
- manifest file with paths and hashes

Backs up:

- existing DXMT build dir
- `dxmt_m12` runtime lane
- AC6 game-local DLLs
- backend binary
- any `mscompatdb*` runtime files

Rules:

- No deletion.
- No launch.
- Prints `BACKUP_ROOT=...` for downstream scripts.

### 4. `m12-slice2-rebuild-runtime.sh`

Purpose: clean rebuild only.

Required gates:

- `m12-slice2-verify-git.sh` passes
- `--yes-clean-build-dir`

Actions:

- remove `vendor/dxmt/build-metalsharp-x64`
- run `m12-dev.sh build-runtime`
- ensure/build `d3d11.dll` and `d3d10core.dll` if missing
- hash build outputs

Rules:

- Does not stage runtime.
- Does not touch game dir.
- Does not restart backend.
- Does not launch.

### 5. `m12-slice2-stage-runtime.sh`

Purpose: stage rebuilt artifacts into M12 runtime lane and game dir.

Required gates:

- backup root exists
- build artifacts exist
- `--yes-stage-runtime`
- optional `--yes-copy-game-local`

Actions:

- run `m12-dev.sh stage-runtime`
- copy full DLL set to `dxmt_m12/x86_64-windows`
- copy full DLL set to AC6 game dir only if game-local gate is present
- copy unix sidecars to `dxmt_m12/x86_64-unix`
- preserve/restore `nvapi64.dll` and `nvngx.dll` if needed from backup

Rules:

- Hard fail if destination path contains `/lib/dxmt/` instead of `/lib/dxmt_m12/`.
- Hard fail if any deployed artifact is missing.
- Does not launch.

### 6. `m12-slice2-quarantine-mscompatdb.sh`

Purpose: remove `mscompatdb` from the active route safely.

Required gates:

- backup root exists
- `--yes-quarantine-mscompatdb`

Actions:

- find `mscompatdb*` under `~/.metalsharp/runtime/wine`
- move active files into backup root preserving relative path
- emit manifest

Rules:

- Never delete without backup.
- Does not touch prefix state.
- Does not launch.

### 7. `m12-slice2-verify-runtime.sh`

Purpose: read-only full runtime/game verification.

Checks:

- required artifacts exist
- build/runtime/game hashes match for Windows DLLs
- build/runtime hashes match for Unix sidecars
- hashes match known stable rebuilt table if `--require-known-hashes` is provided
- `mscompatdb.so` absent from active Wine unix route
- active `dxmt_m12` lane is used, not generic `dxmt`

Rules:

- Read-only.
- Nonzero exit on any mismatch.

### 8. `m12-slice2-build-backend.sh`

Purpose: rebuild backend only.

Required gates:

- git check passes

Actions:

- `cargo build --release`
- hash `target/release/metalsharp-backend`

Rules:

- Does not restart backend.
- Does not launch.

### 9. `m12-slice2-backend-9277.sh`

Purpose: manage only the PR backend on port 9277.

Subcommands:

- `status`: read-only status/lsof
- `start`: start rebuilt backend if nothing is listening
- `replace --yes-kill-9277`: kill only the process currently listening on 9277, then start rebuilt backend

Rules:

- Uses `METALSHARP_PORT=9277`.
- Must not use `PORT=9277`.
- Must not kill Steam/Wine/wineserver/game processes.
- Must not kill another backend unless `--yes-kill-9277` is present and the exact PID is printed before action.

### 10. `m12-slice2-verify-launch-env.sh`

Purpose: read-only launch-route/env verifier.

Actions:

- call `/diagnostics/pipeline/dry-run?appid=1888160&pipeline=m12`
- apply the exact POST no-log override mapping in memory
- assert effective env invariants
- assert launch route recipe fields

Checks:

- dry-run ok
- pipeline m12
- missing list empty
- `WINEDLLPATH` uses `dxmt_m12/x86_64-windows`
- `DYLD_LIBRARY_PATH` starts with `dxmt_m12/x86_64-unix`
- `DXMT_WINEMETAL_UNIXLIB=winemetal.so`
- no `mscompatdb` override
- no-log/no-trace env is effective
- M12Core enabled, required off

Rules:

- Does not launch.

### 11. `m12-slice2-preflight.sh`

Purpose: canonical preflight wrapper.

Actions:

- run `m12-dev.sh preflight` with result dir under backup root or `/tmp`
- summarize pass/fail

Rules:

- Does not launch.
- Does not write repo result artifacts by default.

### 12. `m12-ac6-launch-canonical.sh`

Purpose: launch only after everything passed and operator explicitly approves.

Required gates:

- `--yes-launch-ac6`
- backend healthy
- runtime verification passed recently
- launch env verification passed recently
- preflight passed recently

Actions:

- POST exact canonical payload to `/steam/launch-game`
- save request/response under `/tmp`, not repo
- print PID/log path/recipe/env override summary
- run short process survival check

Rules:

- No retries by default.
- No kill on failure.
- No debugger attach.
- No trace/log mutation.
- No source compile unless operator explicitly changes the payload.

### 13. `m12-ac6-postlaunch-check.sh`

Purpose: inspect only.

Inputs:

- PID or launch response JSON

Actions:

- `ps` survival check
- launch log tail
- check for `mscompatdb: loaded` vs `mscompatdb: not found`
- optionally show relevant child processes

Rules:

- No kill/restart.
- No log-heavy tracing.

## Recommended top-level operator flow

Once scripts exist, the safe full flow should look like this:

```bash
source tools/d3d12-metal-sdk/scripts/m12-slice2-env.sh

tools/d3d12-metal-sdk/scripts/m12-slice2-verify-git.sh
BACKUP_ROOT=$(tools/d3d12-metal-sdk/scripts/m12-slice2-backup-runtime.sh | awk -F= '/^BACKUP_ROOT=/{print $2}')

tools/d3d12-metal-sdk/scripts/m12-slice2-rebuild-runtime.sh --yes-clean-build-dir
tools/d3d12-metal-sdk/scripts/m12-slice2-stage-runtime.sh --backup-root "$BACKUP_ROOT" --yes-stage-runtime --yes-copy-game-local
tools/d3d12-metal-sdk/scripts/m12-slice2-quarantine-mscompatdb.sh --backup-root "$BACKUP_ROOT" --yes-quarantine-mscompatdb

tools/d3d12-metal-sdk/scripts/m12-slice2-verify-runtime.sh --require-known-hashes
tools/d3d12-metal-sdk/scripts/m12-slice2-build-backend.sh
tools/d3d12-metal-sdk/scripts/m12-slice2-backend-9277.sh status
# If needed, and only with approval:
# tools/d3d12-metal-sdk/scripts/m12-slice2-backend-9277.sh replace --yes-kill-9277

tools/d3d12-metal-sdk/scripts/m12-slice2-verify-launch-env.sh
tools/d3d12-metal-sdk/scripts/m12-slice2-preflight.sh --backup-root "$BACKUP_ROOT"

# Only after explicit launch approval:
tools/d3d12-metal-sdk/scripts/m12-ac6-launch-canonical.sh --yes-launch-ac6
```

## Regression tripwires

Any one of these should stop the flow immediately:

- HEAD/origin differs from the Slice-2 commit.
- Build artifacts are missing.
- Runtime lane is `lib/dxmt` instead of `lib/dxmt_m12`.
- Any build/runtime/game hash mismatch exists.
- `mscompatdb.so` exists on active runtime route.
- `WINEDLLOVERRIDES` contains `mscompatdb`.
- `DXMT_D3D12_TRACE=1` or `DXMT_WINEMETAL_DEBUG=1` in effective launch env for the visual no-log run.
- `DXMT_M12CORE_REQUIRED=1` unless explicitly testing required-mode.
- Backend is started with `PORT=9277` instead of `METALSHARP_PORT=9277`.
- `/mtsp/prepare` appears in the launch path.
- The launch request is not `POST /steam/launch-game`.
- The launch response recipe exe is not `start_protected_game.exe`.
- The launch response args do not include the stable windowed 1280x720 args.
- Launch log contains `mscompatdb: loaded`.

## Commit hygiene

This runbook and future shell scripts are source artifacts and may be committed only with explicit approval.

Never commit:

- `.metallib`
- `.air`
- DXBC blobs
- raw D3DMetal payloads
- shader caches
- large corpora
- launch logs
- generated runtime DLLs/dylibs/so files
- `/tmp` backup manifests copied wholesale into the repo

Keep generated evidence in `/tmp` or another explicitly ignored results directory.
