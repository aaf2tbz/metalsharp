# Self-review: Phase 3 SM50 reflection compatibility

- Scope check: `libm12core` now owns SM50 shader reflection and `SM50GetArgumentsInfo` extraction for non-DXIL bytecode.
- Boundary check: reflection/argument data crosses the PE/native boundary as POD `M12CoreSM50*` structs; no SM50 shader handles or C++ objects cross.
- Fallback check: D3D12 still falls back to the old `m_*_shader` + `SM50GetArgumentsInfo` path if unixcall `143` or `libm12core` is unavailable.
- DXIL guard check: D3D12 skips SM50 reflection ABI for DXIL bytecode, avoiding expected `SM50Initialize` failures and unnecessary runtime overhead.
- Phase boundary check: this does not move root/descriptor binding plan ownership; that remains Phase 5.
- Validation check: build/stage/preflight passed, direct native x86_64 SM50 reflection probe passed on a legacy DXBC payload, AC6 120s strict-hash run rendered with no failure buckets.
