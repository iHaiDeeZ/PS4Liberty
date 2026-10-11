#!/bin/bash
# Builds the overlay toolchain ($OO_PS4_TOOLCHAIN) and fake LLVM prefix used by the PS4 build,
# and stages the extracted game files. Safe to re-run.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/ps4-env.sh"

OO="$OO_PS4_TOOLCHAIN"
REPO_OO="$LR_SRC/tools/OpenOrbis-PS4-Toolchain"
rm -rf "$OO" "$LR_ROOT/llvm"
mkdir -p "$OO/lib" "$OO/include" "$OO/bin" "$LR_ROOT/llvm/bin" "$LR_ROOT/llvm/include"

# --- Overlay toolchain: PacBrew OpenOrbis + repo scripts + DolphinPS4 libc++ ---
for f in "$OPENORBIS"/*; do
    case "$(basename "$f")" in lib|include|bin) ;; *) ln -s "$f" "$OO/" ;; esac
done
for f in "$OPENORBIS"/lib/*;     do ln -s "$f" "$OO/lib/"; done
for f in "$OPENORBIS"/bin/*;     do ln -s "$f" "$OO/bin/"; done
for f in "$OPENORBIS"/include/*; do
    [ "$(basename "$f")" = "c++" ] || ln -s "$f" "$OO/include/"
done
# PS4 __config_site overlay dir the toolchain puts first on the C++ include path.
mkdir -p "$OO/include/c++/v1"
cp "$DOLPHIN_SYSROOT/include/c++/v1/__config_site" "$OO/include/c++/v1/__config_site"
# Python helpers referenced by LibertyRecomp's CMake (make_fself.py etc.) live in the repo copy.
ln -s "$REPO_OO/scripts" "$OO/scripts"
# libcxx_ps4_compat.a = LLVM 21 libc++ + libc++abi compiled for PS4 (from DolphinPS4).
(
    cd "$(mktemp -d)"
    printf 'CREATE %s\nADDLIB %s\nADDLIB %s\nSAVE\nEND\n' \
        "$OO/lib/libcxx_ps4_compat.a" "$DOLPHIN_SYSROOT/lib/libc++.a" "$DOLPHIN_SYSROOT/lib/libc++abi.a" \
        | llvm-ar-21 -M
)

# --- Fake LLVM prefix: host clang 21 + DolphinPS4 libc++ headers ---
for t in clang clang++ ld.lld lld llvm-ar llvm-ranlib llvm-nm llvm-objcopy llvm-strip; do
    real="$(command -v "$t-21" || command -v "$t")"
    ln -s "$real" "$LR_ROOT/llvm/bin/$t"
done
ln -s "$DOLPHIN_SYSROOT/include/c++" "$LR_ROOT/llvm/include/c++"

# --- Host shader tools: spirv-val + glslangValidator (with optimizer) from the SDK's submodules ---
HST_BUILD="$LR_ROOT/build/host-shader-tools"
mkdir -p "$LR_ROOT/host-tools/bin"
if [ ! -x "$LR_ROOT/host-tools/bin/spirv-val" ] || [ ! -x "$LR_ROOT/host-tools/bin/glslangValidator" ]; then
    cmake -S "$LR_SCRIPTS/host-shader-tools" -B "$HST_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DREXGLUE_THIRDPARTY="$LR_SRC/glue/rexglue-sdk-main/thirdparty" \
        -DGLSLANG_SOURCE_DIR="$HOME/dolphinps4-build/src/dolphin/Externals/glslang/glslang" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_C_COMPILER=clang-21 -DCMAKE_CXX_COMPILER=clang++-21 >/dev/null
    cmake --build "$HST_BUILD" --target spirv-val glslang-standalone
    cp "$HST_BUILD/spirv-tools/tools/spirv-val" "$LR_ROOT/host-tools/bin/"
    cp "$HST_BUILD/glslang/StandAlone/glslang" "$LR_ROOT/host-tools/bin/glslangValidator"
fi

# --- DXC for the HLSL shader overrides: the bundled DXC 1.8 library crashes in SPIRV-Tools on the
# Bink pixel shaders (the macOS build has the same workaround), so the bundled dxc front end runs
# against a current libdxcompiler from Microsoft's official release. ---
DXC_VERSION="v1.9.2609"
DXC_ASSET="linux_dxc_2026_09_28.x86_x64.tar.gz"
if [ ! -f "$LR_ROOT/host-tools/dxc/lib/libdxcompiler.so" ]; then
    mkdir -p "$LR_ROOT/host-tools/dxc"
    curl -sSLf -o "$LR_ROOT/host-tools/dxc/dxc.tar.gz" \
        "https://github.com/microsoft/DirectXShaderCompiler/releases/download/$DXC_VERSION/$DXC_ASSET"
    tar xzf "$LR_ROOT/host-tools/dxc/dxc.tar.gz" -C "$LR_ROOT/host-tools/dxc"
    rm "$LR_ROOT/host-tools/dxc/dxc.tar.gz"
fi

# --- Game payload: extracted ISO minus the console system-update folder ---
if [ -d "$LR_ROOT/game/iso" ] && [ ! -d "$LR_PAYLOAD" ]; then
    mv "$LR_ROOT/game/iso" "$LR_PAYLOAD"
fi
if [ -d "$LR_PAYLOAD/\$SystemUpdate" ]; then
    mv "$LR_PAYLOAD/\$SystemUpdate" "$LR_ROOT/game/SystemUpdate"
fi

echo "OO_PS4_TOOLCHAIN=$OO"
ls -la "$OO/lib/libcxx_ps4_compat.a"
clang --version | head -1
ls "$LR_PAYLOAD"
