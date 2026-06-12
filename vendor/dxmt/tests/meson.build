dxil_converter_test = executable('test_dxil_converter',
  [
    'dxil/test_dxil_converter.cpp',
    '../src/airconv/dxil/dxil_container.cpp',
    '../src/airconv/dxil/dxil_ir.cpp',
    '../src/airconv/dxil/llvm_bitcode.cpp',
    '../src/airconv/dxil/dxil_to_msl.cpp',
    '../src/airconv/dxil/msl_lowering.cpp',
  ],
  include_directories : [ dxmt_include_path, include_directories('../src') ],
  native : true,
)

test('dxil_converter', dxil_converter_test)

d3d12_vertex_contract_test = executable('test_d3d12_vertex_contract',
  [
    'dxil/test_d3d12_vertex_contract.cpp',
  ],
  include_directories : [ dxmt_include_path, dxmt_native_include_path ],
  native : true,
)

test('d3d12_vertex_contract', d3d12_vertex_contract_test)

subdir('d3d12_game')
if fs.is_file('tests/dx11/cube.obj') and fs.is_file('tests/dx11/test.png')
  subdir('dx11')
else
  message('Skipping dx11 samples: cube.obj/test.png assets are not present')
endif
