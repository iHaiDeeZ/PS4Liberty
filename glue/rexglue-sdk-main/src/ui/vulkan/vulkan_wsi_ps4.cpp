/**
 * @file        ui/vulkan/vulkan_wsi_ps4.cpp
 * @brief       VK_KHR_surface / VK_KHR_swapchain for the PS4 on top of VideoOut
 *
 * Swapchain images are linear images in host-visible memory registered as
 * VideoOut buffers (on the PS4 a GPU address is the CPU address). Presenting
 * queues a VideoOut flip behind the frame's GPU work through Mesa's GNM
 * winsys, falling back to waiting for the work and flipping from the CPU.
 * Based on DolphinPS4's VKSwapChain PS4 path.
 *
 * @license     BSD 3-Clause License
 */

#include "vulkan_wsi_ps4.h"

#if REX_PLATFORM_PS4

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include <orbis/SystemService.h>
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

#include <rex/logging.h>
#include <rex/ui/surface_ps4.h>

// Mesa's GNM winsys (DolphinPS4 port/mesa/ac_ps4_drm.c): queues a VideoOut
// flip behind the GPU work submitted so far. Returns 0 on success.
extern "C" int ac_ps4_submit_flip(int video, unsigned buffer, unsigned mode, int64_t arg);
extern "C" char __text_start[];  // OpenOrbis link.x: start of .text (ELF address 0)
// os/ps4/runtime/ps4_crashlog.cpp: code addresses on the stack into boot-trace.log.
extern "C" __attribute__((weak)) void ps4_trace_callers(const char* what);

