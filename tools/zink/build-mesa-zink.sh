#!/usr/bin/env bash
set -euo pipefail

MESA_VERSION="${MESA_VERSION:-mesa-25.3.6}"
MESA_SOURCE_DIR="${MESA_SOURCE_DIR:-/tmp/mesa-zink-build/mesa-src}"
BUILD_BASE="${BUILD_BASE:-/tmp/mesa-zink-build}"
OUTPUT_DIR="${OUTPUT_DIR:-$(dirname "$0")/../../app/bundles/zink-staging}"
ARCHS="${ARCHS:-x86_64 i686}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

REQUIRED_TOOLS=(meson ninja python3 flex pkg-config zstd git)
REQUIRED_PYTHON=(mako)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[zink]${NC} $*"; }
warn()  { echo -e "${YELLOW}[zink]${NC} $*"; }
error() { echo -e "${RED}[zink]${NC} $*" >&2; exit 1; }

check_bison() {
    local bison_bin
    bison_bin="$(command -v bison 2>/dev/null || true)"
    if [ -z "$bison_bin" ]; then
        error "bison not found. Install with: brew install bison && brew link bison --force"
    fi
    local version
    version="$("$bison_bin" --version | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)"
    local major="${version%%.*}"
    local minor="${version#*.}"
    if [ "$major" -lt 3 ] || { [ "$major" -eq 3 ] && [ "$minor" -lt 2 ]; }; then
        error "bison $version is too old (need 3.2+). Fix: brew install bison && brew link bison --force"
    fi
    info "bison $version OK"
}

check_toolchain() {
    info "Checking toolchain..."
    for tool in "${REQUIRED_TOOLS[@]}"; do
        command -v "$tool" >/dev/null 2>&1 || error "$tool not found in PATH"
    done
    check_bison

    for mod in "${REQUIRED_PYTHON[@]}"; do
        python3 -c "import $mod" 2>/dev/null || error "Python module '$mod' not found. Fix: pip3 install $mod"
    done

    if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
        error "MinGW x86_64 cross-compiler not found. Fix: brew install mingw-w64"
    fi
    local gcc_ver
    gcc_ver="$(x86_64-w64-mingw32-gcc --version | head -1)"
    info "MinGW GCC: $gcc_ver"

    if [ "$(uname -m)" = "arm64" ]; then
        info "Building on ARM64 macOS — cross-compiling to x86_64-w64-mingw32 (no Rosetta needed for host)"
    fi
}

fetch_mesa() {
    if [ -d "$MESA_SOURCE_DIR/.git" ]; then
        info "Mesa source exists at $MESA_SOURCE_DIR, fetching..."
        git -C "$MESA_SOURCE_DIR" fetch --tags origin
    else
        info "Cloning Mesa repository..."
        mkdir -p "$(dirname "$MESA_SOURCE_DIR")"
        git clone --filter=blob:none https://gitlab.freedesktop.org/mesa/mesa.git "$MESA_SOURCE_DIR"
    fi

    info "Checking out $MESA_VERSION..."
    git -C "$MESA_SOURCE_DIR" checkout "$MESA_VERSION"
}

build_spirv_tools_for_mingw() {
    local triplet="$1"
    local build_dir="$BUILD_BASE/spirv-tools-${triplet}"

    if [ -d "$build_dir/install" ]; then
        info "SPIRV-Tools already built for $triplet, skipping"
        return
    fi

    info "Building SPIRV-Tools for $triplet..."
    local src_base="$BUILD_BASE/spirv-src"
    mkdir -p "$src_base"

    if [ ! -d "$src_base/SPIRV-Tools" ]; then
        git clone --filter=blob:none https://github.com/KhronosGroup/SPIRV-Tools.git "$src_base/SPIRV-Tools"
    fi
    if [ ! -d "$src_base/SPIRV-Headers" ]; then
        git clone --filter=blob:none https://github.com/KhronosGroup/SPIRV-Headers.git "$src_base/SPIRV-Headers"
    fi

    local spirv_tools_src="$src_base/SPIRV-Tools"
    local spirv_headers_src="$src_base/SPIRV-Headers"

    local toolchain_file="$BUILD_BASE/toolchain-${triplet}.cmake"
    cat > "$toolchain_file" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ${triplet%%-*})
