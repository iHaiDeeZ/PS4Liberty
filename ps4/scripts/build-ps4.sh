#!/bin/bash
# Configure + build LibertyRecomp for PS4.
# Usage: build-ps4.sh [clean|configure|build|all] [extra ninja args]   (clean = wipe build dir, then all)
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/ps4-env.sh"
step="${1:-all}"; shift || true
cd "$LR_SRC"
LOGS="$LR_ROOT/logs"; mkdir -p "$LOGS"
BUILD_DIR="$LR_SRC/out/build/ps4-release"

if [ "$step" = clean ]; then
    rm -rf "$BUILD_DIR"; step=all
fi
# -O2 for the SDK (runtime, native renderer: full inlining - the renderer's per-draw C++ is
# its bottleneck on the PS4's CPU). The game target itself (recomp code + hooks) adds
# -finline-hint-functions in gta4-recomp/CMakeLists.txt, i.e. -O2 with inlining limited to
# inline-hinted functions: the clang equivalent of the Windows
# RelWithDebInfo build (/O2 /Ob1) that runs the game. The Windows Release build (/Ob2, full
# inlining) crashes in game code after the menus (wild guest write under sub_8285D170).
# Mesa RADV: our own build ($LR_ROOT/mesa-build, a worktree of DolphinPS4's Mesa at mesa-src) with
# mesa-aco-mubuf-addr64.patch: on GFX7 ACO reaches global memory with FLAT, which hangs the PS4's
# GPU (the Orbis kernel's FLAT apertures); the patch uses MUBUF addr64 as on GFX6. Skinned vertex
# shaders (dynamically indexed bone matrices) hung the GPU at the first in-game frame.
if [ "$step" = configure ] || [ "$step" = all ]; then
    cmake --preset ps4-release \
        -DOO_PS4_TOOLCHAIN="$OO_PS4_TOOLCHAIN" \
        -DPS4_LIBC_OVERLAY="$PS4_LIBC_OVERLAY" \
        -DGTA4_NATIVE_DXC_LIBRARY_PATH="$LR_ROOT/host-tools/dxc/lib" \
        -DLIBERTY_RECOMP_EMBEDDED_GAME_PATH="$LR_PAYLOAD" \
        -DPS4_MESA_BUILD="$LR_ROOT/mesa-build" \
        -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" \
        -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" \
        2>&1 | tee "$LOGS/configure.log"
    rc=${PIPESTATUS[0]}; [ $rc -eq 0 ] || { echo "CONFIGURE FAILED ($rc)"; exit $rc; }
fi
if [ "$step" = build ] || [ "$step" = all ]; then
    cmake --build "$BUILD_DIR" --target LibertyRecomp -- -k 0 "$@" 2>&1 | tee "$LOGS/build.log"
    rc=${PIPESTATUS[0]}; echo "BUILD EXIT $rc"; exit $rc
fi