namespace rex::ui::vulkan::ps4_wsi {

namespace {

constexpr uint32_t kMaxVideoOutBuffers = 16;  // buffer indices per VideoOut port
constexpr uint32_t kMinImageCount = 2;
constexpr uint32_t kMaxImageCount = 4;
// VideoOut scans out of 2 MiB aligned buffers.
constexpr VkDeviceSize kVideoOutBufferAlignment = VkDeviceSize(2) << 20;
// ARGB / ABGR in register order, i.e. B8G8R8A8 / R8G8B8A8 in memory.
constexpr int32_t kPixelFormatA8R8G8B8Srgb = int32_t(0x80000000u);
constexpr int32_t kPixelFormatA8B8G8R8Srgb = int32_t(uint32_t(ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB));

struct VideoOutSurface {
  int32_t video = -1;
  OrbisKernelEqueue flip_queue = 0;
  // Frames are always 1080p; VideoOut scales them to the output mode.
  uint32_t width = PS4VideoOutSurface::kWidth;
  uint32_t height = PS4VideoOutSurface::kHeight;
  std::mutex mutex;                // guards the fields below
  uint32_t used_buffer_slots = 0;  // VideoOut buffer indices held by swapchains
  int64_t flips_submitted = 0;     // each flip's flipArg is its number (from 1)
};

struct Swapchain {
  VideoOutSurface* surface = nullptr;
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t width = 0;
  uint32_t height = 0;
  bool vsync = true;
  std::vector<VkImage> images;
  std::vector<VkDeviceMemory> memory;
  // Per image: completion fence of its last present submission, and the
  // flipArg of the flip that last showed it (0 = never shown).
  std::vector<VkFence> present_fences;
  std::vector<int64_t> last_flip;
  uint32_t first_buffer = 0;  // VideoOut buffer index of images[0]
  uint32_t buffer_slot_mask = 0;
  int32_t buffer_set = -1;
  uint32_t next_image = 0;
};

const VulkanDevice* g_device = nullptr;
PFN_vkGetImageSubresourceLayout g_get_image_subresource_layout = nullptr;

// The WSI's own submissions (semaphore signals and waits, fences) carry this
// recorded command buffer: Mesa's GNM winsys rejects a submission without
// command buffers (-EINVAL), which RADV reports as VK_ERROR_DEVICE_LOST.
std::mutex g_noop_mutex;
VkCommandPool g_noop_pool = VK_NULL_HANDLE;
VkCommandBuffer g_noop_command_buffer = VK_NULL_HANDLE;

VkCommandBuffer NoopCommandBuffer() {
  std::lock_guard<std::mutex> lock(g_noop_mutex);
  if (g_noop_command_buffer != VK_NULL_HANDLE) {
    return g_noop_command_buffer;
  }
  const VulkanDevice::Functions& dfn = g_device->functions();
  const VkDevice device = g_device->device();
  VkCommandPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.queueFamilyIndex = g_device->queue_family_graphics_compute();
  if (dfn.vkCreateCommandPool(device, &pool_info, nullptr, &g_noop_pool) != VK_SUCCESS) {
    REXLOG_ERROR("PS4 WSI: vkCreateCommandPool failed");
    return VK_NULL_HANDLE;
  }
  VkCommandBufferAllocateInfo alloc_info = {};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = g_noop_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = 1;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  if (dfn.vkAllocateCommandBuffers(device, &alloc_info, &command_buffer) != VK_SUCCESS) {
    REXLOG_ERROR("PS4 WSI: vkAllocateCommandBuffers failed");
    return VK_NULL_HANDLE;
  }
  // Submitted any number of times, also while earlier submissions are pending.
  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
  dfn.vkBeginCommandBuffer(command_buffer, &begin_info);
  // Something for the command stream to hold.
  VkMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  dfn.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0,
                           nullptr);
  if (dfn.vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
    REXLOG_ERROR("PS4 WSI: vkEndCommandBuffer failed");
    return VK_NULL_HANDLE;
  }
  g_noop_command_buffer = command_buffer;
  return command_buffer;
}

// GPU-side semaphore waits: Mesa's GNM winsys can't export or import sync files
// (-ENOSYS), and RADV turns every submission that waits on a semaphore into
// VK_ERROR_DEVICE_LOST. The winsys runs all work on one in-order GFX ring, so a
// submission already starts after everything submitted before it: the waits
// are dropped (signals still work). DolphinPS4 never waits on semaphores.
PFN_vkQueueSubmit g_queue_submit = nullptr;

// Temporary: the app submitted ~5700 times a second while presenting a few
// frames, and the console froze. Every kSubmitReportInterval-th submission is
// logged with its caller (ELF address, symbolize with llvm-symbolizer).
constexpr uint64_t kSubmitReportInterval = 2000;
std::atomic<uint64_t> g_submit_count{0};

VKAPI_ATTR VkResult VKAPI_CALL QueueSubmitWithoutWaits(VkQueue queue, uint32_t submit_count,
                                                       const VkSubmitInfo* submits,
                                                       VkFence fence) {
  static const auto s_start = std::chrono::steady_clock::now();
  const uint64_t number = g_submit_count.fetch_add(1, std::memory_order_relaxed) + 1;
  if (number % kSubmitReportInterval == 0 && number <= 100 * kSubmitReportInterval) {
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - s_start).count();
    uint32_t command_buffers = 0;
    for (uint32_t i = 0; i < submit_count; ++i) {
      command_buffers += submits[i].commandBufferCount;
    }
    REXLOG_WARN(
        "PS4 WSI: vkQueueSubmit #{} at {:.1f} s from elf {:#x}: queue {}, {} submits, "
        "{} command buffers, fence {}",
        number, seconds,
        uint64_t(reinterpret_cast<uintptr_t>(__builtin_return_address(0)) -
                 reinterpret_cast<uintptr_t>(__text_start)),
        static_cast<const void*>(queue), submit_count, command_buffers, fence != VK_NULL_HANDLE);
    // Deeper callers (stack scan) into boot-trace.log.
    if (ps4_trace_callers) {
      char what[64];
      std::snprintf(what, sizeof(what), "vkQueueSubmit #%llu", static_cast<unsigned long long>(number));
      ps4_trace_callers(what);
    }
  }
  bool has_waits = false;
  for (uint32_t i = 0; i < submit_count; ++i) {
    has_waits |= submits[i].waitSemaphoreCount != 0;
  }
  if (!has_waits) {
    return g_queue_submit(queue, submit_count, submits, fence);
  }
  std::vector<VkSubmitInfo> stripped(submits, submits + submit_count);
  for (VkSubmitInfo& submit : stripped) {
    submit.waitSemaphoreCount = 0;
    submit.pWaitSemaphores = nullptr;
    submit.pWaitDstStageMask = nullptr;
    // Timeline wait values (VkTimelineSemaphoreSubmitInfo) are ignored without
    // wait semaphores.
  }
  return g_queue_submit(queue, submit_count, stripped.data(), fence);
}

