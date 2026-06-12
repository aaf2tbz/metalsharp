lib_d3dcompiler = cpp.find_library('d3dcompiler_47')
lib_dxgi = cpp.find_library('dxgi')

m12_game = executable('m12_game',
  'm12_game.cpp',
  dependencies: [ d3d12_dep, lib_dxgi, lib_d3dcompiler ],
  link_args: ['-static', '-mconsole'],
  install: false,
)
