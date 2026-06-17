m12core_src = [
  'm12core.cpp',
]

# Native macOS shared library used by the future unified M12 runtime.  This is
# intentionally built as a native target, not a PE DLL.  PE-side D3D12/DXGI code
# must reach it through stable C ABI thunks rather than C++ object sharing.
m12core_dylib = shared_library('m12core', m12core_src,
  name_prefix      : 'lib',
  include_directories : dxmt_include_path,
  native           : true,
  build_by_default : true,
  install_rpath    : '',
  build_rpath      : '@loader_path/',
)
