#pragma once
/**
 * @file        rex/ui/surface_ps4.h
 * @brief       PS4 VideoOut surface for Vulkan presentation.
 *
 * The VkSurfaceKHR and swapchain behind it are implemented in
 * src/ui/vulkan/vulkan_wsi_ps4.cpp. Frames are 1080p; VideoOut scales them to
 * the console's output mode.
 */

#include <rex/ui/surface.h>

namespace rex {
namespace ui {

class PS4VideoOutSurface final : public Surface {
 public:
  static constexpr uint32_t kWidth = 1920;
  static constexpr uint32_t kHeight = 1080;

  TypeIndex GetType() const override { return kTypeIndex_PS4VideoOut; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override {
    width_out = kWidth;
    height_out = kHeight;
    return true;
  }
};

}  // namespace ui
}  // namespace rex
