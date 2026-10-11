# PS4 / OpenOrbis Toolchain
# Requirements:
#   - OpenOrbis PS4 Toolchain cloned into tools/OpenOrbis-PS4-Toolchain
#     (already present — see tools/OpenOrbis-PS4-Toolchain)
#   - LLVM/lld installed: brew install llvm  (macOS/Linux)
#   - Environment variable OO_PS4_TOOLCHAIN pointing at the toolchain root
#
# Setup:
#   export OO_PS4_TOOLCHAIN="$(pwd)/tools/OpenOrbis-PS4-Toolchain"
#   cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/ps4-openorbis.cmake \
#         -DLIBERTY_RECOMP_TARGET_PLATFORM=ps4 \
#         -DLIBERTY_RECOMP_EMBEDDED_GAME_PATH=tools/local_game_payload \
#         ..

# Tell rexglue SDK we're targeting PS4 console
set(LIBERTY_RECOMP_TARGET_PLATFORM "ps4" CACHE STRING "" FORCE)

# Resolve toolchain root.
# CMake presets forward parent env via `$penv{OO_PS4_TOOLCHAIN}`, which
# evaluates to an EMPTY STRING when the parent var is unset — that still
# counts as DEFINED, so treat empty-string the same as unset.
if(NOT DEFINED OO_PS4_TOOLCHAIN OR OO_PS4_TOOLCHAIN STREQUAL "")
    if(DEFINED ENV{OO_PS4_TOOLCHAIN} AND NOT "$ENV{OO_PS4_TOOLCHAIN}" STREQUAL "")
        set(OO_PS4_TOOLCHAIN "$ENV{OO_PS4_TOOLCHAIN}")
    else()
        # Default: repo-local clone (use CMAKE_CURRENT_LIST_DIR for try_compile safety)
        get_filename_component(_toolchain_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
        set(OO_PS4_TOOLCHAIN "${_toolchain_repo_root}/tools/OpenOrbis-PS4-Toolchain")
    endif()
endif()

if(NOT EXISTS "${OO_PS4_TOOLCHAIN}")
    message(FATAL_ERROR
        "OpenOrbis toolchain not found at: ${OO_PS4_TOOLCHAIN}\n"
        "Run: git clone https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain.git tools/OpenOrbis-PS4-Toolchain\n"
        "Or set OO_PS4_TOOLCHAIN to the correct path.")
endif()

message(STATUS "Using OpenOrbis PS4 Toolchain: ${OO_PS4_TOOLCHAIN}")

set(CMAKE_SYSTEM_NAME PS4)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
# Platform/PS4.cmake lives next to this file (FreeBSD-derived UNIX platform).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

# OpenOrbis sample builds target FreeBSD ELF and rely on link.x + create-fself
# for PS4-specific rewriting. Match that model instead of mixing in the
# x86_64-scei-ps4 target triple.
# Prefer Homebrew LLVM (newer, with matching libc++ headers)
find_program(CLANG_PATH "clang" PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin NO_DEFAULT_PATH)
find_program(CLANGPP_PATH "clang++" PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin NO_DEFAULT_PATH)
if(NOT CLANG_PATH)
    find_program(CLANG_PATH "clang" REQUIRED)
    find_program(CLANGPP_PATH "clang++" REQUIRED)
endif()
find_program(LLD_PATH "lld" REQUIRED)
find_program(LD_LLD_PATH "ld.lld" PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin NO_DEFAULT_PATH)
if(NOT LD_LLD_PATH)
    find_program(LD_LLD_PATH "ld.lld" REQUIRED)
endif()

set(CMAKE_C_COMPILER   "${CLANG_PATH}")
set(CMAKE_CXX_COMPILER "${CLANGPP_PATH}")
set(CMAKE_LINKER        "${LD_LLD_PATH}")

set(CMAKE_C_COMPILER_TARGET   "x86_64-pc-freebsd12-elf")
set(CMAKE_CXX_COMPILER_TARGET "x86_64-pc-freebsd12-elf")
set(CMAKE_ASM_COMPILER        "${CLANG_PATH}")
set(CMAKE_ASM_COMPILER_TARGET "x86_64-pc-freebsd12-elf")

# The x86_64-scei-ps4 driver expects `orbis-ld` on the tool search path.
# We provide a thin wrapper at ${OO_PS4_TOOLCHAIN}/bin/orbis-ld that execs
# the host lld (which natively supports the PS4/orbis linker flavor).
# -B tells clang where to find toolchain programs.
# Link PS4 system libraries: C/C++ runtime, kernel, and SDK stubs.
set(_PS4_LINK_FLAGS "-B ${OO_PS4_TOOLCHAIN}/bin")
# Match the canonical OpenOrbis sample build (samples/hello_world/Makefile):
# `-m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr crt1.o`.
# The clang freebsd12 driver would otherwise inject its own crt1.o + crti.o +
# crtbegin.o + crtend.o + crtn.o + -lgcc/-lgcc_s + a default PT_INTERP
# pointing at /libexec/ld-elf.so.1 — none of which PS4 wants.
string(APPEND _PS4_LINK_FLAGS " -nostdlib")
# -pie: PS4 SELFs are position-independent; without this, lld emits absolute
# relocations that break create-fself's relro/dynamic-section rewriting.
string(APPEND _PS4_LINK_FLAGS " -pie")
# --eh-frame-hdr: emit a proper PT_GNU_EH_FRAME with version=1 byte. Without
# it, our libcxx_ps4_compat unwind tables get a malformed header and shadPS4
# logs `Critical: Unsupported .eh_frame_hdr version: 0x0`.
string(APPEND _PS4_LINK_FLAGS " -Wl,--eh-frame-hdr")
# CANONICAL OpenOrbis linker script. It (a) embeds the interp string
# `/libexec/ld-elf.so.1` as 4 inline QUADs in .text so no PT_INTERP segment
# is needed (avoids "Unimplemented type Interpreter Path x2"), (b) defines
# explicit output sections for .eh_frame, .eh_frame_hdr, .data.sce_process_param,
# .data.sce_module_param, .dynamic, .tls, .got, .got.plt, .init_array — the
# exact layout create-fself's OELFGenProgramHeaders expects.
# Replaces our previous ps4-orbis-sections.ld INSERT-BEFORE script, which let
# lld defaults emit PT_GNU_STACK/RELRO/EH_FRAME and a PT_INTERP segment that
# shadPS4 doesn't recognize.
string(APPEND _PS4_LINK_FLAGS " -Wl,--script=${OO_PS4_TOOLCHAIN}/link.x")
# libunwind (in OpenOrbis libc++.a) references __eh_frame_start/end and
# __eh_frame_hdr_start/end. link.x defines these as section bracket symbols
# (KEEP(*(.eh_frame))), so our os/ps4/eh_frame_stub.cpp weak symbols are
# now redundant — kept compiled-in for safety since they're zero-length.

# PS4 CRT: ONLY crt1.o (provides _start entry point + .data.sce_process_param).
# Drop crti.o/crtn.o — those are FreeBSD's _init/_fini wrappers; PS4 uses
# .ctors/.dtors arrays processed by crt1.o, not _init/_fini sections.
string(APPEND _PS4_LINK_FLAGS " ${OO_PS4_TOOLCHAIN}/lib/crt1.o")
# The clang FreeBSD driver adds -dynamic-linker /libexec/ld-elf.so.1, which
# makes lld emit a .interp section in front of .text. link.x already embeds the
# interpreter string at the start of .text; the extra PT_INTERP and the shifted
# code segment make the console refuse the eboot (CE-41839-5).
string(APPEND _PS4_LINK_FLAGS " -Wl,--no-dynamic-linker")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld ${_PS4_LINK_FLAGS}")
# No library search paths in the eboot: CMake would otherwise add a build RPATH
# for the SDK stub library directory (a host path), which becomes a DT_RUNPATH
# the PS4 loader does not accept.
set(CMAKE_SKIP_RPATH ON)

# Standard libraries go at the end of the link line, after the program's own
# objects and libraries (CMAKE_<LANG>_STANDARD_LIBRARIES). In the linker flags
# they would come first, and an early reference would pull libc members such
# as memcpy or the C11 thread functions out of libc.a even though the program
# (or Mesa, linked as whole archives) defines them, giving duplicate symbols.
#
# C/C++ runtime (musl libc, libc++, libc++abi, unwind, compiler-rt).
# libcxx_ps4_compat.a: LLVM 21 libc++ + libc++abi compiled for PS4. It is the
# only C++ runtime linked: the SDK's own LLVM 12 libc++/libc++abi are left out
# so the two can never mix. libunwind comes from the SDK.
set(_PS4_STD_LIBS "${OO_PS4_TOOLCHAIN}/lib/libcxx_ps4_compat.a")
# PS4 kernel (provides pthread, sceKernel*, mmap, sockets, signals, dl*, etc.).
# It must come before libc.a: functions both provide must resolve to the
# kernel. Taken from libc.a instead, they would be linked into the eboot and
# exported to override the kernel's (e.g. __tls_get_addr, dlopen, sem_*),
# and the console refuses to start such an eboot (CE-41839-5).
string(APPEND _PS4_STD_LIBS " -lkernel")
string(APPEND _PS4_STD_LIBS " -lunwind -lc -lm")
string(APPEND _PS4_STD_LIBS " ${OO_PS4_TOOLCHAIN}/lib/libclang_rt.builtins-x86_64.a")
# Sony's system libc: libc.a's malloc.lo (pulled in by libc start-up through
# malloc_init) is built on its sceLibcMspace API, and it applies the
# malloc-replacement table of a replacement crt1.o (cmake/PS4Runtime.cmake).
string(APPEND _PS4_STD_LIBS " -lSceLibcInternal")
# PS4 SDK stub libraries. Each one that ends up needed becomes a module the
# loader must resolve at launch, so they are linked --as-needed: only the ones
# the program actually calls are imported. (No NP/PSN libraries: the build
# does not use them, and PacBrew's OpenOrbis has no libSceNpManager stub.)
string(APPEND _PS4_STD_LIBS " -Wl,--as-needed")
string(APPEND _PS4_STD_LIBS " -lSceSystemService -lSceSysmodule -lScePad -lSceUserService")
string(APPEND _PS4_STD_LIBS " -lSceAudioOut -lSceAudioIn -lSceVideoOut")
string(APPEND _PS4_STD_LIBS " -lSceNet -lSceNetCtl -lSceHttp -lSceSsl")
string(APPEND _PS4_STD_LIBS " -lSceSaveData -lSceCommonDialog -lSceMsgDialog")
string(APPEND _PS4_STD_LIBS " -lSceGnmDriver -lSceFios2 -lScePosix")
string(APPEND _PS4_STD_LIBS " -lSceRtc")
string(APPEND _PS4_STD_LIBS " -Wl,--no-as-needed")
set(CMAKE_C_STANDARD_LIBRARIES_INIT "${_PS4_STD_LIBS}")
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "${_PS4_STD_LIBS}")

