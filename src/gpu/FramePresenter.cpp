#include "aceps/gpu/FramePresenter.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include <algorithm>
#include <limits>

namespace aceps::gpu {

FramePresenter::~FramePresenter() { destroy(); }

bool FramePresenter::init(const VkInstance instance, const VkPhysicalDevice physicalDevice,
                          const VkDevice device, const VkSurfaceKHR surface,
                          const std::uint32_t width, const std::uint32_t height,
                          const std::uint32_t graphicsFamily, const std::uint32_t presentFamily,
                          std::string& error) {
  destroy();
  if (instance == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE ||
      surface == VK_NULL_HANDLE) {
    error = "frame presenter requires a Vulkan surface and device";
    return false;
  }
  instance_ = instance;
  physicalDevice_ = physicalDevice;
  device_ = device;
  surface_ = surface;
  graphicsFamily_ = graphicsFamily;
  presentFamily_ = presentFamily;
  vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
  vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
  if (!createSwapchain(width, height, error) || !createRenderPass(error) || !createFramebuffers(error)) {
    destroy();
    return false;
  }
  VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  if (vkCreateSemaphore(device_, &semaphore, nullptr, &imageAvailable_) != VK_SUCCESS ||
      vkCreateSemaphore(device_, &semaphore, nullptr, &renderFinished_) != VK_SUCCESS) {
    error = "vkCreateSemaphore failed for frame presenter";
    destroy();
    return false;
  }
  error.clear();
  return true;
}

bool FramePresenter::createSwapchain(const std::uint32_t width, const std::uint32_t height, std::string& error) {
  VkSurfaceCapabilitiesKHR capabilities{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities) != VK_SUCCESS) {
    error = "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed";
    return false;
  }
  std::uint32_t formatCount = 0;
  if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr) != VK_SUCCESS || formatCount == 0) {
    error = "surface has no Vulkan formats";
    return false;
  }
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());
  const auto found = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& value) {
    return value.format == VK_FORMAT_B8G8R8A8_UNORM;
  });
  const auto selected = found == formats.end() ? formats.front() : *found;
  format_ = selected.format;
  extent_ = capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max()
                ? VkExtent2D{std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                             std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)}
                : capabilities.currentExtent;
  VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  createInfo.surface = surface_;
  createInfo.minImageCount = std::max(2U, capabilities.minImageCount);
  if (capabilities.maxImageCount != 0) createInfo.minImageCount = std::min(createInfo.minImageCount, capabilities.maxImageCount);
  createInfo.imageFormat = format_;
  createInfo.imageColorSpace = selected.colorSpace;
  createInfo.imageExtent = extent_;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  const std::uint32_t families[] = {graphicsFamily_, presentFamily_};
  createInfo.imageSharingMode = graphicsFamily_ == presentFamily_ ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
  if (graphicsFamily_ != presentFamily_) { createInfo.queueFamilyIndexCount = 2; createInfo.pQueueFamilyIndices = families; }
  createInfo.preTransform = capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  createInfo.clipped = VK_TRUE;
  if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_) != VK_SUCCESS) {
    error = "vkCreateSwapchainKHR failed";
    return false;
  }
  std::uint32_t count = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
  images_.resize(count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());
  views_.resize(count);
  for (std::size_t index = 0; index < images_.size(); ++index) {
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = images_[index]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format_;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; view.subresourceRange.levelCount = 1; view.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &view, nullptr, &views_[index]) != VK_SUCCESS) { error = "vkCreateImageView failed"; return false; }
  }
  return true;
}

bool FramePresenter::createRenderPass(std::string& error) {
  VkAttachmentDescription attachment{}; attachment.format = format_; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
  VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; info.attachmentCount = 1; info.pAttachments = &attachment; info.subpassCount = 1; info.pSubpasses = &subpass;
  if (vkCreateRenderPass(device_, &info, nullptr, &renderPass_) != VK_SUCCESS) { error = "vkCreateRenderPass failed"; return false; }
  return true;
}

bool FramePresenter::createFramebuffers(std::string& error) {
  framebuffers_.resize(views_.size());
  for (std::size_t index = 0; index < views_.size(); ++index) {
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; info.renderPass = renderPass_; info.attachmentCount = 1; info.pAttachments = &views_[index]; info.width = extent_.width; info.height = extent_.height; info.layers = 1;
    if (vkCreateFramebuffer(device_, &info, nullptr, &framebuffers_[index]) != VK_SUCCESS) { error = "vkCreateFramebuffer failed"; return false; }
  }
  return true;
}

