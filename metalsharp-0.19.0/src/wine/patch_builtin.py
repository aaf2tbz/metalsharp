import sys, os

sig = b'Wine builtin DLL\x00' + b'\x00' * 15
lib_dir = sys.argv[1]

for dll in ['d3d9.dll', 'd3d11.dll', 'dxgi.dll', 'mscoree32.dll', 'mscoree.dll']:
    path = os.path.join(lib_dir, dll)
    if not os.path.exists(path):
        print(f'WARNING: {path} not found, skipping')
        continue
    with open(path, 'r+b') as f:
        f.seek(0x40)
        f.write(sig)
    print(f'Patched {dll}')
