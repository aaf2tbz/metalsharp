# MetalSharp C backend

This directory is the checked-in, Apple Silicon C source of truth for the
backend and its tests. `runtime/c` contains the 374 C translation
units selected by the backend link manifest; `tests/c` contains the 394 units
for the 629-test executable. `native/objects` contains the pinned arm64 C
objects for bundled SQLite and zstd.

Build and verify with Xcode command-line tools only:

```sh
make -C app/src-c verify
```

This command invokes Clang directly. It does not invoke Cargo, `rustc`, a Rust
runtime, or a Rust-to-C generator. The repository intentionally contains no
`.rs` sources. `make verify` rejects reintroducing one, checks all 45 backend
modules against the committed C symbols, and runs all 629 converted tests.

Installer bundle policy is C-owned in `installer.c`. Rebuilt M12 files are
validated by complete runtime layout and the release bundle manifest rather
than compiler-output hashes embedded in the executable.
