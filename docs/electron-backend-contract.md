# Electron ↔ backend contract

[`contracts/electron-backend.v1.json`](../contracts/electron-backend.v1.json) is the authoritative, implementation-independent boundary between Electron and the MetalSharp backend. It defines the compatibility version, status handshake, typed high-value routes, and the isolated conformance fixtures that every shipped backend must pass.

Electron requires contract v1. New backends report it as `contract_version` in `GET /status`; the initial direct-C backend (`0.54.5`) is explicitly mapped as the v1 baseline because it was shipped immediately before the field existed. Any backend without that field or an approved legacy mapping is rejected before renderer requests are forwarded.

Run the suite against an executable with:

```sh
python3 tools/contracts/validate-electron-backend-contract.py
python3 tools/contracts/run-electron-backend-conformance.py app/build/c-backend/metalsharp-backend
```

The runner creates a fresh `METALSHARP_HOME`, starts one backend artifact, and exercises status, setup, Steam, bottles, logs/streaming, and dry-run diagnostic flows without Steam credentials, a Wine prefix, or a game installation. `make -C app/src-c verify` runs it for the committed C backend in CI.

Add a new contract version for breaking wire changes. First add its fixtures and Electron types, then update the backend and packaging together; do not silently extend the legacy-version map.
