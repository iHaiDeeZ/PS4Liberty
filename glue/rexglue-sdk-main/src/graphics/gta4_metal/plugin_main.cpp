#include <string_view>

#include <rex/logging.h>
#include <rex/system/gpu_plugin.h>

#include "graphics_system.h"

extern "C" REX_GPU_PLUGIN_EXPORT uint32_t REX_GPU_PLUGIN_SYMBOL(rex_gpu_abi_version)() {
  return rex::system::kGpuPluginAbiVersion;
}

extern "C" REX_GPU_PLUGIN_EXPORT rex::system::IGraphicsSystem* REX_GPU_PLUGIN_SYMBOL(rex_gpu_create)(
    uint32_t abi, const rex::system::GpuCreateInfo* info) {
  if (abi != rex::system::kGpuPluginAbiVersion || !info ||
      info->struct_size < sizeof(rex::system::GpuCreateInfo)) {
    REXLOG_ERROR("gta4-metal: incompatible GPU plugin ABI or creation structure");
    return nullptr;
  }
  const std::string_view backend = info->backend ? info->backend : "any";
  if (backend != "any" && backend != "metal") {
    REXLOG_ERROR("gta4-metal: backend '{}' is not supported", backend);
    return nullptr;
  }
  return new rex::graphics::gta4_metal::Gta4MetalGraphicsSystem();
}
