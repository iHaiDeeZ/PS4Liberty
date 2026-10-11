set(REXGLUE_USE_VULKAN ON CACHE BOOL "" FORCE)
set(REXGLUE_USE_D3D12 OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_FIDELITYFX OFF CACHE BOOL "Enable optional FidelityFX temporal providers")
set(REXGLUE_BUILD_TESTS OFF CACHE BOOL "")

if(PS4)
    # Tracy is a desktop profiler built as a shared library with no PS4 port.
    set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
endif()

set(REXSDK_DIR "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main"
    CACHE PATH "Path to the Graine RexGlue SDK source tree")

if(NOT EXISTS "${REXSDK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Graine RexGlue SDK not found at ${REXSDK_DIR}")
endif()

add_subdirectory("${REXSDK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/rexglue-sdk")

# The macOS consumer must package the workspace MoltenVK build.  The generic
# RexGlue staging code still supplies the Vulkan loader and SDK support files,
# but an installed MoltenVK may not contain the fixes carried by this tree.
if(APPLE AND NOT TARGET MoltenVK)
    add_subdirectory(
        "${CMAKE_SOURCE_DIR}/thirdparty/MoltenVK"
        "${CMAKE_BINARY_DIR}/thirdparty/MoltenVK"
    )
endif()

add_subdirectory("${REXSDK_DIR}/gta4-recomp" "${CMAKE_CURRENT_BINARY_DIR}/gta4-recomp")