// Points a WSI submission at the no-op command buffer (if it could be made).
void AddNoopCommandBuffer(VkSubmitInfo& submit, VkCommandBuffer& storage) {
  storage = NoopCommandBuffer();
  if (storage != VK_NULL_HANDLE) {
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &storage;
  }
}

VideoOutSurface* AsSurface(VkSurfaceKHR surface) {
  return reinterpret_cast<VideoOutSurface*>(surface);
}

Swapchain* AsSwapchain(VkSwapchainKHR swapchain) {
  return reinterpret_cast<Swapchain*>(swapchain);
}

template <typename T, size_t N>
VkResult FillArray(const T (&values)[N], uint32_t* count, T* out) {
  if (!out) {
    *count = uint32_t(N);
    return VK_SUCCESS;
  }
  const uint32_t written = std::min(*count, uint32_t(N));
  std::copy_n(values, written, out);
  *count = written;
  return written < N ? VK_INCOMPLETE : VK_SUCCESS;
}

constexpr VkSurfaceFormatKHR kSurfaceFormats[] = {
    {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
};

constexpr VkPresentModeKHR kPresentModes[] = {
    VK_PRESENT_MODE_FIFO_KHR,
    VK_PRESENT_MODE_IMMEDIATE_KHR,
};

// Flip completed most recently on the port (its flipArg), or -1 on error.
int64_t CompletedFlip(const VideoOutSurface& surface) {
  OrbisVideoOutFlipStatus status = {};
  if (sceVideoOutGetFlipStatus(surface.video, &status) < 0) {
    return -1;
  }
  return status.flipArg;
}

uint32_t FindVideoOutMemoryType(uint32_t type_bits) {
  VkPhysicalDeviceMemoryProperties properties;
  g_device->vulkan_instance()->functions().vkGetPhysicalDeviceMemoryProperties(
      g_device->physical_device(), &properties);
  const VkMemoryPropertyFlags preferred[] = {
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
  };
  for (VkMemoryPropertyFlags wanted : preferred) {
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
      if ((type_bits & (1u << i)) &&
          (properties.memoryTypes[i].propertyFlags & wanted) == wanted) {
        return i;
      }
    }
  }
  return UINT32_MAX;
}

void DestroySwapchainResources(Swapchain& chain) {
  const VulkanDevice::Functions& dfn = g_device->functions();
  const VkDevice device = g_device->device();
  if (!chain.present_fences.empty()) {
    dfn.vkWaitForFences(device, uint32_t(chain.present_fences.size()),
                        chain.present_fences.data(), VK_TRUE, UINT64_MAX);
  }
  VideoOutSurface& surface = *chain.surface;
  if (chain.buffer_set >= 0) {
    // Let queued flips reach the screen before the buffers go away.
    while (sceVideoOutIsFlipPending(surface.video) > 0) {
      sceKernelUsleep(1000);
    }
    const int32_t result = sceVideoOutUnregisterBuffers(surface.video, chain.buffer_set);
    std::lock_guard<std::mutex> lock(surface.mutex);
    if (result < 0) {
      // Typically one of the buffers is still on screen. Keep its indices
      // reserved so a later swapchain doesn't reuse them.
      REXLOG_WARN("PS4 WSI: sceVideoOutUnregisterBuffers failed: {:#x}", uint32_t(result));
    } else {
      surface.used_buffer_slots &= ~chain.buffer_slot_mask;
    }
  } else if (chain.buffer_slot_mask) {
    std::lock_guard<std::mutex> lock(surface.mutex);
    surface.used_buffer_slots &= ~chain.buffer_slot_mask;
  }
  for (VkFence fence : chain.present_fences) {
    dfn.vkDestroyFence(device, fence, nullptr);
  }
  for (VkImage image : chain.images) {
    if (image != VK_NULL_HANDLE) {
      dfn.vkDestroyImage(device, image, nullptr);
    }
  }
  for (VkDeviceMemory memory : chain.memory) {
    if (memory != VK_NULL_HANDLE) {
      dfn.vkFreeMemory(device, memory, nullptr);
    }
  }
  chain.present_fences.clear();
  chain.images.clear();
  chain.memory.clear();
}

