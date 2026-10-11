#!/bin/bash
# Environment for building LibertyRecomp (GTA IV recomp) for PS4.
# Source this from WSL Ubuntu: source ps4/scripts/ps4-env.sh
#
# Uses the PacBrew OpenOrbis install plus the LLVM 21 libc++ built by DolphinPS4, merged into an
# overlay toolchain dir (see setup-toolchain.sh) because LibertyRecomp's toolchain file expects
# files that stock OpenOrbis doesn't ship (libcxx_ps4_compat.a, a modern libc++ header tree).

LR_ROOT="${LR_ROOT:-$HOME/libertyrecomp-ps4-build}"
LR_SRC="$LR_ROOT/src/LibertyRecomp"
LR_SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENORBIS="${OPENORBIS:-/opt/pacbrew/ps4/openorbis}"
DOLPHIN_SYSROOT="${DOLPHIN_SYSROOT:-$HOME/dolphinps4-build/sysroot}"

export OO_PS4_TOOLCHAIN="$LR_ROOT/oo"
export LR_PAYLOAD="$LR_ROOT/game/payload"
# DolphinPS4's libc header fixes (plain musl math.h without the C++ overloads that clash with
# LLVM 21 libc++, plus signal/mman/unistd/time/pthread_np wrappers that #include_next the
# OpenOrbis originals). The toolchain file searches this right before $OO_PS4_TOOLCHAIN/include.
export PS4_LIBC_OVERLAY="$DOLPHIN_SYSROOT/libc-overlay"

# The toolchain file locates libc++ headers at <clang>/../../include/c++/v1 (Homebrew layout),
# so put a fake LLVM prefix first on PATH whose include/ points at the DolphinPS4 libc++ headers.
export PATH="$LR_ROOT/llvm/bin:$PATH"
# Host-side shader tools for the gta4-native renderer's override compiler
# (spirv-val built by setup-toolchain.sh, glslangValidator from DolphinPS4's host tools).
export PATH="$LR_ROOT/host-tools/bin:$PATH"

# PacBrew's tools link against libxml2.so.2; newer distros only ship libxml2.so.16 (same C API).
if ldd "$OPENORBIS/bin/ld.lld" 2>/dev/null | grep -q "libxml2.so.2 => not found"; then
    compat=$(ls /usr/lib/x86_64-linux-gnu/libxml2.so.* 2>/dev/null | grep -E 'libxml2\.so\.[0-9]+$' | head -1)
    if [ -n "$compat" ]; then
        mkdir -p "$LR_ROOT/host-compat"
        ln -sf "$compat" "$LR_ROOT/host-compat/libxml2.so.2"
        export LD_LIBRARY_PATH="$LR_ROOT/host-compat${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    fi
fi

# Never let makefile-style flags leak into CMake (they break the toolchain's include order).
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LIBS
