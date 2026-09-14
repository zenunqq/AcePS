/*
 * CommandProcessor.cpp implements a small, defensive Vulkan backend and the
 * supported PM4 packet subset. It can run without a window for CLI tests.
 */
#include "aceps/gpu/CommandProcessor.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>

#if defined(__linux__)
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#elif defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#endif

namespace aceps::gpu {
namespace {

constexpr std::uint8_t kItNop = 0x10;
constexpr std::uint8_t kItDrawIndex2 = 0x27;
constexpr std::uint8_t kItIndirectBuffer = 0x3F;
constexpr std::uint8_t kItSetContextReg = 0x69;
constexpr std::uint8_t kItDrawIndexAuto = 0x2D;
constexpr std::uint8_t kItDispatchDirect = 0x15;
constexpr std::size_t kMaxIndirectDepth = 32;

bool hasLayer(const std::vector<VkLayerProperties>& layers, const char* name) {
  return std::any_of(layers.begin(), layers.end(), [name](const auto& layer) {
    return std::strcmp(layer.layerName, name) == 0;
  });
}

} // namespace

CommandProcessor::~CommandProcessor() { shutdown(); }

bool CommandProcessor::checkResult(VkResult result, const char* operation,
                                   std::string& error) const {
  if (result == VK_SUCCESS) return true;
  error = operation;
  error += " failed with Vulkan result ";
  error += std::to_string(static_cast<int>(result));
  return false;
}

bool CommandProcessor::createInstance(std::string& error) {
  std::uint32_t layerCount = 0;
  std::vector<VkLayerProperties> layers;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
  layers.resize(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

  std::vector<const char*> enabledLayers;
#if !defined(NDEBUG)
  if (hasLayer(layers, "VK_LAYER_KHRONOS_validation")) enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif
  std::vector<const char*> extensions{"VK_KHR_surface"};
#if defined(__linux__)
  extensions.push_back("VK_KHR_xlib_surface");
#elif defined(_WIN32)
  extensions.push_back("VK_KHR_win32_surface");
#endif
  VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  application.pApplicationName = "AcePS";
  application.applicationVersion = 1;
  application.pEngineName = "AcePS";
  application.engineVersion = 1;
  application.apiVersion = VK_API_VERSION_1_0;
  VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  createInfo.pApplicationInfo = &application;
  createInfo.enabledLayerCount = static_cast<std::uint32_t>(enabledLayers.size());
  createInfo.ppEnabledLayerNames = enabledLayers.data();
  createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();
  return checkResult(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance", error);
}

bool CommandProcessor::createSurface(const NativeWindowHandle* windowHandle, std::string& error) {
  if (windowHandle == nullptr || windowHandle->display == nullptr || windowHandle->window == 0) return true;
#if defined(__linux__)
  VkXlibSurfaceCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
  createInfo.dpy = static_cast<Display*>(windowHandle->display);
  createInfo.window = static_cast<Window>(windowHandle->window);
  if (!checkResult(vkCreateXlibSurfaceKHR(instance_, &createInfo, nullptr, &surface_),
                   "vkCreateXlibSurfaceKHR", error)) return false;
#elif defined(_WIN32)
  VkWin32SurfaceCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
  createInfo.hinstance = GetModuleHandle(nullptr);
  createInfo.hwnd = reinterpret_cast<HWND>(windowHandle->window);
  if (!checkResult(vkCreateWin32SurfaceKHR(instance_, &createInfo, nullptr, &surface_),
                   "vkCreateWin32SurfaceKHR", error)) return false;
#else
  error = "native Vulkan surface creation is unsupported on this platform";
  return false;
#endif
  hasSurface_ = true;
  return true;
}

bool CommandProcessor::selectPhysicalDevice(std::string& error) {
  std::uint32_t count = 0;
  if (!checkResult(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
                   "vkEnumeratePhysicalDevices", error) || count == 0) {
    if (error.empty()) error = "no Vulkan physical device is available";
    return false;
  }
  std::vector<VkPhysicalDevice> devices(count);
  if (!checkResult(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
                   "vkEnumeratePhysicalDevices", error)) return false;
  auto score = [this](VkPhysicalDevice device) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    return (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 0) +
           static_cast<int>(properties.limits.maxImageDimension2D);
  };
  physicalDevice_ = *std::max_element(devices.begin(), devices.end(),
                                       [&](auto left, auto right) { return score(left) < score(right); });
  std::uint32_t familyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, nullptr);
  std::vector<VkQueueFamilyProperties> families(familyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, families.data());
  bool foundGraphics = false;
  bool foundPresent = !hasSurface_;
  for (std::uint32_t index = 0; index < familyCount; ++index) {
    if (!foundGraphics && (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      graphicsFamily_ = index;
      foundGraphics = true;
    }
    if (hasSurface_) {
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, index, surface_, &present);
      if (present == VK_TRUE && !foundPresent) {
        presentFamily_ = index;
        foundPresent = true;
      }
    }
  }
  if (!foundGraphics || !foundPresent) {
    error = "selected Vulkan device has no required graphics/present queue";
    return false;
  }
  if (!hasSurface_) presentFamily_ = graphicsFamily_;
  return true;
}

bool CommandProcessor::createDevice(std::string& error) {
  std::set<std::uint32_t> families{graphicsFamily_, presentFamily_};
  const float priority = 1.0F;
  std::vector<VkDeviceQueueCreateInfo> queues;
  for (const auto family : families) {
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    queues.push_back(queue);
  }
  std::vector<const char*> extensions;
  if (hasSurface_) extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queues.size());
  createInfo.pQueueCreateInfos = queues.data();
  createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();
  if (!checkResult(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
                   "vkCreateDevice", error)) return false;
  vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
  vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
  return true;
}

