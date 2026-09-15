/*
 * CommandProcessor.cpp implements a small, defensive Vulkan backend and the
 * supported PM4 packet subset. It can run without a window for CLI tests.
 */
#include "aceps/gpu/CommandProcessor.h"
#include "aceps/gpu/FramePresenter.h"
#include "aceps/gpu/TextureCache.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>
#include <memory>
#include <mutex>
#include <unordered_map>

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
constexpr std::uint8_t kItSetShReg = 0x76;
constexpr std::uint8_t kItDrawIndexAuto = 0x2D;
constexpr std::uint8_t kItDispatchDirect = 0x15;
constexpr std::uint8_t kItEventWrite = 0x46;
constexpr std::size_t kMaxIndirectDepth = 32;

bool hasLayer(const std::vector<VkLayerProperties>& layers, const char* name) {
  return std::any_of(layers.begin(), layers.end(), [name](const auto& layer) {
    return std::strcmp(layer.layerName, name) == 0;
  });
}

struct CommandExtras final {
  FramePresenter presenter;
  TextureCache textures;
  VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
  VkDescriptorSetLayout descSetLayout{VK_NULL_HANDLE};
  VkSampler defaultSampler{VK_NULL_HANDLE};
  VkPipelineLayout descriptorPipelineLayout{VK_NULL_HANDLE};
  bool presenterActive{false};
};

std::mutex extrasMutex;
std::unordered_map<const CommandProcessor*, std::unique_ptr<CommandExtras>> extras;