# Cross-compilation: skip try_compile linking
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Add OpenOrbis include/lib paths (PS4-specific headers like orbis/*.h)
include_directories(SYSTEM "${OO_PS4_TOOLCHAIN}/include")
link_directories("${OO_PS4_TOOLCHAIN}/lib")

# Build include paths for cross-compilation.
# Order: Homebrew libc++ (C++23 features) → clang builtins → SDK C headers (musl).
# We use Homebrew's modern libc++ for C++23 support (std::bit_cast, etc.)
# The SDK-bundled libc++ is too old (~LLVM 10). We provide an xlocale.h shim
# and enable _GNU_SOURCE so musl exposes the POSIX APIs libc++ needs.
get_filename_component(_BREW_LLVM_DIR "${CLANG_PATH}/../.." ABSOLUTE)
execute_process(
    COMMAND "${CLANG_PATH}" -print-resource-dir
    OUTPUT_VARIABLE _CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(_PS4_C_ISYSTEM "")
set(_PS4_CXX_ISYSTEM "")
# Only C++ compilations should see libc++ headers.
# 0) PS4 __config_site overlay (shadows Homebrew's to override musl/pthread settings)
string(APPEND _PS4_CXX_ISYSTEM " -isystem ${OO_PS4_TOOLCHAIN}/include/c++/v1")
# 1) Homebrew libc++ C++ stdlib headers (modern, C++23 capable)
string(APPEND _PS4_CXX_ISYSTEM " -isystem ${_BREW_LLVM_DIR}/include/c++/v1")
message(STATUS "PS4: Using Homebrew libc++ headers from ${_BREW_LLVM_DIR}")
# 2) Clang builtins (stddef.h, stdint.h, etc.)
if(_CLANG_RESOURCE_DIR)
    string(APPEND _PS4_C_ISYSTEM   " -isystem ${_CLANG_RESOURCE_DIR}/include")
    string(APPEND _PS4_CXX_ISYSTEM " -isystem ${_CLANG_RESOURCE_DIR}/include")
endif()
# 3a) Optional libc header overlay (PS4_LIBC_OVERLAY, CMake or env var):
#     replacement/wrapper headers that #include_next the OpenOrbis originals,
#     e.g. a math.h without the C++ isfinite/isnan overloads that collide with
#     modern libc++'s <cmath>.
if(NOT PS4_LIBC_OVERLAY AND DEFINED ENV{PS4_LIBC_OVERLAY})
    set(PS4_LIBC_OVERLAY "$ENV{PS4_LIBC_OVERLAY}")
endif()
if(PS4_LIBC_OVERLAY)
    string(APPEND _PS4_C_ISYSTEM   " -isystem ${PS4_LIBC_OVERLAY}")
    string(APPEND _PS4_CXX_ISYSTEM " -isystem ${PS4_LIBC_OVERLAY}")
endif()
# 3) OpenOrbis SDK C headers (musl: string.h, stdio.h, pthread.h, etc.)
string(APPEND _PS4_C_ISYSTEM   " -isystem ${OO_PS4_TOOLCHAIN}/include")
string(APPEND _PS4_CXX_ISYSTEM " -isystem ${OO_PS4_TOOLCHAIN}/include")

message(STATUS "PS4: Using Homebrew LLVM from ${_BREW_LLVM_DIR}")

# _GNU_SOURCE enables all POSIX extensions in musl (nanosleep, localtime_r,
# locale_t, uselocale, fileno, fsync, tm_gmtoff, etc.) that libc++ requires.
# -U__FreeBSD__: PS4 is FreeBSD-based but we use musl headers, not FreeBSD libc.
#   Without this, libc++ selects the FreeBSD locale backend which needs
#   strtoll_l, snprintf_l, asprintf_l, etc. that musl doesn't provide.
# _LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE: provides generic ctype bitmasks
#   instead of platform-specific _CTYPE_* / _IS* constants.
# Note: _LIBCPP_HAS_MUSL_LIBC and _LIBCPP_HAS_THREAD_API_PTHREAD are set
# via the __config_site overlay in include/c++/v1/__config_site.
set(_PS4_DEFINES "-D__ORBIS__ -D_GNU_SOURCE -U__FreeBSD__ -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE")
# The PS4 CPU is AMD Jaguar (AVX, F16C, BMI1, SSE4.2; no AVX2/BMI2). It goes in
# the base flags so that try_compile feature checks (e.g. snappy's SIMD probes)
# see the same instruction set as the real compile.
string(APPEND _PS4_DEFINES " -march=btver2")

# Suppress default includes and set our explicit ordered paths
# Modern clang may define wchar_t via stddef.h before OpenOrbis musl headers,
# or reach stdlib.h without it depending on include order. Force stddef.h in
# first for C and suppress the duplicate musl typedef in that mode.
#
# -fPIC + -funwind-tables match the canonical OpenOrbis sample CFLAGS:
# -fPIC is required because we link with -pie (PS4 SELFs are PIE).
# -funwind-tables ensures clang emits .eh_frame entries for every function
# even without try/catch, so libunwind has the data it needs at signal time.
# Without these, exception unwinding from signal handlers crashes (which is
# exactly what we hit at rex::arch::ExceptionHandlerCallback).
set(CMAKE_C_FLAGS_INIT   "${_PS4_DEFINES} -D__DEFINED_wchar_t -include stddef.h -nostdinc -fPIC -funwind-tables ${_PS4_C_ISYSTEM}")
set(CMAKE_CXX_FLAGS_INIT "${_PS4_DEFINES} -fexceptions -fcxx-exceptions -frtti -nostdinc++ -nostdinc -fPIC -funwind-tables ${_PS4_CXX_ISYSTEM}")

# Prevent CMake from finding host (macOS) libraries during cross-compilation
set(CMAKE_FIND_ROOT_PATH "${OO_PS4_TOOLCHAIN}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# PS4 has no Vulkan ICD via OpenOrbis (no vkGetInstanceProcAddr, no WSI
# extensions, no dynamic loader). The SDK's Vulkan UI subsystem is therefore
# disabled here AND in glue/rexglue-sdk-main/CMakeLists.txt — the recomp uses
# the GNM-backed path in LibertyRecomp/gpu/video.cpp directly. If a Vulkan-
# on-GNM translation layer (e.g. a future community port) becomes available,
# flip both knobs in tandem.
set(LIBERTY_RECOMP_VULKAN OFF CACHE BOOL "PS4 uses GNM directly; no Vulkan ICD available" FORCE)
set(LIBERTY_RECOMP_D3D12 OFF CACHE BOOL "" FORCE)
set(LIBERTY_RECOMP_METAL OFF CACHE BOOL "" FORCE)
set(LIBERTY_RECOMP_GNM ON CACHE BOOL "Use GNM-native renderer on PS4" FORCE)

# No installer wizard on PS4
set(LIBERTY_RECOMP_EMBEDDED_ASSETS ON CACHE BOOL "Package assets at build time (no installer UI)" FORCE)

# PKG output target name
set(LIBERTY_PS4_TITLE_ID "LBTY00001" CACHE STRING "PS4 title ID for PKG packaging")
set(LIBERTY_PS4_APP_VERSION "01.00" CACHE STRING "PS4 application version")