bool CommandProcessor::createSwapchain(std::uint32_t width, std::uint32_t height,
                                       std::string& error) {
  if (!hasSurface_) return true;
  VkSurfaceCapabilitiesKHR capabilities{};
  if (!checkResult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities),
                   "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", error)) return false;
  std::uint32_t formatCount = 0;
  std::uint32_t modeCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount, nullptr);
  if (formatCount == 0 || modeCount == 0) {
    error = "Vulkan surface has no supported formats or present modes";
    return false;
  }
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  std::vector<VkPresentModeKHR> modes(modeCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount, modes.data());
  const auto preferredFormat = std::find_if(formats.begin(), formats.end(), [](const auto& candidate) {
    return candidate.format == VK_FORMAT_B8G8R8A8_SRGB;
  });
  const auto format = preferredFormat == formats.end() ? formats.front() : *preferredFormat;
  swapchainFormat_ = format.format;
  const auto mode = std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()
                        ? VK_PRESENT_MODE_MAILBOX_KHR
                        : VK_PRESENT_MODE_FIFO_KHR;
  VkExtent2D extent{std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                    std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
  VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  createInfo.surface = surface_;
  createInfo.minImageCount = std::max(capabilities.minImageCount + 1, 2U);
  if (capabilities.maxImageCount != 0) createInfo.minImageCount = std::min(createInfo.minImageCount, capabilities.maxImageCount);
  createInfo.imageFormat = format.format;
  createInfo.imageColorSpace = format.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  const std::uint32_t queueFamilies[] = {graphicsFamily_, presentFamily_};
  if (graphicsFamily_ != presentFamily_) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilies;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }
  createInfo.preTransform = capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = mode;
  createInfo.clipped = VK_TRUE;
  if (!checkResult(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_), "vkCreateSwapchainKHR", error)) return false;
  vkGetSwapchainImagesKHR(device_, swapchain_, &formatCount, nullptr);
  swapchainImages_.resize(formatCount);
  vkGetSwapchainImagesKHR(device_, swapchain_, &formatCount, swapchainImages_.data());
  swapchainImageViews_.resize(formatCount);
  for (std::size_t index = 0; index < swapchainImages_.size(); ++index) {
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = swapchainImages_[index];
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format.format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    if (!checkResult(vkCreateImageView(device_, &view, nullptr, &swapchainImageViews_[index]),
                     "vkCreateImageView", error)) return false;
  }
  width_ = extent.width;
  height_ = extent.height;
  return true;
}

bool CommandProcessor::createRenderPass(std::string& error) {
  if (!hasSurface_) return true;
  VkAttachmentDescription attachment{};
  attachment.format = swapchainFormat_;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &reference;
  VkRenderPassCreateInfo createInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  createInfo.attachmentCount = 1;
  createInfo.pAttachments = &attachment;
  createInfo.subpassCount = 1;
  createInfo.pSubpasses = &subpass;
  return checkResult(vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_), "vkCreateRenderPass", error);
}

bool CommandProcessor::createFramebuffers(std::string& error) {
  if (!hasSurface_) return true;
  framebuffers_.resize(swapchainImageViews_.size());
  for (std::size_t index = 0; index < framebuffers_.size(); ++index) {
    VkFramebufferCreateInfo createInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    createInfo.renderPass = renderPass_;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &swapchainImageViews_[index];
    createInfo.width = width_;
    createInfo.height = height_;
    createInfo.layers = 1;
    if (!checkResult(vkCreateFramebuffer(device_, &createInfo, nullptr, &framebuffers_[index]),
                     "vkCreateFramebuffer", error)) return false;
  }
  return true;
}