CommandExtras* commandExtras(const CommandProcessor* processor) noexcept {
  std::scoped_lock lock(extrasMutex);
  const auto found = extras.find(processor);
  return found == extras.end() ? nullptr : found->second.get();
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
  contextTracker_ = ContextTracker{};
  if (!createInstance(error) || !createSurface(windowHandle, error) || !selectPhysicalDevice(error) ||
      !createDevice(error)) {
    shutdown();
    return false;
  }
  if (!pipelineCache_.init(device_, error)) {
    shutdown();
    return false;
  }
  auto commandExtra = std::make_unique<CommandExtras>();
  if (!commandExtra->textures.init(device_, physicalDevice_, nullptr, error)) {
    shutdown();
    return false;
  }
  VkDescriptorSetLayoutBinding texBinding{};
  texBinding.binding = 1;
  texBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  texBinding.descriptorCount = 16;
  texBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &texBinding;
  if (!checkResult(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &commandExtra->descSetLayout),
                   "vkCreateDescriptorSetLayout", error)) {
    shutdown();
    return false;
  }
  VkPipelineLayoutCreateInfo descriptorPipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  descriptorPipelineInfo.setLayoutCount = 1;
  descriptorPipelineInfo.pSetLayouts = &commandExtra->descSetLayout;
  if (!checkResult(vkCreatePipelineLayout(device_, &descriptorPipelineInfo, nullptr,
                                          &commandExtra->descriptorPipelineLayout),
                   "vkCreatePipelineLayout", error)) {
    shutdown();
    return false;
  }
  VkDescriptorPoolSize poolSize{};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = 16U * 8U;
  VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.maxSets = 8;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  if (!checkResult(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &commandExtra->descriptorPool),
                   "descriptor pool creation", error)) {
    shutdown();
    return false;
  }
  VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
  if (!checkResult(vkCreateSampler(device_, &samplerInfo, nullptr, &commandExtra->defaultSampler),
                   "default sampler creation", error)) {
    shutdown();
    return false;
  }
  if (hasSurface_) {
    std::string presenterError;
    if (!commandExtra->presenter.init(instance_, physicalDevice_, device_, surface_, width, height,
                                     graphicsFamily_, presentFamily_, presenterError)) {
      error = "FramePresenter: " + presenterError;
      shutdown();
      return false;
    }
    commandExtra->presenterActive = true;
    renderPass_ = commandExtra->presenter.renderPass();
  } else if (!createCommandResources(error)) {
    shutdown();
    return false;
  }
  {
    std::scoped_lock extraLock(extrasMutex);
    extras.emplace(this, std::move(commandExtra));
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
  if (auto* commandExtra = commandExtras(this); commandExtra != nullptr && commandExtra->presenterActive) {
    if (!commandExtra->presenter.beginFrame(activeCommandBuffer_, imageIndex_, error)) return false;
    frameActive_ = true;
    renderPassActive_ = false;
    error.clear();
    return true;
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
  if (auto* commandExtra = commandExtras(this); commandExtra != nullptr && commandExtra->presenterActive) {
    if (!commandExtra->presenter.present(activeCommandBuffer_, VK_NULL_HANDLE, error)) return false;
    frameActive_ = false;
    error.clear();
    return true;
  }
  if (!checkResult(vkEndCommandBuffer(activeCommandBuffer_), "vkEndCommandBuffer", error)) return false;
  frameActive_ = false;
  error.clear();
  return true;
}

bool CommandProcessor::clearScreen(float red, float green, float blue, std::string& error) {
  if (!frameActive_ && !beginFrame(error)) return false;
  auto* commandExtra = commandExtras(this);
  const bool presenterActive = commandExtra != nullptr && commandExtra->presenterActive;
  if (hasSurface_ && !renderPassActive_ && !presenterActive) {
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
    render.framebuffer = presenterActive ? commandExtra->presenter.framebuffer(imageIndex_)
                                         : framebuffers_[imageIndex_];
    if (auto* commandExtra = commandExtras(this); commandExtra != nullptr && commandExtra->presenterActive) {
      render.framebuffer = commandExtra->presenter.framebuffer(imageIndex_);
    }
    render.renderArea.extent = {width_, height_};
    vkCmdBeginRenderPass(activeCommandBuffer_, &render, VK_SUBPASS_CONTENTS_INLINE);
    renderPassActive_ = true;
    if (presenterActive) {
      VkClearAttachment clear{};
      clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      clear.clearValue.color = {{red, green, blue, 1.0F}};
      VkClearRect rect{};
      rect.rect.extent = {width_, height_};
      rect.layerCount = 1;
      vkCmdClearAttachments(activeCommandBuffer_, 1, &clear, 1, &rect);
    }
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

bool CommandProcessor::translateShaderModule(std::span<const std::uint32_t> bytecode,
                                             shader::ShaderType type,
                                             std::string& error) {
  std::size_t hash = 1469598103934665603ULL;
  for (const auto word : bytecode) {
    hash ^= static_cast<std::size_t>(word);
    hash *= 1099511628211ULL;
  }
  hash ^= static_cast<std::size_t>(type);
  if (shaderModules_.contains(hash)) return true;
  std::vector<std::uint32_t> spirv;
  if (!shader::ShaderTranslator::translate(bytecode, type, spirv, error)) return false;
  VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  createInfo.codeSize = spirv.size() * sizeof(std::uint32_t);
  createInfo.pCode = spirv.data();
  VkShaderModule module = VK_NULL_HANDLE;
  if (!checkResult(vkCreateShaderModule(device_, &createInfo, nullptr, &module),
                   "vkCreateShaderModule", error)) return false;
  shaderModules_.emplace(hash, module);
  shaderCache_.set(type == shader::ShaderType::Vertex ? contextTracker_.vertexShaderAddr()
                                                       : contextTracker_.pixelShaderAddr(), module);
  return true;
}

bool CommandProcessor::bindGraphicsPipeline(std::string& error) {
  if (!hasSurface_ || !renderPassActive_) return true;
  const auto pipeline = pipelineCache_.getOrBuild(contextTracker_, renderPass_, shaderCache_, error);
  if (pipeline == VK_NULL_HANDLE) return false;
  vkCmdBindPipeline(activeCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  const auto& viewport = contextTracker_.viewport(0);
  VkViewport vkViewport{viewport.x, viewport.y, viewport.width == 0.0F ? static_cast<float>(width_) : viewport.width,
                        viewport.height == 0.0F ? static_cast<float>(height_) : viewport.height,
                        viewport.minDepth, viewport.maxDepth};
  VkRect2D scissor{{0, 0}, {width_, height_}};
  vkCmdSetViewport(activeCommandBuffer_, 0, 1, &vkViewport);
  vkCmdSetScissor(activeCommandBuffer_, 0, 1, &scissor);
  return true;
}

bool CommandProcessor::bindComputePipeline(std::string& error) {
  const auto pipeline = pipelineCache_.getOrBuildCompute(contextTracker_.computeShaderAddr(), shaderCache_, error);
  if (pipeline == VK_NULL_HANDLE) return false;
  vkCmdBindPipeline(activeCommandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  return true;
}

bool CommandProcessor::dispatch(const Pm4PacketView& packet, std::string& error, std::size_t depth) {
  if (depth > kMaxIndirectDepth) {
    error = "PM4 indirect-buffer nesting is too deep";
    return false;
  }
  if (packet.type != Pm4PacketType::Type3) return true;
  if (packet.opcode == kItEventWrite) {
    if (auto* commandExtra = commandExtras(this); commandExtra != nullptr) commandExtra->textures.invalidateRange(0, std::numeric_limits<std::uint64_t>::max() - 1U);
    return true;
  }
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
      contextTracker_.setContextReg(packet.payload[index], packet.payload[index + 1]);
    }
    return true;
  case kItSetShReg:
    // The safe inline form carries a register/value pair followed by bytecode.
    // Guest-address-backed shader memory is resolved before submission.
    if (packet.payload.size() < 3) return true;
    contextRegisters_[packet.payload[0]] = packet.payload[1];
    contextTracker_.setShReg(packet.payload[0], packet.payload[1]);
  return translateShaderModule(packet.payload.subspan(2),
                                  packet.payload[0] == 0x2C8U ? shader::ShaderType::Vertex
                                                               : shader::ShaderType::Fragment,
                                  error);
  case kItDrawIndex2:
    if (packet.payload.empty()) { error = "PM4 indexed draw packet is truncated"; return false; }
    if (hasSurface_ && !renderPassActive_) {
      if (!clearScreen(0.0F, 0.0F, 0.0F, error)) return false;
    }
    if (!bindGraphicsPipeline(error)) return false;
    if (auto* extra = commandExtras(this); extra != nullptr) {
      std::vector<VkImageView> boundViews;
      boundViews.reserve(16);
      for (int texSlot = 0; texSlot < 16; ++texSlot) {
        std::uint32_t tsharp[8]{};
        for (int word = 0; word < 8; ++word) { const auto found = contextRegisters_.find(static_cast<std::uint32_t>(texSlot * 8 + word)); if (found != contextRegisters_.end()) tsharp[word] = found->second; }
        if (tsharp[0] == 0U) continue;
        std::string texErr;
        const auto view = extra->textures.getOrUpload(tsharp, activeCommandBuffer_, texErr);
        if (view != VK_NULL_HANDLE) boundViews.push_back(view);
      }
      if (!boundViews.empty() && extra->descriptorPool != VK_NULL_HANDLE && extra->descSetLayout != VK_NULL_HANDLE && extra->defaultSampler != VK_NULL_HANDLE) {
        vkResetDescriptorPool(device_, extra->descriptorPool, 0);
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = extra->descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &extra->descSetLayout;
        VkDescriptorSet descSet = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device_, &allocInfo, &descSet) == VK_SUCCESS) {
          std::vector<VkDescriptorImageInfo> infos(boundViews.size());
          std::vector<VkWriteDescriptorSet> writes(boundViews.size());
          for (std::size_t index = 0; index < boundViews.size(); ++index) {
            infos[index] = {extra->defaultSampler, boundViews[index], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSet, 1, static_cast<std::uint32_t>(index), 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &infos[index], nullptr, nullptr};
          }
          vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
          vkCmdBindDescriptorSets(activeCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, extra->descriptorPipelineLayout, 1, 1, &descSet, 0, nullptr);
        }
      }
    }
    vkCmdDrawIndexed(activeCommandBuffer_, packet.payload[0], packet.payload.size() > 1 ? packet.payload[1] : 1, 0, 0, 0);
    contextTracker_.clearDirty();
    return true;
  case kItDrawIndexAuto:
    if (packet.payload.empty()) { error = "PM4 automatic draw packet is truncated"; return false; }
    if (hasSurface_ && !renderPassActive_ && !clearScreen(0.0F, 0.0F, 0.0F, error)) return false;
    if (!bindGraphicsPipeline(error)) return false;
    if (auto* extra = commandExtras(this); extra != nullptr) {
      std::vector<VkImageView> boundViews;
      boundViews.reserve(16);
      for (int texSlot = 0; texSlot < 16; ++texSlot) {
        std::uint32_t tsharp[8]{};
        for (int word = 0; word < 8; ++word) { const auto found = contextRegisters_.find(static_cast<std::uint32_t>(texSlot * 8 + word)); if (found != contextRegisters_.end()) tsharp[word] = found->second; }
        if (tsharp[0] == 0U) continue;
        std::string texErr;
        const auto view = extra->textures.getOrUpload(tsharp, activeCommandBuffer_, texErr);
        if (view != VK_NULL_HANDLE) boundViews.push_back(view);
      }
      if (!boundViews.empty() && extra->descriptorPool != VK_NULL_HANDLE && extra->descSetLayout != VK_NULL_HANDLE && extra->defaultSampler != VK_NULL_HANDLE) {
        vkResetDescriptorPool(device_, extra->descriptorPool, 0);
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = extra->descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &extra->descSetLayout;
        VkDescriptorSet descSet = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device_, &allocInfo, &descSet) == VK_SUCCESS) {
          std::vector<VkDescriptorImageInfo> infos(boundViews.size());
          std::vector<VkWriteDescriptorSet> writes(boundViews.size());
          for (std::size_t index = 0; index < boundViews.size(); ++index) {
            infos[index] = {extra->defaultSampler, boundViews[index], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSet, 1, static_cast<std::uint32_t>(index), 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &infos[index], nullptr, nullptr};
          }
          vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
          vkCmdBindDescriptorSets(activeCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, extra->descriptorPipelineLayout, 1, 1, &descSet, 0, nullptr);
        }
      }
    }
    vkCmdDraw(activeCommandBuffer_, packet.payload[0], packet.payload.size() > 1 ? packet.payload[1] : 1, 0, 0);
    contextTracker_.clearDirty();
    return true;
  case kItDispatchDirect:
    if (packet.payload.size() < 3) { error = "PM4 dispatch packet is truncated"; return false; }
    if (!bindComputePipeline(error)) return false;
    vkCmdDispatch(activeCommandBuffer_, packet.payload[0], packet.payload[1], packet.payload[2]);
    return true;
  default:
    return true;
  }
}

