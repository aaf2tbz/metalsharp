#!/bin/bash
set -e
cd "$(dirname "$0")"
clang -shared -arch arm64 -o libfmod.dylib fmod_stub.c -install_name @loader_path/libfmod.dylib
clang -shared -arch arm64 -o libfmodstudio.dylib fmodstudio_stub.c -install_name @loader_path/libfmodstudio.dylib
codesign --force -s - libfmod.dylib libfmodstudio.dylib
echo "Built FMOD stubs"