bool FramePresenter::beginFrame(VkCommandBuffer& command, std::uint32_t& imageIndex, std::string& error) {
  if (commands_.empty()) {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex = graphicsFamily_;
    pools_.resize(images_.size()); commands_.resize(images_.size()); fences_.resize(images_.size());
    for (std::size_t index = 0; index < images_.size(); ++index) {
      if (vkCreateCommandPool(device_, &pool, nullptr, &pools_[index]) != VK_SUCCESS) { error = "vkCreateCommandPool failed"; return false; }
      VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; allocation.commandPool = pools_[index]; allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount = 1;
      if (vkAllocateCommandBuffers(device_, &allocation, &commands_[index]) != VK_SUCCESS) { error = "vkAllocateCommandBuffers failed"; return false; }
      VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
      if (vkCreateFence(device_, &fence, nullptr, &fences_[index]) != VK_SUCCESS) { error = "vkCreateFence failed"; return false; }
    }
  }
  if (vkAcquireNextImageKHR(device_, swapchain_, std::numeric_limits<std::uint64_t>::max(), imageAvailable_, VK_NULL_HANDLE, &currentImage_) != VK_SUCCESS) { error = "vkAcquireNextImageKHR failed"; return false; }
  command = commands_[currentImage_]; imageIndex = currentImage_; vkResetCommandBuffer(command, 0);
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS) { error = "vkBeginCommandBuffer failed"; return false; }
  error.clear(); return true;
}

bool FramePresenter::present(const VkCommandBuffer command, const VkSemaphore renderDone, std::string& error) {
  if (vkEndCommandBuffer(command) != VK_SUCCESS) { error = "vkEndCommandBuffer failed"; return false; }
  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &imageAvailable_; submit.pWaitDstStageMask = &waitStage; submit.commandBufferCount = 1; submit.pCommandBuffers = &command; submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = renderDone == VK_NULL_HANDLE ? &renderFinished_ : &renderDone;
  if (vkQueueSubmit(graphicsQueue_, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) { error = "vkQueueSubmit failed"; return false; }
  const VkSemaphore presentSemaphore = renderDone == VK_NULL_HANDLE ? renderFinished_ : renderDone;
  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &presentSemaphore;
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain_;
  present.pImageIndices = &currentImage_;
  const auto presentResult = vkQueuePresentKHR(presentQueue_, &present);
  if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) { error = "vkQueuePresentKHR failed"; return false; }
  error.clear(); return true;
}

VkFramebuffer FramePresenter::framebuffer(const std::uint32_t index) const noexcept { return index < framebuffers_.size() ? framebuffers_[index] : VK_NULL_HANDLE; }

void FramePresenter::destroy() noexcept {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
    for (auto fence : fences_) vkDestroyFence(device_, fence, nullptr);
    for (auto pool : pools_) vkDestroyCommandPool(device_, pool, nullptr);
    if (renderFinished_ != VK_NULL_HANDLE) vkDestroySemaphore(device_, renderFinished_, nullptr);
    if (imageAvailable_ != VK_NULL_HANDLE) vkDestroySemaphore(device_, imageAvailable_, nullptr);
    for (auto framebuffer : framebuffers_) vkDestroyFramebuffer(device_, framebuffer, nullptr);
    if (renderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, renderPass_, nullptr);
    for (auto view : views_) vkDestroyImageView(device_, view, nullptr);
    if (swapchain_ != VK_NULL_HANDLE) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  }
  instance_ = VK_NULL_HANDLE;
  physicalDevice_ = VK_NULL_HANDLE;
  device_ = VK_NULL_HANDLE;
  graphicsQueue_ = VK_NULL_HANDLE;
  presentQueue_ = VK_NULL_HANDLE;
  surface_ = VK_NULL_HANDLE;
  swapchain_ = VK_NULL_HANDLE;
  renderPass_ = VK_NULL_HANDLE;
  images_.clear();
  views_.clear();
  framebuffers_.clear();
  pools_.clear();
  commands_.clear();
  fences_.clear();
  imageAvailable_ = VK_NULL_HANDLE;
  renderFinished_ = VK_NULL_HANDLE;
}

} // namespace aceps::gpu

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
