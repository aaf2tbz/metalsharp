# MetalSharp C backend

This directory is the checked-in, Apple Silicon C source of truth for the
shipped backend and its tests. `runtime/c` contains the 374 C translation
units selected by the backend link manifest; `tests/c` contains the 394 units
for the 629-test executable. `native/objects` contains the pinned arm64 C
objects for bundled SQLite and zstd.

Build and verify with Xcode command-line tools only:

```sh
make -C app/src-c verify
```

This command invokes Clang directly. It does not invoke Cargo, `rustc`, or a
Rust runtime. The generated source came from the pinned compiler integration
in `tools/rustc-c`; that integration is retained strictly for explicit future
regeneration and is not used by CI, release, or packaging.