set(CMAKE_C_COMPILER ${triplet}-gcc)
set(CMAKE_CXX_COMPILER ${triplet}-g++)
set(CMAKE_RC_COMPILER ${triplet}-windres)
set(CMAKE_FIND_ROOT_PATH /usr/local/opt/mingw-w64 /opt/homebrew/Cellar/mingw-w64)
EOF

    rm -rf "$build_dir/build"
    mkdir -p "$build_dir/build"
    cmake -S "$spirv_tools_src" -B "$build_dir/build" \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" \
        -DCMAKE_BUILD_TYPE=Release \
        -DSPIRV-Headers_SOURCE_DIR="$spirv_headers_src" \
        -DSPIRV_SKIP_TESTS=ON \
        -DSPIRV_TOOLS_BUILD_STATIC=ON \
        -DCMAKE_INSTALL_PREFIX="$build_dir/install"
    cmake --build "$build_dir/build" -j"$JOBS"
    cmake --install "$build_dir/build"
}

build_vulkan_headers_for_mingw() {
    local triplet="$1"
    local build_dir="$BUILD_BASE/vulkan-headers-${triplet}"

    if [ -d "$build_dir/install" ]; then
        info "Vulkan-Headers already built for $triplet, skipping"
        return
    fi

    info "Installing Vulkan-Headers for $triplet (header-only)..."
    local src_base="$BUILD_BASE/vulkan-src"
    mkdir -p "$src_base"

    if [ ! -d "$src_base/Vulkan-Headers" ]; then
        git clone --filter=blob:none https://github.com/KhronosGroup/Vulkan-Headers.git "$src_base/Vulkan-Headers"
    fi

    mkdir -p "$build_dir/build"
    cmake -S "$src_base/Vulkan-Headers" -B "$build_dir/build" \
        -DCMAKE_TOOLCHAIN_FILE="$BUILD_BASE/toolchain-${triplet}.cmake" \
        -DCMAKE_INSTALL_PREFIX="$build_dir/install"
    cmake --build "$build_dir/build" --target install
}

build_zink_for_arch() {
    local arch="$1"
    local triplet

    case "$arch" in
        x86_64) triplet="x86_64-w64-mingw32" ;;
        i686)   triplet="i686-w64-mingw32" ;;
        *)      error "Unsupported arch: $arch" ;;
    esac

    local build_dir="$BUILD_BASE/mesa-${arch}"
    local cross_file="$BUILD_BASE/cross-${triplet}.ini"

    info "Writing cross-file for $triplet..."
    cat > "$cross_file" <<EOF
[binaries]
c = '${triplet}-gcc'
cpp = '${triplet}-g++'
ar = '${triplet}-ar'
strip = '${triplet}-strip'
pkgconfig = 'pkg-config'
windres = '${triplet}-windres'

[host_machine]
system = 'windows'
cpu_family = '${arch}'
cpu = '${arch}'
endian = 'little'

