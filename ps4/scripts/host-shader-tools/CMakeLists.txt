# Host build of the shader tools the gta4-native renderer's shader override
# compiler runs at build time: spirv-val and glslangValidator (with the SPIR-V
# optimizer, for -Os).
#   cmake -S scripts/host-shader-tools -B <build>
#         -DREXGLUE_THIRDPARTY=<sdk>/thirdparty -DGLSLANG_SOURCE_DIR=<glslang>
# SPIRV-Tools comes from the RexGlue SDK. glslang must be recent enough for the
# override shaders (GL_EXT_spirv_intrinsics): the SDK's own copy is too old, so
# setup-toolchain.sh points GLSLANG_SOURCE_DIR at DolphinPS4's glslang 16.
cmake_minimum_required(VERSION 3.20)
project(liberty_host_shader_tools C CXX)

if(NOT REXGLUE_THIRDPARTY)
    message(FATAL_ERROR "Set REXGLUE_THIRDPARTY to glue/rexglue-sdk-main/thirdparty")
endif()
if(NOT GLSLANG_SOURCE_DIR)
    set(GLSLANG_SOURCE_DIR "${REXGLUE_THIRDPARTY}/glslang")
endif()

set(SPIRV-Headers_SOURCE_DIR "${REXGLUE_THIRDPARTY}/spirv-headers")
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
add_subdirectory("${REXGLUE_THIRDPARTY}/spirv-tools" spirv-tools)

# glslang turns its optimizer on only if the SPIRV-Tools-opt target exists.
set(ENABLE_OPT ON CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory("${GLSLANG_SOURCE_DIR}" glslang)