bool CommandProcessor::createCommandResources(std::string& error) {
  VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool.queueFamilyIndex = graphicsFamily_;
  if (!checkResult(vkCreateCommandPool(device_, &pool, nullptr, &commandPool_), "vkCreateCommandPool", error)) return false;
  const auto count = std::max<std::size_t>(swapchainImages_.size(), 1);
  commandBuffers_.resize(count);
  VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocate.commandPool = commandPool_;
  allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate.commandBufferCount = static_cast<std::uint32_t>(count);
  return checkResult(vkAllocateCommandBuffers(device_, &allocate, commandBuffers_.data()),
                     "vkAllocateCommandBuffers", error);
}

bool CommandProcessor::initialize(const NativeWindowHandle* windowHandle,
                                  std::uint32_t width, std::uint32_t height,
                                  std::string& error) {
  shutdown();
  if (!createInstance(error) || !createSurface(windowHandle, error) || !selectPhysicalDevice(error) ||
      !createDevice(error) || !createSwapchain(width, height, error) || !createRenderPass(error) ||
      !createFramebuffers(error) || !createCommandResources(error)) {
    shutdown();
    return false;
  }
  width_ = width;
  height_ = height;
  initialized_ = true;
  error.clear();
  return true;
}

bool CommandProcessor::beginFrame(std::string& error) {
  std::scoped_lock lock(mutex_);
  if (!initialized_ || frameActive_) return false;
  if (hasSurface_) {
    const auto result = vkAcquireNextImageKHR(device_, swapchain_, std::numeric_limits<std::uint64_t>::max(), VK_NULL_HANDLE, VK_NULL_HANDLE, &imageIndex_);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
      if (!recreateSwapchain(error)) return false;
      const auto retry = vkAcquireNextImageKHR(device_, swapchain_, std::numeric_limits<std::uint64_t>::max(), VK_NULL_HANDLE, VK_NULL_HANDLE, &imageIndex_);
      if (!checkResult(retry, "vkAcquireNextImageKHR", error)) return false;
    }
    if (!checkResult(result, "vkAcquireNextImageKHR", error)) return false;
  }
  activeCommandBuffer_ = commandBuffers_[hasSurface_ ? imageIndex_ : 0];
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  if (!checkResult(vkBeginCommandBuffer(activeCommandBuffer_, &begin), "vkBeginCommandBuffer", error)) return false;
  frameActive_ = true;
  renderPassActive_ = false;
  error.clear();
  return true;
}