[paths]
prefix = '/tmp/mesa-zink-build/install-${arch}'
EOF

    build_spirv_tools_for_mingw "$triplet"
    build_vulkan_headers_for_mingw "$triplet"

    local spirv_prefix="$BUILD_BASE/spirv-tools-${triplet}/install"
    local vulkan_prefix="$BUILD_BASE/vulkan-headers-${triplet}/install"

    local extra_cflags="-I${vulkan_prefix}/include -I${spirv_prefix}/include"
    local extra_ldflags="-L${spirv_prefix}/lib"
    local pkg_config_path="${spirv_prefix}/lib/pkgconfig:${vulkan_prefix}/lib/pkgconfig"

    info "Configuring Mesa for $arch ($triplet)..."
    rm -rf "$build_dir"

    PKG_CONFIG_PATH="$pkg_config_path" \
    CFLAGS="$extra_cflags" \
    CXXFLAGS="$extra_cflags" \
    LDFLAGS="$extra_ldflags" \
    meson setup "$build_dir" "$MESA_SOURCE_DIR" \
        --cross-file "$cross_file" \
        --buildtype=release \
        -Dplatforms=windows \
        -Dgallium-drivers=zink \
        -Dvulkan-drivers=[] \
        -Dopengl=true \
        -Dgles1=disabled \
        -Dgles2=disabled \
        -Dglx=disabled \
        -Degl=disabled \
        -Dllvm=disabled \
        -Dshared-llvm=disabled \
        -Dgbm=disabled \
        -Dgallium-va=disabled \
        -Dgallium-d3d10umd=false \
        -Dgallium-rusticl=false \
        -Dmicrosoft-clc=disabled \
        -Dspirv-to-dxil=false \
        -Dbuild-tests=false \
        -Dxmlconfig=disabled \
        --wrap-mode=nodownload

    info "Applying Wine/MoltenVK compatibility patches..."
    local zink_screen="$MESA_SOURCE_DIR/src/gallium/drivers/zink/zink_screen.c"
    if grep -q "Zink requires the nullDescriptor feature" "$zink_screen" 2>/dev/null; then
        sed -i.bak '/Zink requires the nullDescriptor feature/,/goto fail;/{
            s/mesa_loge/mesa_logw/
            s/Zink requires the nullDescriptor feature of KHR\/EXT robustness2./Zink: nullDescriptor not reported (likely winevulkan thunk issue); proceeding anyway./
            /goto fail;/d
            /nullDescriptor/c   if (!screen->info.rb2_feats.nullDescriptor) {\n      screen->info.rb2_feats.nullDescriptor = VK_TRUE;\n   }
        }' "$zink_screen"
        info "Patched nullDescriptor check in zink_screen.c"
    else
        info "nullDescriptor patch already applied or not needed"
    fi

    info "Compiling Mesa Zink for $arch..."
    meson compile -C "$build_dir" -j"$JOBS"

    info "Extracting DLLs for $arch..."
    local staging="$OUTPUT_DIR/zink/${arch}-windows"
    mkdir -p "$staging"

    local opengl32="$build_dir/src/gallium/targets/libgl-gdi/opengl32.dll"
    local libgallium_wgl="$build_dir/src/gallium/targets/wgl/libgallium_wgl.dll"

    if [ ! -f "$opengl32" ]; then
        opengl32="$build_dir/src/gallium/targets/wgl/opengl32.dll"
    fi

    [ -f "$opengl32" ] || error "opengl32.dll not found in build output for $arch"
    [ -f "$libgallium_wgl" ] || warn "libgallium_wgl.dll not found for $arch (may be statically linked)"

    cp "$opengl32" "$staging/"
    [ -f "$libgallium_wgl" ] && cp "$libgallium_wgl" "$staging/"

    local mingw_sysroot
    case "$arch" in
        x86_64) mingw_sysroot="$(dirname "$(command -v x86_64-w64-mingw32-gcc)")/../x86_64-w64-mingw32" ;;
        i686)   mingw_sysroot="$(dirname "$(command -v i686-w64-mingw32-gcc)")/../i686-w64-mingw32" ;;
    esac
    if [ -f "$mingw_sysroot/bin/libwinpthread-1.dll" ]; then
        cp "$mingw_sysroot/bin/libwinpthread-1.dll" "$staging/"
        info "Copied libwinpthread-1.dll from $mingw_sysroot"
    else
        warn "libwinpthread-1.dll not found in $mingw_sysroot — Zink may fail to load"
    fi

    info "Built $arch: $(ls -lh "$staging/")"
}

package_zink() {
    local tar_name="mesa-zink.tar.zst"
    local staging="$OUTPUT_DIR"

    if [ ! -d "$staging/zink" ]; then
        error "No zink build output found at $staging/zink"
    fi

    info "Packaging $tar_name..."
    (cd "$staging" && tar --use-compress-program="zstd -19 -T0" -cf "$tar_name" zink/)

    info "Output: $staging/$tar_name"
    info "Size: $(du -h "$staging/$tar_name" | cut -f1)"
    info ""
    info "To install into MetalSharp runtime:"
    info "  tar --use-compress-program=unzstd -xf $staging/$tar_name -C ~/.metalsharp/runtime/wine/lib/"
}

main() {
    local action="${1:-build}"
    case "$action" in
        check)
            check_toolchain
            info "All toolchain checks passed"
            ;;
        fetch)
            fetch_mesa
            info "Mesa source ready at $MESA_SOURCE_DIR"
            ;;
        build)
            check_toolchain
            fetch_mesa
            for arch in $ARCHS; do
                build_zink_for_arch "$arch"
            done
            package_zink
            info "Build complete!"
            ;;
        package)
            package_zink
            ;;
        clean)
            rm -rf "$BUILD_BASE"
            info "Cleaned $BUILD_BASE"
            ;;
        *)
            echo "Usage: $0 {check|fetch|build|package|clean}"
            echo ""
            echo "Environment variables:"
            echo "  MESA_VERSION     Mesa tag/branch (default: mesa-25.3.6)"
            echo "  MESA_SOURCE_DIR  Local mesa clone (default: /tmp/mesa-zink-build/mesa-src)"
            echo "  BUILD_BASE       Build directory (default: /tmp/mesa-zink-build)"
            echo "  OUTPUT_DIR       Staging output (default: tools/zink/../../app/bundles/zink-staging)"
            echo "  ARCHS            Space-separated arch list (default: x86_64)"
            echo "  JOBS             Parallel jobs (default: \$(sysctl -n hw.ncpu))"
            exit 1
            ;;
    esac
}

main "$@"
