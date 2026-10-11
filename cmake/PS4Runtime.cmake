# PS4 (OpenOrbis) executable support for toolchains/ps4-openorbis.cmake builds.
#
# Adapted from DolphinPS4's toolchain/ps4-love-style.cmake, which runs on retail
# consoles. Include once from the top-level project before the executable's
# directory is added; it adjusts CMAKE_EXE_LINKER_FLAGS for that scope:
#   - a patched copy of the OpenOrbis link.x that keeps prioritized constructors
#     (.init_array.NNN, e.g. libc++'s iostream setup) and per-symbol data
#     sections (.data.*, .tdata.*, .tbss.*). With the stock script those become
#     orphan sections the loader never initializes, so globals start as zeros.
#   - a crt1.o replacement (os/ps4/runtime/ps4_crt1.S) whose libc malloc
#     replacement table sends system-module allocations to the app heap.
#
# Functions:
#   ps4_configure_executable(<target>)  app heap + libc fixes (os/ps4/runtime)
#   ps4_link_mesa(<target>)             Mesa RADV (Vulkan on GNM), linked in
#   ps4_add_eboot(<target>)             ELF -> fake-SELF eboot.bin, OELF checked

include_guard(GLOBAL)

if(NOT OO_PS4_TOOLCHAIN)
    message(FATAL_ERROR "PS4Runtime.cmake needs the PS4 toolchain file (OO_PS4_TOOLCHAIN)")
endif()

set(PS4_RUNTIME_DIR "${CMAKE_CURRENT_LIST_DIR}/../os/ps4/runtime")
set(PS4_RUNTIME_SOURCES
    "${PS4_RUNTIME_DIR}/ps4_runtime.cpp"
    "${PS4_RUNTIME_DIR}/ps4_dlmalloc.c"
    "${PS4_RUNTIME_DIR}/ps4_crashlog.cpp"
    "${PS4_RUNTIME_DIR}/ps4_string.c"
    "${PS4_RUNTIME_DIR}/ps4_compat.c"
    "${PS4_RUNTIME_DIR}/ps4_stackdump.cpp")

# ── Linker script ────────────────────────────────────────────────────────────
set(_ps4_link_script_in "${OO_PS4_TOOLCHAIN}/link.x")
set(PS4_LINK_SCRIPT "${CMAKE_BINARY_DIR}/ps4-link.x")
file(READ "${_ps4_link_script_in}" _ps4_link_script_text)
string(FIND "${_ps4_link_script_text}" "*(.init_array);" _ps4_pos)
if(_ps4_pos EQUAL -1)
    message(FATAL_ERROR "Unexpected ${_ps4_link_script_in}: no '*(.init_array);' rule to patch")
endif()
string(REPLACE "*(.init_array);"
    "KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))); KEEP(*(.init_array));"
    _ps4_link_script_text "${_ps4_link_script_text}")
foreach(_rule ".data" ".tdata" ".tbss")
    string(FIND "${_ps4_link_script_text}" "*(${_rule})" _ps4_pos)
    if(_ps4_pos EQUAL -1)
        message(FATAL_ERROR "Unexpected ${_ps4_link_script_in}: no '*(${_rule})' rule to patch")
    endif()
    string(REPLACE "*(${_rule})" "*(${_rule} ${_rule}.*)"
        _ps4_link_script_text "${_ps4_link_script_text}")
endforeach()
# Code built with -mcmodel=large (the RexGlue app target) puts its sections in
# .ltext/.lrodata/.ldata/.lbss. As orphans, lld places .ldata/.lbss after
# .data/.bss, past what create-fself maps: its writable segment ends at .bss,
# so .ldata's initial values are lost and .lbss is left unmapped. Fold them
# into the regular sections.
foreach(_pair ".text|.ltext" ".rodata|.lrodata" ".data|.ldata" ".bss|.lbss")
    string(REPLACE "|" ";" _pair "${_pair}")
    list(GET _pair 0 _regular)
    list(GET _pair 1 _large)
    set(_rule "*(${_regular} ${_regular}.*)")
    string(FIND "${_ps4_link_script_text}" "${_rule}" _ps4_pos)
    if(_ps4_pos EQUAL -1)
        message(FATAL_ERROR "Unexpected ${_ps4_link_script_in}: no '${_rule}' rule to patch")
    endif()
    string(REPLACE "${_rule}" "*(${_regular} ${_regular}.* ${_large} ${_large}.*)"
        _ps4_link_script_text "${_ps4_link_script_text}")