// ── VK_KHR_surface ───────────────────────────────────────────────────────────

VKAPI_ATTR void VKAPI_CALL DestroySurface(VkInstance /*instance*/, VkSurfaceKHR surface,
                                          const VkAllocationCallbacks* /*allocator*/) {
  VideoOutSurface* video_out_surface = AsSurface(surface);
  if (!video_out_surface) {
    return;
  }
  if (video_out_surface->video >= 0) {
    sceVideoOutClose(video_out_surface->video);
  }
  if (video_out_surface->flip_queue) {
    sceKernelDeleteEqueue(video_out_surface->flip_queue);
  }
  delete video_out_surface;
}

VKAPI_ATTR VkResult VKAPI_CALL GetSurfaceSupport(VkPhysicalDevice /*physical_device*/,
                                                 uint32_t /*queue_family_index*/,
                                                 VkSurfaceKHR /*surface*/,
                                                 VkBool32* supported) {
  *supported = VK_TRUE;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetSurfaceCapabilities(VkPhysicalDevice /*physical_device*/,
                                                      VkSurfaceKHR surface,
                                                      VkSurfaceCapabilitiesKHR* capabilities) {
  const VideoOutSurface& video_out_surface = *AsSurface(surface);
  *capabilities = {};
  capabilities->minImageCount = kMinImageCount;
  capabilities->maxImageCount = kMaxImageCount;
  capabilities->currentExtent = {video_out_surface.width, video_out_surface.height};
  capabilities->minImageExtent = capabilities->currentExtent;
  capabilities->maxImageExtent = capabilities->currentExtent;
  capabilities->maxImageArrayLayers = 1;
  capabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  capabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  capabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  capabilities->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetSurfaceFormats(VkPhysicalDevice /*physical_device*/,
                                                 VkSurfaceKHR /*surface*/, uint32_t* count,
                                                 VkSurfaceFormatKHR* formats) {
  return FillArray(kSurfaceFormats, count, formats);
}

VKAPI_ATTR VkResult VKAPI_CALL GetSurfacePresentModes(VkPhysicalDevice /*physical_device*/,
                                                      VkSurfaceKHR /*surface*/, uint32_t* count,
                                                      VkPresentModeKHR* present_modes) {
  return FillArray(kPresentModes, count, present_modes);
}

// ── VK_KHR_swapchain ─────────────────────────────────────────────────────────

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchain(VkDevice device,
                                               const VkSwapchainCreateInfoKHR* create_info,
                                               const VkAllocationCallbacks* /*allocator*/,
                                               VkSwapchainKHR* swapchain_out) {
  const VulkanDevice::Functions& dfn = g_device->functions();
  VideoOutSurface& surface = *AsSurface(create_info->surface);

  int32_t pixel_format;
  switch (create_info->imageFormat) {
    case VK_FORMAT_B8G8R8A8_UNORM:
      pixel_format = kPixelFormatA8R8G8B8Srgb;
      break;
    case VK_FORMAT_R8G8B8A8_UNORM:
      pixel_format = kPixelFormatA8B8G8R8Srgb;
      break;
    default:
      REXLOG_ERROR("PS4 WSI: unsupported swapchain format {}", uint32_t(create_info->imageFormat));
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
  }

  auto chain = std::make_unique<Swapchain>();
  chain->surface = &surface;
  chain->format = create_info->imageFormat;
  chain->width = create_info->imageExtent.width;
  chain->height = create_info->imageExtent.height;
  chain->vsync = create_info->presentMode != VK_PRESENT_MODE_IMMEDIATE_KHR;
  const uint32_t image_count =
      std::clamp(create_info->minImageCount, kMinImageCount, kMaxImageCount);

  // Reserve consecutive VideoOut buffer indices: the swapchain being replaced
  // still holds its own until it is destroyed.
  {
    std::lock_guard<std::mutex> lock(surface.mutex);
    const uint32_t mask = (1u << image_count) - 1;
    bool found = false;
    for (uint32_t first = 0; first + image_count <= kMaxVideoOutBuffers; ++first) {
      if (!(surface.used_buffer_slots & (mask << first))) {
        chain->first_buffer = first;
        chain->buffer_slot_mask = mask << first;
        surface.used_buffer_slots |= chain->buffer_slot_mask;
        found = true;
        break;
      }
    }
    if (!found) {
      REXLOG_ERROR("PS4 WSI: no free VideoOut buffer indices for {} images", image_count);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
  }

  std::vector<void*> addresses(image_count, nullptr);
  uint32_t pixel_pitch = chain->width;
  chain->images.resize(image_count, VK_NULL_HANDLE);
  chain->memory.resize(image_count, VK_NULL_HANDLE);
  chain->last_flip.assign(image_count, 0);
  for (uint32_t i = 0; i < image_count; ++i) {
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = chain->format;
    image_info.extent = {chain->width, chain->height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;
    image_info.usage = create_info->imageUsage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult result = dfn.vkCreateImage(device, &image_info, nullptr, &chain->images[i]);
    if (result != VK_SUCCESS) {
      REXLOG_ERROR("PS4 WSI: vkCreateImage failed: {}", int32_t(result));
      DestroySwapchainResources(*chain);
      return result;
    }
    VkMemoryRequirements requirements;
    dfn.vkGetImageMemoryRequirements(device, chain->images[i], &requirements);
    // Over-allocate and bind at a 2 MiB aligned address for VideoOut.
    VkMemoryAllocateInfo allocate_info = {};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size + kVideoOutBufferAlignment;
    allocate_info.memoryTypeIndex = FindVideoOutMemoryType(requirements.memoryTypeBits);
    if (allocate_info.memoryTypeIndex == UINT32_MAX) {
      REXLOG_ERROR("PS4 WSI: no host-visible memory type for swapchain images");
      DestroySwapchainResources(*chain);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    result = dfn.vkAllocateMemory(device, &allocate_info, nullptr, &chain->memory[i]);
    uint8_t* base = nullptr;
    if (result == VK_SUCCESS) {
      result = dfn.vkMapMemory(device, chain->memory[i], 0, VK_WHOLE_SIZE, 0,
                               reinterpret_cast<void**>(&base));
    }
    if (result != VK_SUCCESS) {
      REXLOG_ERROR("PS4 WSI: swapchain image memory allocation failed: {}", int32_t(result));
      DestroySwapchainResources(*chain);
      return result;
    }
    const uintptr_t base_address = reinterpret_cast<uintptr_t>(base);
    VkDeviceSize offset =
        ((base_address + kVideoOutBufferAlignment - 1) & ~uintptr_t(kVideoOutBufferAlignment - 1)) -
        base_address;
    offset = (offset + requirements.alignment - 1) / requirements.alignment * requirements.alignment;
    result = dfn.vkBindImageMemory(device, chain->images[i], chain->memory[i], offset);
    if (result != VK_SUCCESS) {
      REXLOG_ERROR("PS4 WSI: vkBindImageMemory failed: {}", int32_t(result));
      DestroySwapchainResources(*chain);
      return result;
    }
    VkImageSubresource subresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    VkSubresourceLayout layout;
    g_get_image_subresource_layout(device, chain->images[i], &subresource, &layout);
    addresses[i] = base + offset + layout.offset;
    pixel_pitch = uint32_t(layout.rowPitch / 4);
  }

  OrbisVideoOutBufferAttribute attribute;
  sceVideoOutSetBufferAttribute(&attribute, uint32_t(pixel_format),
                                ORBIS_VIDEO_OUT_TILING_MODE_LINEAR,
                                ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9, chain->width, chain->height,
                                pixel_pitch);
  chain->buffer_set = sceVideoOutRegisterBuffers(surface.video, int32_t(chain->first_buffer),
                                                 addresses.data(), int32_t(image_count),
                                                 &attribute);
  if (chain->buffer_set < 0) {
    REXLOG_ERROR("PS4 WSI: sceVideoOutRegisterBuffers failed: {:#x}",
                 uint32_t(chain->buffer_set));
    chain->buffer_set = -1;
    DestroySwapchainResources(*chain);
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkFenceCreateInfo fence_info = {};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  chain->present_fences.resize(image_count, VK_NULL_HANDLE);
  for (VkFence& fence : chain->present_fences) {
    if (dfn.vkCreateFence(device, &fence_info, nullptr, &fence) != VK_SUCCESS) {
      DestroySwapchainResources(*chain);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
  }

  REXLOG_INFO("PS4 WSI: swapchain {} x {}x{}, pitch {}, VideoOut buffers {}..{}, {}", image_count,
              chain->width, chain->height, pixel_pitch, chain->first_buffer,
              chain->first_buffer + image_count - 1, chain->vsync ? "vsync" : "immediate");
  *swapchain_out = reinterpret_cast<VkSwapchainKHR>(chain.release());
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchain(VkDevice /*device*/, VkSwapchainKHR swapchain,
                                            const VkAllocationCallbacks* /*allocator*/) {
  Swapchain* chain = AsSwapchain(swapchain);
  if (!chain) {
    return;
  }
  DestroySwapchainResources(*chain);
  delete chain;
}

VKAPI_ATTR VkResult VKAPI_CALL GetSwapchainImages(VkDevice /*device*/, VkSwapchainKHR swapchain,
                                                  uint32_t* count, VkImage* images) {
  const Swapchain& chain = *AsSwapchain(swapchain);
  const uint32_t image_count = uint32_t(chain.images.size());
  if (!images) {
    *count = image_count;
    return VK_SUCCESS;
  }
  const uint32_t written = std::min(*count, image_count);
  std::copy_n(chain.images.data(), written, images);
  *count = written;
  return written < image_count ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImage(VkDevice /*device*/, VkSwapchainKHR swapchain,
                                                uint64_t timeout, VkSemaphore semaphore,
                                                VkFence fence, uint32_t* image_index) {
  Swapchain& chain = *AsSwapchain(swapchain);
  VideoOutSurface& surface = *chain.surface;
  const uint32_t image = chain.next_image;

  // The image is free once a later flip has replaced it on screen: its own
  // flip (if any) has completed and is no longer the newest completed one.
  // Like DolphinPS4, wait for that at most kMaxOffScreenWait and then render
  // into the image anyway: an unbounded wait for a flip that never completes
  // hung the app (and with it the console).
  constexpr auto kMaxOffScreenWait = std::chrono::milliseconds(200);
  const int64_t image_flip = chain.last_flip[image];
  if (image_flip) {
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = timeout == UINT64_MAX
                              ? std::chrono::steady_clock::time_point::max()
                              : start + std::chrono::nanoseconds(timeout);
    while (true) {
      OrbisVideoOutFlipStatus status = {};
      if (sceVideoOutGetFlipStatus(surface.video, &status) < 0 || status.flipArg > image_flip) {
        break;
      }
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        return timeout ? VK_TIMEOUT : VK_NOT_READY;
      }
      if (now - start >= kMaxOffScreenWait) {
        static int s_reports = 0;
        if (s_reports < 20) {
          ++s_reports;
          int64_t submitted;
          {
            std::lock_guard<std::mutex> lock(surface.mutex);
            submitted = surface.flips_submitted;
          }
          REXLOG_WARN(
              "PS4 WSI: image {} still on screen after {} ms (its flip {}, flips submitted {}, "
              "completed flipArg {}, flips done {}, pending {} (GPU {}), current buffer {})",
              image, kMaxOffScreenWait.count(), image_flip, submitted, status.flipArg, status.num,
              status.numFlipPending, status.numGpuFlipPending, status.currentBuffer);
        }
        break;
      }
      OrbisKernelEvent event;
      int event_count = 0;
      OrbisKernelUseconds wait_us = 5000;
      sceKernelWaitEqueue(surface.flip_queue, &event, 1, &event_count, &wait_us);
    }
  }
  chain.next_image = (image + 1) % uint32_t(chain.images.size());
  *image_index = image;

  // The image is ready now; signal the semaphore / fence through the queue.
  if (semaphore != VK_NULL_HANDLE || fence != VK_NULL_HANDLE) {
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.signalSemaphoreCount = semaphore != VK_NULL_HANDLE ? 1 : 0;
    submit.pSignalSemaphores = &semaphore;
    VkCommandBuffer noop;
    AddNoopCommandBuffer(submit, noop);
    const VulkanDevice::Queue::Acquisition queue =
        g_device->AcquireQueue(g_device->queue_family_graphics_compute(), 0);
    const VkResult result = g_device->functions().vkQueueSubmit(queue.queue(), 1, &submit, fence);
    if (result != VK_SUCCESS) {
      return result;
    }
  }
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL QueuePresent(VkQueue queue, const VkPresentInfoKHR* present_info) {
  const VulkanDevice::Functions& dfn = g_device->functions();
  const VkDevice device = g_device->device();
  std::vector<VkPipelineStageFlags> wait_stages(present_info->waitSemaphoreCount,
                                                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  VkResult overall_result = VK_SUCCESS;
  for (uint32_t i = 0; i < present_info->swapchainCount; ++i) {
    Swapchain& chain = *AsSwapchain(present_info->pSwapchains[i]);
    VideoOutSurface& surface = *chain.surface;
    const uint32_t image = present_info->pImageIndices[i];

    // Consume the wait semaphores (once) in a submission whose fence marks the
    // frame's GPU work as done, for the CPU-flip fallback.
    VkFence present_fence = chain.present_fences[image];
    dfn.vkWaitForFences(device, 1, &present_fence, VK_TRUE, UINT64_MAX);
    dfn.vkResetFences(device, 1, &present_fence);
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if (i == 0) {
      submit.waitSemaphoreCount = present_info->waitSemaphoreCount;
      submit.pWaitSemaphores = present_info->pWaitSemaphores;
      submit.pWaitDstStageMask = wait_stages.data();
    }
    VkCommandBuffer noop;
    AddNoopCommandBuffer(submit, noop);
    VkResult result = dfn.vkQueueSubmit(queue, 1, &submit, present_fence);

    if (result == VK_SUCCESS) {
      std::lock_guard<std::mutex> lock(surface.mutex);
      const int64_t flip_arg = surface.flips_submitted + 1;
      const unsigned buffer = chain.first_buffer + image;
      const unsigned mode = chain.vsync ? ORBIS_VIDEO_OUT_FLIP_VSYNC : ORBIS_VIDEO_OUT_FLIP_HSYNC;
      // GPU-side flip, queued behind the frame: no CPU wait.
      int flip_result = ac_ps4_submit_flip(surface.video, buffer, mode, flip_arg);
      if (flip_result != 0) {
        dfn.vkWaitForFences(device, 1, &present_fence, VK_TRUE, UINT64_MAX);
        flip_result = sceVideoOutSubmitFlip(surface.video, int32_t(buffer), mode, flip_arg);
      }
      if (flip_result < 0) {
        REXLOG_ERROR("PS4 WSI: flip of VideoOut buffer {} failed: {:#x}", buffer,
                     uint32_t(flip_result));
        result = VK_ERROR_SURFACE_LOST_KHR;
      } else {
        surface.flips_submitted = flip_arg;
        chain.last_flip[image] = flip_arg;
        if (flip_arg == 1) {
          // The system keeps the package's launch splash (sce_sys/pic1.png) over the app's
          // VideoOut output until the app hides it: every frame was flipped, but the TV kept
          // showing the splash art.
          const int32_t hide_result = sceSystemServiceHideSplashScreen();
          REXLOG_INFO("PS4 WSI: launch splash hidden ({:#x})", uint32_t(hide_result));
        }
        if (flip_arg <= 8) {
          OrbisVideoOutFlipStatus status = {};
          sceVideoOutGetFlipStatus(surface.video, &status);
          REXLOG_INFO(
              "PS4 WSI: flip {} of buffer {} queued ({}); completed flipArg {}, flips done {}, "
              "pending {} (GPU {})",
              flip_arg, buffer, flip_result == 0 ? "GPU-side" : "CPU", status.flipArg, status.num,
              status.numFlipPending, status.numGpuFlipPending);
        }
      }
    }

    if (present_info->pResults) {
      present_info->pResults[i] = result;
    }
    if (result != VK_SUCCESS && overall_result == VK_SUCCESS) {
      overall_result = result;
    }
  }
  return overall_result;
}

}  // namespace

void InstallInstanceFunctions(VulkanInstance::Functions& ifn) {
  ifn.vkDestroySurfaceKHR = DestroySurface;
  ifn.vkGetPhysicalDeviceSurfaceCapabilitiesKHR = GetSurfaceCapabilities;
  ifn.vkGetPhysicalDeviceSurfaceFormatsKHR = GetSurfaceFormats;
  ifn.vkGetPhysicalDeviceSurfacePresentModesKHR = GetSurfacePresentModes;
  ifn.vkGetPhysicalDeviceSurfaceSupportKHR = GetSurfaceSupport;
}

void InstallDeviceFunctions(const VulkanDevice& device, VulkanDevice::Functions& dfn) {
  g_device = &device;
  {
    // A new device: the old one's no-op command buffer is gone with it.
    std::lock_guard<std::mutex> lock(g_noop_mutex);
    g_noop_pool = VK_NULL_HANDLE;
    g_noop_command_buffer = VK_NULL_HANDLE;
  }
  g_get_image_subresource_layout = PFN_vkGetImageSubresourceLayout(
      device.vulkan_instance()->functions().vkGetDeviceProcAddr(device.device(),
                                                                "vkGetImageSubresourceLayout"));
  g_queue_submit = dfn.vkQueueSubmit;
  dfn.vkQueueSubmit = QueueSubmitWithoutWaits;
  dfn.vkAcquireNextImageKHR = AcquireNextImage;
  dfn.vkCreateSwapchainKHR = CreateSwapchain;
  dfn.vkDestroySwapchainKHR = DestroySwapchain;
  dfn.vkGetSwapchainImagesKHR = GetSwapchainImages;
  dfn.vkQueuePresentKHR = QueuePresent;
}

VkResult CreateVideoOutSurface(VkSurfaceKHR* surface_out) {
  auto surface = std::make_unique<VideoOutSurface>();
  surface->video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, nullptr);
  if (surface->video < 0) {
    REXLOG_ERROR("PS4 WSI: sceVideoOutOpen failed: {:#x}", uint32_t(surface->video));
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  OrbisVideoOutResolutionStatus resolution = {};
  sceVideoOutGetResolutionStatus(surface->video, &resolution);
  if (sceKernelCreateEqueue(&surface->flip_queue, "rex videoout flip") < 0) {
    surface->flip_queue = 0;
  } else {
    sceVideoOutAddFlipEvent(surface->flip_queue, surface->video, nullptr);
  }
  REXLOG_INFO("PS4 WSI: VideoOut port {} at {}x{} (display {}x{})", surface->video,
              surface->width, surface->height, resolution.width, resolution.height);
  *surface_out = reinterpret_cast<VkSurfaceKHR>(surface.release());
  return VK_SUCCESS;
}

}  // namespace rex::ui::vulkan::ps4_wsi

#endif  // REX_PLATFORM_PS4