bool CommandProcessor::recreateSwapchain(std::string& error) {
  (void)error;
  return true;
}

void CommandProcessor::destroySwapchainResources() noexcept {
  framebuffers_.clear();
  renderPass_ = VK_NULL_HANDLE;
  swapchainImageViews_.clear();
  swapchain_ = VK_NULL_HANDLE;
  swapchainImages_.clear();
}

void CommandProcessor::shutdown() noexcept {
  std::scoped_lock lock(mutex_);
  if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
  {
    std::unique_ptr<CommandExtras> commandExtra;
    {
      std::scoped_lock extraLock(extrasMutex);
      const auto found = extras.find(this);
      if (found != extras.end()) {
        commandExtra = std::move(found->second);
        extras.erase(found);
      }
    }
    if (commandExtra) {
      commandExtra->textures.destroy();
      commandExtra->presenter.destroy();
      if (commandExtra->defaultSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device_, commandExtra->defaultSampler, nullptr);
        commandExtra->defaultSampler = VK_NULL_HANDLE;
      }
      if (commandExtra->descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, commandExtra->descriptorPool, nullptr);
        commandExtra->descriptorPool = VK_NULL_HANDLE;
      }
      if (commandExtra->descSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, commandExtra->descSetLayout, nullptr);
        commandExtra->descSetLayout = VK_NULL_HANDLE;
      }
      if (commandExtra->descriptorPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, commandExtra->descriptorPipelineLayout, nullptr);
        commandExtra->descriptorPipelineLayout = VK_NULL_HANDLE;
      }
      if (hasSurface_) renderPass_ = VK_NULL_HANDLE;
    }
  }
  pipelineCache_.destroy();
  destroySwapchainResources();
  for (const auto& [hash, module] : shaderModules_) {
    (void)hash;
    vkDestroyShaderModule(device_, module, nullptr);
  }
  shaderModules_.clear();
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