endforeach()

# Notes such as SDL3's .note.dlopen (optional shared library manifest) would be
# placed in front of .text as orphans, shifting the code segment off address 0;
# the console then refuses the eboot (CE-41839-5).
string(FIND "${_ps4_link_script_text}" "/DISCARD/ : {" _ps4_pos)
if(_ps4_pos EQUAL -1)
    message(FATAL_ERROR "Unexpected ${_ps4_link_script_in}: no '/DISCARD/ : {' rule to patch")
endif()
string(REPLACE "/DISCARD/ : {" "/DISCARD/ : {\n\t\t*(.note.dlopen)"
    _ps4_link_script_text "${_ps4_link_script_text}")
file(WRITE "${PS4_LINK_SCRIPT}" "${_ps4_link_script_text}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ps4_link_script_in}")

string(FIND "${CMAKE_EXE_LINKER_FLAGS}" "--script=${_ps4_link_script_in}" _ps4_pos)
if(_ps4_pos EQUAL -1)
    message(FATAL_ERROR "PS4Runtime.cmake: '--script=${_ps4_link_script_in}' not found in "
        "CMAKE_EXE_LINKER_FLAGS; has toolchains/ps4-openorbis.cmake changed?")
endif()
string(REPLACE "--script=${_ps4_link_script_in}" "--script=${PS4_LINK_SCRIPT}"
    CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

# ── crt1.o with the malloc replacement table ─────────────────────────────────
set(PS4_CRT1 "${CMAKE_BINARY_DIR}/ps4-crt1-malloc-replace.o")
execute_process(
    COMMAND "${CMAKE_C_COMPILER}" --target=x86_64-pc-freebsd12-elf -c
            "${PS4_RUNTIME_DIR}/ps4_crt1.S" -o "${PS4_CRT1}"
    RESULT_VARIABLE _ps4_crt1_result)
if(NOT _ps4_crt1_result EQUAL 0)
    message(FATAL_ERROR "assembling os/ps4/runtime/ps4_crt1.S failed: ${_ps4_crt1_result}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PS4_RUNTIME_DIR}/ps4_crt1.S")
string(REPLACE "${OO_PS4_TOOLCHAIN}/lib/crt1.o" "${PS4_CRT1}"
    CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

# ── Exported symbols ─────────────────────────────────────────────────────────
# lld exports any symbol the eboot defines that a linked system library also
# defines (e.g. our getcontext/swapcontext, libc.a's execvp), which would
# override the system's own for every module in the process. Export only what
# OpenOrbis' libc start-up needs, as a plain OpenOrbis eboot does.
set(PS4_VERSION_SCRIPT "${CMAKE_BINARY_DIR}/ps4-exports.map")
file(WRITE "${PS4_VERSION_SCRIPT}"
"{
  global:
    __progname;
    environ;
    pthread_getaffinity_np;
    pthread_setaffinity_np;
  local:
    *;
};
")

# ── App heap and libc fixes ──────────────────────────────────────────────────
function(ps4_configure_executable target)
    target_sources(${target} PRIVATE ${PS4_RUNTIME_SOURCES})
    target_link_options(${target} PRIVATE "LINKER:--version-script=${PS4_VERSION_SCRIPT}")
    # OpenOrbis' libc heap cannot initialize on retail consoles: route the
    # malloc family to the dlmalloc heap in ps4_runtime.cpp. The kernel's
    # pthread_once writes past the headers' pthread_once_t; large anonymous
    # mmaps go to the system flexible pool; CLOCK_MONOTONIC comes from the TSC.
    # abort/_exit: logged with their callers (ps4_crashlog.cpp).
    foreach(_fn malloc free calloc realloc memalign __memalign pthread_once mmap clock_gettime
            abort _exit
            fdatasync msync sysctlbyname flock  # ps4_compat.c
            fma fmaf  # ps4_compat.c: no FMA unit on the PS4's Jaguar cores
            pthread_create)  # ps4_stackdump.cpp: every thread in the stack dumps
        target_link_options(${target} PRIVATE "LINKER:--wrap=${_fn}")
    endforeach()
endfunction()

# ── Mesa RADV ────────────────────────────────────────────────────────────────
# Built by DolphinPS4's scripts/build-mesa.sh. Whole archives, as Mesa's own
# shared-library link does: the dispatch tables refer to entry points through
# weak symbols, which don't pull archive members (they would silently be NULL).
set(PS4_MESA_BUILD "$ENV{HOME}/dolphinps4-build/build/mesa" CACHE PATH
    "Mesa RADV build directory for the PS4 (DolphinPS4 scripts/build-mesa.sh)")
function(ps4_link_mesa target)
    set(libs
        src/amd/vulkan/libvulkan_radeon.a src/amd/common/libamd_common.a
        src/amd/compiler/libaco.a src/amd/addrlib/libaddrlib.a src/vulkan/util/libvulkan_util.a
        src/compiler/spirv/libvtn.a src/compiler/nir/libnir.a src/compiler/libcompiler.a
        src/util/libxmlconfig.a src/util/libmesa_util.a src/util/libmesa_util_simd.a
        src/util/libmesa_util_clflush.a src/util/libmesa_util_clflushopt.a src/util/libparson.a
        src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a)
    list(TRANSFORM libs PREPEND "${PS4_MESA_BUILD}/")
    foreach(lib IN LISTS libs)
        if(NOT EXISTS "${lib}")
            message(FATAL_ERROR "Mesa RADV library missing: ${lib}\n"
                "Build it with DolphinPS4's scripts/build-mesa.sh or set PS4_MESA_BUILD.")
        endif()
    endforeach()
    target_link_libraries(${target} PRIVATE "-Wl,--whole-archive" ${libs} "-Wl,--no-whole-archive")
endfunction()

# ── eboot.bin ────────────────────────────────────────────────────────────────
# Program authority ID: the plain homebrew identity, which RADV needs (the
# RetroArch identity 0x3100000000000002 is for the Piglet GL path). The
# identity also decides the direct memory size, i.e. RADV's GPU memory.
set(PS4_PAID "0x3800000000000035" CACHE STRING "Program authority ID of eboot.bin")
set(PS4_AUTH_INFO "000000000000000000000000001C004000FF000000000080000000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    CACHE STRING "create-fself --authinfo for eboot.bin")

# ELF -> <target dir>/eboot.bin (fake SELF). PacBrew's create-fself sometimes
# duplicates the data PT_LOAD, which the console rejects at launch
# (CE-41839-5), so the intermediate OELF is checked.
function(ps4_add_eboot target)
    set(out_dir "$<TARGET_FILE_DIR:${target}>")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env "OO_PS4_TOOLCHAIN=${OO_PS4_TOOLCHAIN}"
            "${OO_PS4_TOOLCHAIN}/bin/create-fself" -in=$<TARGET_FILE:${target}>
            -out=${out_dir}/${target}.oelf --eboot ${out_dir}/eboot.bin
            --paid ${PS4_PAID} --authinfo ${PS4_AUTH_INFO}
        COMMAND ${CMAKE_COMMAND} -DOELF=${out_dir}/${target}.oelf
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/PS4CheckOelf.cmake"
        COMMENT "PS4: creating eboot.bin"
        VERBATIM)
endfunction()
