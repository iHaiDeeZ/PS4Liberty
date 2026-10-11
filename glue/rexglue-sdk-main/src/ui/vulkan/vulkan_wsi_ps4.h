/**
 * @file        ui/vulkan/vulkan_wsi_ps4.h
 * @brief       VK_KHR_surface / VK_KHR_swapchain for the PS4 on top of VideoOut
 *
 * The PS4 build links Mesa RADV with no window system integration, so these
 * extensions are provided here instead: the surface and swapchain entry points
 * in the VulkanInstance / VulkanDevice function tables are replaced with
 * implementations that render into linear host-visible images registered as
 * VideoOut buffers and flip VideoOut to them (the approach DolphinPS4's Vulkan
 * backend uses). The presenter's own swapchain code runs unchanged.
 *
 * @license     BSD 3-Clause License
 */

#pragma once

#include <rex/platform.h>

#if REX_PLATFORM_PS4

#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/instance.h>

namespace rex::ui::vulkan::ps4_wsi {

// Fills the VK_KHR_surface entry points of `ifn`.
void InstallInstanceFunctions(VulkanInstance::Functions& ifn);

// Fills the VK_KHR_swapchain entry points of `dfn` for `device`, whose other
// device functions must already be loaded.
void InstallDeviceFunctions(const VulkanDevice& device, VulkanDevice::Functions& dfn);

// Opens the main VideoOut port as a VkSurfaceKHR for the functions above.
VkResult CreateVideoOutSurface(VkSurfaceKHR* surface_out);

}  // namespace rex::ui::vulkan::ps4_wsi

#endif  // REX_PLATFORM_PS4
