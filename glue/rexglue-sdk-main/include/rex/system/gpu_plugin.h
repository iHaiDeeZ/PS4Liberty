/**
 * @file        system/gpu_plugin.h
 * @brief       Graphics plugin ABI and host-side loader
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 *
 * @remarks     Plugins may provide full GPU emulation or a title-specific
 *              graphics system. The factory ABI is shared by both.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include <rex/system/interfaces/graphics.h>

#if defined(_WIN32)
#define REX_GPU_PLUGIN_EXPORT __declspec(dllexport)
#else
#define REX_GPU_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

// Name of a plugin's exported entry point. A plugin built as a static library
// (for platforms that cannot load shared libraries, e.g. PS4) is compiled with
// REX_GPU_PLUGIN_STATIC_ID=<id>, which suffixes its exports with _<id> so that
// several plugins can be linked into one executable; the host then registers
// them with RegisterStaticGpuPlugin.
#define REX_GPU_PLUGIN_SYMBOL_JOIN2(symbol, id) symbol##_##id
#define REX_GPU_PLUGIN_SYMBOL_JOIN(symbol, id) REX_GPU_PLUGIN_SYMBOL_JOIN2(symbol, id)
#ifdef REX_GPU_PLUGIN_STATIC_ID
#define REX_GPU_PLUGIN_SYMBOL(symbol) REX_GPU_PLUGIN_SYMBOL_JOIN(symbol, REX_GPU_PLUGIN_STATIC_ID)
#else
#define REX_GPU_PLUGIN_SYMBOL(symbol) symbol
#endif

namespace rex::system {

// Bump on any change to GpuCreateInfo or to the IGraphicsSystem interface.
inline constexpr uint32_t kGpuPluginAbiVersion = 2;

inline constexpr const char* kGpuCreateSymbol = "rex_gpu_create";
inline constexpr const char* kGpuAbiVersionSymbol = "rex_gpu_abi_version";

struct GpuCreateInfo {
  uint32_t struct_size = 0;       // sizeof(GpuCreateInfo), set by the host
  const char* backend = nullptr;  // "d3d12", "vulkan", or "any"
};

// extern "C" exports every GPU plugin must provide:
//   uint32_t rex_gpu_abi_version(void);
//   rex::system::IGraphicsSystem* rex_gpu_create(uint32_t abi_version,
//                                                const GpuCreateInfo* info);
using GpuAbiVersionFn = uint32_t (*)();
using GpuCreateFn = IGraphicsSystem* (*)(uint32_t abi_version, const GpuCreateInfo* info);

// Makes a plugin that is linked into the executable (see
// REX_GPU_PLUGIN_SYMBOL) available to LoadGpuPlugin under `name`. Call before
// the runtime loads its graphics system.
void RegisterStaticGpuPlugin(std::string_view name, GpuAbiVersionFn abi_version_fn,
                             GpuCreateFn create_fn);

// Constructs the graphics system of plugin <name>: a plugin registered with
// RegisterStaticGpuPlugin, otherwise rexgpu-<name> loaded from the
// executable's directory. Returns nullptr after logging a detailed error
// (missing file, missing exports, ABI mismatch, or factory failure). The
// library handle is retained for process lifetime; plugins are never unloaded.
std::unique_ptr<IGraphicsSystem> LoadGpuPlugin(std::string_view name,
                                               std::string_view backend = "any");

}  // namespace rex::system