bool CommandProcessor::endFrame(std::string& error) {
  std::scoped_lock lock(mutex_);
  if (!frameActive_) return false;
  if (renderPassActive_) {
    vkCmdEndRenderPass(activeCommandBuffer_);
    renderPassActive_ = false;
  }
  if (!checkResult(vkEndCommandBuffer(activeCommandBuffer_), "vkEndCommandBuffer", error)) return false;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &activeCommandBuffer_;
  if (!checkResult(vkQueueSubmit(graphicsQueue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit", error)) return false;
  if (hasSurface_) {
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex_;
    const auto result = vkQueuePresentKHR(presentQueue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
      std::string ignored;
      (void)recreateSwapchain(ignored);
    } else if (!checkResult(result, "vkQueuePresentKHR", error)) return false;
  }
  frameActive_ = false;
  error.clear();
  return true;
}

bool CommandProcessor::clearScreen(float red, float green, float blue, std::string& error) {
  if (!frameActive_ && !beginFrame(error)) return false;
  if (hasSurface_ && !renderPassActive_) {
    VkClearColorValue color{{red, green, blue, 1.0F}};
    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.image = swapchainImages_[imageIndex_];
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(activeCommandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
    vkCmdClearColorImage(activeCommandBuffer_, swapchainImages_[imageIndex_], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &toTransfer.subresourceRange);
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toTransfer.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(activeCommandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
  }
  if (hasSurface_ && !renderPassActive_) {
    VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    render.renderPass = renderPass_;
    render.framebuffer = framebuffers_[imageIndex_];
    render.renderArea.extent = {width_, height_};
    vkCmdBeginRenderPass(activeCommandBuffer_, &render, VK_SUBPASS_CONTENTS_INLINE);
    renderPassActive_ = true;
  }
  error.clear();
  return true;
}

bool CommandProcessor::submit(std::span<const std::uint32_t> words, std::string& error) {
  if (!frameActive_ && !beginFrame(error)) return false;
  std::vector<Pm4PacketView> packets;
  if (!Pm4Parser::parseViews(words, packets, error)) return false;
  for (const auto& packet : packets) {
    if (!dispatch(packet, error)) return false;
  }
  return true;
}

bool CommandProcessor::dispatch(const Pm4PacketView& packet, std::string& error, std::size_t depth) {
  if (depth > kMaxIndirectDepth) {
    error = "PM4 indirect-buffer nesting is too deep";
    return false;
  }
  if (packet.type != Pm4PacketType::Type3) return true;
  switch (packet.opcode) {
  case kItNop:
    return true;
  case kItIndirectBuffer: {
    if (packet.payload.size() < 2) { error = "PM4 indirect-buffer packet is truncated"; return false; }
    const auto address = static_cast<std::uintptr_t>(packet.payload[0]) |
                         (static_cast<std::uintptr_t>(packet.payload[1]) << 32U);
    const auto words = packet.payload.size() >= 3 ? packet.payload[2] : 0;
    if (address == 0 || words == 0 || words > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t)) {
      error = "PM4 indirect-buffer range is invalid";
      return false;
    }
    std::vector<Pm4PacketView> child;
    const auto childWords = std::span<const std::uint32_t>(reinterpret_cast<const std::uint32_t*>(address), words);
    if (!Pm4Parser::parseViews(childWords, child, error)) return false;
    for (const auto& childPacket : child) if (!dispatch(childPacket, error, depth + 1)) return false;
    return true;
  }
  case kItSetContextReg:
    for (std::size_t index = 0; index + 1 < packet.payload.size(); index += 2) {
      contextRegisters_[packet.payload[index]] = packet.payload[index + 1];
    }
    return true;
  case kItDrawIndex2:
    if (packet.payload.empty()) { error = "PM4 indexed draw packet is truncated"; return false; }
    if (hasSurface_ && !renderPassActive_) {
      if (!clearScreen(0.0F, 0.0F, 0.0F, error)) return false;
    }
    if (hasSurface_) {
      vkCmdSetViewport(activeCommandBuffer_, 0, 1, &state_.viewport);
      vkCmdSetScissor(activeCommandBuffer_, 0, 1, &state_.scissor);
    }
    vkCmdDrawIndexed(activeCommandBuffer_, packet.payload[0], packet.payload.size() > 1 ? packet.payload[1] : 1, 0, 0, 0);
    return true;
  case kItDrawIndexAuto:
    if (packet.payload.empty()) { error = "PM4 automatic draw packet is truncated"; return false; }
    if (hasSurface_ && !renderPassActive_ && !clearScreen(0.0F, 0.0F, 0.0F, error)) return false;
    vkCmdDraw(activeCommandBuffer_, packet.payload[0], packet.payload.size() > 1 ? packet.payload[1] : 1, 0, 0);
    return true;
  case kItDispatchDirect:
    if (packet.payload.size() < 3) { error = "PM4 dispatch packet is truncated"; return false; }
    vkCmdDispatch(activeCommandBuffer_, packet.payload[0], packet.payload[1], packet.payload[2]);
    return true;
  default:
    return true;
  }
}

bool CommandProcessor::recreateSwapchain(std::string& error) {
  if (!hasSurface_) return true;
  vkDeviceWaitIdle(device_);
  destroySwapchainResources();
  return createSwapchain(width_, height_, error) && createRenderPass(error) && createFramebuffers(error);
}

void CommandProcessor::destroySwapchainResources() noexcept {
  if (device_ == VK_NULL_HANDLE) return;
  for (auto framebuffer : framebuffers_) vkDestroyFramebuffer(device_, framebuffer, nullptr);
  framebuffers_.clear();
  if (renderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, renderPass_, nullptr);
  renderPass_ = VK_NULL_HANDLE;
  for (auto view : swapchainImageViews_) vkDestroyImageView(device_, view, nullptr);
  swapchainImageViews_.clear();
  if (swapchain_ != VK_NULL_HANDLE) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = VK_NULL_HANDLE;
  swapchainImages_.clear();
}

void CommandProcessor::shutdown() noexcept {
  std::scoped_lock lock(mutex_);
  if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
  destroySwapchainResources();
  if (device_ != VK_NULL_HANDLE && commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, commandPool_, nullptr);
  commandPool_ = VK_NULL_HANDLE;
  commandBuffers_.clear();
  if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
  device_ = VK_NULL_HANDLE;
  if (instance_ != VK_NULL_HANDLE && surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
  surface_ = VK_NULL_HANDLE;
  if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
  instance_ = VK_NULL_HANDLE;
  physicalDevice_ = VK_NULL_HANDLE;
  hasSurface_ = false;
  frameActive_ = false;
  renderPassActive_ = false;
  initialized_ = false;
}

bool CommandProcessor::initialized() const noexcept {
  std::scoped_lock lock(mutex_);
  return initialized_;
}

const RenderState& CommandProcessor::renderState() const noexcept { return state_; }

} // namespace aceps::gpu

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
