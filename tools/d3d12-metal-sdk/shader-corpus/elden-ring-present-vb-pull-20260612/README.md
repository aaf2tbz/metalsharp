# Elden Ring M12 Present VB Pull Shader Corpus

This corpus captures the clean M12 shader set from the Elden Ring `start_protected_game.exe`
present-path run taken on 2026-06-12 after the present vertex-buffer pull work.

It is checked in as a shader-engine fixture so M12 changes can be inspected and replayed
offline without launching the game.

## Contents

- `msl/`: 217 generated Metal Shading Language files captured from the run.
- `air/`: 217 AIR intermediates compiled from `msl/` with `xcrun metal -x metal -c`.
- `metallib/`: 217 metallibs from the offline compile pass.
- `proof/`: capture summaries, compile summaries, failure index, and SHA-256 checksums.

## Verification

The original offline metallib proof for this capture reported:

```text
ok=217
fail=0
captured_msl=217
compiled_metallib=217
```

The checked-in AIR intermediates were regenerated from the checked-in MSL with:

```sh
xcrun metal -x metal -c <shader>.msl -o <shader>.air
```

The regenerated AIR proof is in `proof/air-compile-summary.txt` and should end with:

```text
ok=217
fail=0
captured_msl=217
compiled_air=217
```
