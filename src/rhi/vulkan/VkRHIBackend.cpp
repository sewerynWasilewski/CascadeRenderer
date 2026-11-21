#include "VkRHIBackend.h"
#include <cstdint>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>

namespace {

VkFormat toVkFormat(RHITextureFormat format) {
  switch (format) {
    case RHI_FORMAT_RGBA8_UNORM:   return VK_FORMAT_R8G8B8A8_UNORM;
    case RHI_FORMAT_BGRA8_SRGB:    return VK_FORMAT_B8G8R8A8_SRGB;
    case RHI_FORMAT_RGBA16_FLOAT:  return VK_FORMAT_R16G16B16A16_SFLOAT;
    case RHI_FORMAT_D32_FLOAT:     return VK_FORMAT_D32_SFLOAT;
    default:                       return VK_FORMAT_UNDEFINED;
  }
}

VkFormat toVkFormat(RHIVertexFormat format) {
  switch (format) {
    case RHI_VERTEX_FLOAT2: return VK_FORMAT_R32G32_SFLOAT;
    case RHI_VERTEX_FLOAT3: return VK_FORMAT_R32G32B32_SFLOAT;
    case RHI_VERTEX_FLOAT4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    default:                return VK_FORMAT_UNDEFINED;
  }
}

std::vector<char> readSpirvFile(const char* path) {
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error(std::string("Failed to open SPIR-V file: ") + path);
  }
  const size_t size = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(size);
  file.seekg(0);
  file.read(buffer.data(), static_cast<std::streamsize>(size));
  return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

  VkShaderModule module = VK_NULL_HANDLE;
  
  if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create shader module.");
  }
  return module;
}

// Owns a VkPipeline + its VkPipelineLayout as one opaque handle, so callers never need
// to track the layout separately just to call pushConstants().
struct VkRHIPipeline {
  VkPipeline       pipeline = VK_NULL_HANDLE;
  VkPipelineLayout layout   = VK_NULL_HANDLE;
};

} // namespace

const std::vector<const char*> validationLayers = {
  "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  }
  return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
  VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageType,
  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
  void* pUserData) {
  std::cerr << "[Vulkan Validation]: " << pCallbackData->pMessage << std::endl;
  return VK_FALSE;
}

static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
  createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
}

VkRHIBackend::VkRHIBackend() {
  if (volkInitialize() != VK_SUCCESS) {
    throw std::runtime_error("Failed to find the Vulkan loader on this system.");
  }

  createInstance();
  setupDebugMessenger();

  pickPhysicalDevice();
  createLogicalDevice();

  mNativeHandles = { mInstance, mPhysicalDevice, mDevice, mGraphicsQueue, mGraphicsQueueFamily };

  mAllocator.init(mInstance, mPhysicalDevice, mDevice);
  std::cout << "[VkRHIBackend] Vulkan initialized successfully.\n";

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = mGraphicsQueueFamily;
  if (vkCreateCommandPool(mDevice, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create command pool.");
  }

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_CommandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(mDevice, &allocInfo, &m_CommandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate command buffers.");
  }
}

VkRHIBackend::~VkRHIBackend() {
  mAllocator.shutdown();
  
  if (mDevice != VK_NULL_HANDLE) {
    if (m_ImageAvailableSemaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(mDevice, m_ImageAvailableSemaphore, nullptr);
      m_ImageAvailableSemaphore = VK_NULL_HANDLE;
    }
    if (m_RenderFinishedSemaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(mDevice, m_RenderFinishedSemaphore, nullptr);
      m_RenderFinishedSemaphore = VK_NULL_HANDLE;
    }

    for (auto imageView : m_SwapchainImageViews) {
      vkDestroyImageView(mDevice, imageView, nullptr);
    }
    m_SwapchainImageViews.clear();

    if (m_CommandPool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(mDevice, m_CommandPool, nullptr);
      m_CommandPool = VK_NULL_HANDLE;
    }

    if (m_Swapchain != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(mDevice, m_Swapchain, nullptr);
      m_Swapchain = VK_NULL_HANDLE;
    }

    vkDestroyDevice(mDevice, nullptr);
    mDevice = VK_NULL_HANDLE;
  }

  if (m_Surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(mInstance, m_Surface, nullptr);
    m_Surface = VK_NULL_HANDLE;
  }

  if (enableValidationLayers && mDebugMessenger != VK_NULL_HANDLE) {
    DestroyDebugUtilsMessengerEXT(mInstance, mDebugMessenger, nullptr);
    mDebugMessenger = VK_NULL_HANDLE;
  }

  if (mInstance != VK_NULL_HANDLE) {
    vkDestroyInstance(mInstance, nullptr);
    mInstance = VK_NULL_HANDLE;
  }

  std::cout << "[VkRHIBackend] Vulkan destroyed.\n";
}

void VkRHIBackend::createInstance() {
  if (enableValidationLayers && !checkValidationLayerSupport()) {
    throw std::runtime_error("Validation layers requested, but not available.");
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Cascade Renderer";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "Cascade";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  // WSI surface extensions are a compile-time choice on Windows/macOS. Linux is the exception
  // (X11 vs Wayland is a runtime choice), so this list would need runtime detection there.
  std::vector<const char*> extensions = { VK_KHR_SURFACE_EXTENSION_NAME };

  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

#if defined(_WIN32)
  extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__APPLE__)
  extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
  extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif

#ifdef __APPLE__
  extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
  createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  if (enableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
    populateDebugMessengerCreateInfo(debugCreateInfo);
    createInfo.pNext = &debugCreateInfo;
  } else {
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = nullptr;
  }

  if (vkCreateInstance(&createInfo, nullptr, &mInstance) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan instance.");
  }

  volkLoadInstance(mInstance);
}
void VkRHIBackend::setupDebugMessenger() {
  if (!enableValidationLayers) return;

  VkDebugUtilsMessengerCreateInfoEXT createInfo{};
  populateDebugMessengerCreateInfo(createInfo);

  if (CreateDebugUtilsMessengerEXT(mInstance, &createInfo, nullptr, &mDebugMessenger) != VK_SUCCESS) {
    throw std::runtime_error("Failed to set up debug messenger.");
  }
}

bool VkRHIBackend::checkValidationLayerSupport() {
  uint32_t layerCount;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

  for (const char* layerName : validationLayers) {
    bool layerFound = false;
    for (const auto& layerProperties : availableLayers) {
      if (strcmp(layerName, layerProperties.layerName) == 0) {
        layerFound = true;
        break;
      }
    }
    if (!layerFound) return false;
  }
  return true;
}

static u32 findGraphicsQueueFamily(VkPhysicalDevice device) {
  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

  for (uint32_t i = 0; i < queueFamilyCount; i++) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      return i;
    }
  }
  return RG_INVALID_ID;
}

void VkRHIBackend::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(mInstance, &deviceCount, nullptr);
  if (deviceCount == 0) {
    throw std::runtime_error("Failed to find GPUs with Vulkan support.");
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(mInstance, &deviceCount, devices.data());

  for (const auto& device : devices) {
    u32 queueFamily = findGraphicsQueueFamily(device);
    if (queueFamily == RG_INVALID_ID) continue;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      mPhysicalDevice = device;
      mGraphicsQueueFamily = queueFamily;
      std::cout << "[VkRHIBackend] Selected GPU (Discrete): " << props.deviceName << "\n";
      return;
    }
    if (mPhysicalDevice == VK_NULL_HANDLE) {
      mPhysicalDevice = device;
      mGraphicsQueueFamily = queueFamily;
    }
  }

  if (mPhysicalDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("Failed to find a suitable GPU.");
  }

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(mPhysicalDevice, &props);
  std::cout << "[VkRHIBackend] Selected GPU: " << props.deviceName << "\n";
}


void VkRHIBackend::createLogicalDevice() {
  VkDeviceQueueCreateInfo queueCreateInfo{};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = mGraphicsQueueFamily;
  queueCreateInfo.queueCount = 1;
  float queuePriority = 1.0f;
  queueCreateInfo.pQueuePriorities = &queuePriority;

  VkPhysicalDeviceVulkan13Features features13{};
  features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features13.synchronization2 = VK_TRUE;
  features13.dynamicRendering = VK_TRUE;

  std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
  };

#ifdef __APPLE__
  // MoltenVK always reports VK_KHR_portability_subset; the spec requires enabling it
  // whenever a physical device exposes it. Named as a raw string to avoid pulling in
  // vulkan_beta.h / VK_ENABLE_BETA_EXTENSIONS for this one constant.
  deviceExtensions.push_back("VK_KHR_portability_subset");
#endif

  VkPhysicalDeviceFeatures2 deviceFeatures2{};
  deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  deviceFeatures2.pNext = &features13;

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.pQueueCreateInfos = &queueCreateInfo;
  createInfo.queueCreateInfoCount = 1;
  createInfo.pNext = &deviceFeatures2;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();

  if (vkCreateDevice(mPhysicalDevice, &createInfo, nullptr, &mDevice) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create logical device.");
  }

  volkLoadDevice(mDevice);

  vkGetDeviceQueue(mDevice, mGraphicsQueueFamily, 0, &mGraphicsQueue);
}



struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};

SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
  SwapChainSupportDetails details;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

  uint32_t formatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
  if (formatCount != 0) {
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
  }

  uint32_t presentModeCount;
  vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
  if (presentModeCount != 0) {
    details.presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
  }

  return details;
}

VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
  for (const auto& availableFormat : availableFormats) {
    if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return availableFormat;
    }
  }
  return availableFormats[0];
}

VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
  for (const auto& availablePresentMode : availablePresentModes) {
    if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return availablePresentMode;
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, u32 framebufferWidth, u32 framebufferHeight) {
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  } else {
    VkExtent2D actualExtent = { framebufferWidth, framebufferHeight };

    actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

    return actualExtent;
  }
}

void VkRHIBackend::createSwapChain(void* nativeSurface, u32 framebufferWidth, u32 framebufferHeight) {
  m_Surface = static_cast<VkSurfaceKHR>(nativeSurface);

  SwapChainSupportDetails swapChainSupport = querySwapChainSupport(mPhysicalDevice, m_Surface);

  VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
  VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
  VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities, framebufferWidth, framebufferHeight);

  uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
  if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
    imageCount = swapChainSupport.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = m_Surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  createInfo.queueFamilyIndexCount = 0;
  createInfo.pQueueFamilyIndices = nullptr;

  createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(mDevice, &createInfo, nullptr, &m_Swapchain) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create swap chain.");
  }

  vkGetSwapchainImagesKHR(mDevice, m_Swapchain, &imageCount, nullptr);
  m_SwapchainImages.resize(imageCount);
  vkGetSwapchainImagesKHR(mDevice, m_Swapchain, &imageCount, m_SwapchainImages.data());

  m_SwapchainImageViews.resize(imageCount);
  for (size_t i = 0; i < imageCount; i++) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_SwapchainImages[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = surfaceFormat.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(mDevice, &viewInfo, nullptr, &m_SwapchainImageViews[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create texture image view.");
    }
  }

  // TODO: map surfaceFormat.format back to RHITextureFormat instead of assuming it
  m_SwapchainRHIImages.resize(imageCount);
  for (size_t i = 0; i < imageCount; i++) {
    m_SwapchainRHIImages[i] = VkRHIImage{
      m_SwapchainImages[i], m_SwapchainImageViews[i], RHI_FORMAT_BGRA8_SRGB
    };
  }

  m_SwapchainImageFormat = surfaceFormat.format;
  m_SwapchainExtent = extent;
  mNativeHandles.swapchainColorFormat = surfaceFormat.format;
}

void VkRHIBackend::beginFrame() {
  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  
  if (m_ImageAvailableSemaphore == VK_NULL_HANDLE) {
    if (vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphore) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create image available semaphore.");
    }
  }

  VkResult result = vkAcquireNextImageKHR(
    mDevice, 
    m_Swapchain, 
    UINT64_MAX, 
    m_ImageAvailableSemaphore,
    VK_NULL_HANDLE, 
    &m_CurrentImageIndex
  );

  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("Failed to acquire swap chain image.");
  }

  vkResetCommandBuffer(m_CommandBuffer, 0);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (vkBeginCommandBuffer(m_CommandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("Failed to begin recording command buffer.");
  }

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = m_SwapchainImages[m_CurrentImageIndex];
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  vkCmdPipelineBarrier(
    m_CommandBuffer,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,
    0, nullptr,
    0, nullptr,
    1, &barrier
  );
}

void VkRHIBackend::endFrame() {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = m_SwapchainImages[m_CurrentImageIndex];
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.dstAccessMask = 0;

  vkCmdPipelineBarrier(
    m_CommandBuffer,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    0,
    0, nullptr,
    0, nullptr,
    1, &barrier
  );

  if (vkEndCommandBuffer(m_CommandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to record command buffer.");
  }

  if (m_RenderFinishedSemaphore == VK_NULL_HANDLE) {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphore) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create render finished semaphore.");
    }
  }

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphore };
  VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;

  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &m_CommandBuffer;

  VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphore };
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  if (vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
    throw std::runtime_error("Failed to submit draw command buffer.");
  }

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;

  VkSwapchainKHR swapChains[] = { m_Swapchain };
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = &m_CurrentImageIndex;

  VkResult result = vkQueuePresentKHR(mGraphicsQueue, &presentInfo);
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("Failed to present swap chain image.");
  }

  vkQueueWaitIdle(mGraphicsQueue);
}

void* VkRHIBackend::getCurrentSwapchainTarget() const {
  if (m_SwapchainImageViews.empty()) return nullptr;
  return m_SwapchainImageViews[m_CurrentImageIndex];
}

void* VkRHIBackend::getCurrentSwapchainImage() const {
  if (m_SwapchainRHIImages.empty()) return nullptr;
  return const_cast<VkRHIImage*>(&m_SwapchainRHIImages[m_CurrentImageIndex]); // void* handles aren't const
}

void* VkRHIBackend::createImage(const RHITextureDesc& desc) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = desc.width;
  imageInfo.extent.height = desc.height;
  imageInfo.extent.depth = desc.depth > 0 ? desc.depth : 1;
  imageInfo.mipLevels = desc.mipLevels > 0 ? desc.mipLevels : 1;
  imageInfo.arrayLayers = desc.arrayLayers > 0 ? desc.arrayLayers : 1;
  
  imageInfo.format = toVkFormat(desc.format);
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  
  VkImageUsageFlags usageFlags = 0;
  if ((desc.usage & RHI_USAGE_COLOR_ATTACHMENT) != 0)  usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if ((desc.usage & RHI_USAGE_DEPTH_ATTACHMENT) != 0)  usageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  if ((desc.usage & RHI_USAGE_SAMPLED_TEXTURE) != 0)   usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
  if ((desc.usage & RHI_USAGE_STORAGE_TEXTURE) != 0)   usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;
  if ((desc.usage & RHI_USAGE_TRANSFER_SRC) != 0)      usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if ((desc.usage & RHI_USAGE_TRANSFER_DST) != 0)      usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  if (usageFlags == 0) {
    usageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  imageInfo.usage = usageFlags;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkImage image = VK_NULL_HANDLE;
  if (vkCreateImage(mDevice, &imageInfo, nullptr, &image) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan image.");
  }

  return new VkRHIImage{image, VK_NULL_HANDLE, desc.format}; // view made later by bindMemory()
}

void* VkRHIBackend::createBuffer(const RHIBufferDesc& desc) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = desc.size;

  VkBufferUsageFlags usageFlags = 0;
  if ((desc.usage & RHI_USAGE_VERTEX_BUFFER) != 0)  usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  if ((desc.usage & RHI_USAGE_INDEX_BUFFER) != 0)   usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  if ((desc.usage & RHI_USAGE_UNIFORM_BUFFER) != 0) usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  if ((desc.usage & RHI_USAGE_TRANSFER_SRC) != 0)   usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  if ((desc.usage & RHI_USAGE_TRANSFER_DST) != 0)   usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  if (usageFlags == 0) {
    usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | 
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }

  bufferInfo.usage = usageFlags;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkBuffer buffer = VK_NULL_HANDLE;
  if (vkCreateBuffer(mDevice, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan buffer.");
  }

  return static_cast<void*>(buffer);
}

void  VkRHIBackend::destroyImage(void* handle) {
  if (handle != nullptr) {
    auto* img = static_cast<VkRHIImage*>(handle);
    if (img->imageView != VK_NULL_HANDLE) {
      vkDestroyImageView(mDevice, img->imageView, nullptr);
    }
    vkDestroyImage(mDevice, img->image, nullptr);
    delete img;
  }
}

void  VkRHIBackend::destroyBuffer(void* handle) {
  if (handle != nullptr) {
    VkBuffer buffer = static_cast<VkBuffer>(handle);
    vkDestroyBuffer(mDevice, buffer, nullptr);
  }
}

RHIMemoryRequirements VkRHIBackend::getMemoryRequirements(void* gpuHandle, RHIResourceKind kind) {
  VkMemoryRequirements memRequirements{};  

  if (kind == RHI_RESOURCE_BUFFER) {
    VkBuffer buffer = static_cast<VkBuffer>(gpuHandle);
    vkGetBufferMemoryRequirements(mDevice, buffer, &memRequirements);
  } else if (kind == RHI_RESOURCE_TEXTURE) {
    auto* img = static_cast<VkRHIImage*>(gpuHandle);
    vkGetImageMemoryRequirements(mDevice, img->image, &memRequirements);
  }

  return RHIMemoryRequirements{ memRequirements.size, memRequirements.alignment };
}

RHIMemoryBlock VkRHIBackend::allocatePool(u64 size, RHIMemoryType memoryType) {
  return mAllocator.allocate(size, memoryType);
}

void VkRHIBackend::freePool(RHIMemoryBlock block) {
  mAllocator.free(block);
}

void* VkRHIBackend::map(RHIMemoryBlock block) {
  return mAllocator.map(block);
}

void VkRHIBackend::unmap(RHIMemoryBlock block) {
  mAllocator.unmap(block);
}

void VkRHIBackend::bindMemory(void* gpuHandle, RHIResourceKind kind, RHIMemoryBlock block, u64 offset) {
  // block.handle is a VmaAllocation*, not a VkDeviceMemory - look up the real backing
  // memory (and VMA's own base offset into it) via the allocator.
  const VkRHIAllocator::AllocationDetails* details = mAllocator.getAllocationDetails(block.id);
  if (!details) {
    throw std::runtime_error("bindMemory: unknown memory block id.");
  }
  const VkDeviceSize absoluteOffset = details->baseOffset + offset;

  if (kind == RHI_RESOURCE_TEXTURE) {
    auto* img = static_cast<VkRHIImage*>(gpuHandle);
    if (vkBindImageMemory(mDevice, img->image, details->deviceMemory, absoluteOffset) != VK_SUCCESS) {
      throw std::runtime_error("Failed to bind image memory.");
    }

    // Only now that memory is bound can a view be created on top of it.
    if (img->imageView == VK_NULL_HANDLE) {
      const bool isDepth = img->format == RHI_FORMAT_D32_FLOAT;

      VkImageViewCreateInfo viewInfo{};
      viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewInfo.image = img->image;
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = toVkFormat(img->format);
      viewInfo.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.subresourceRange.baseMipLevel = 0;
      viewInfo.subresourceRange.levelCount = 1;
      viewInfo.subresourceRange.baseArrayLayer = 0;
      viewInfo.subresourceRange.layerCount = 1;

      if (vkCreateImageView(mDevice, &viewInfo, nullptr, &img->imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view.");
      }
    }
  } else {
    VkBuffer buffer = static_cast<VkBuffer>(gpuHandle);
    if (vkBindBufferMemory(mDevice, buffer, details->deviceMemory, absoluteOffset) != VK_SUCCESS) {
      throw std::runtime_error("Failed to bind buffer memory.");
    }
  }
}

void VkRHIBackend::emitBarrier(const RHIBarrierInfo& info, void* cmdBuf) {
  VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(cmdBuf);
  if (commandBuffer == VK_NULL_HANDLE) {
    commandBuffer = m_CommandBuffer;
  }

  auto getStageFlags = [](RHIUsage usage) -> VkPipelineStageFlags2 {
    VkPipelineStageFlags2 flags = 0;
    if ((usage & (RHI_USAGE_VERTEX_BUFFER | RHI_USAGE_INDEX_BUFFER)) != 0) flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    if ((usage & RHI_USAGE_UNIFORM_BUFFER) != 0) flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    if ((usage & RHI_USAGE_COLOR_ATTACHMENT) != 0) flags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    if ((usage & RHI_USAGE_DEPTH_ATTACHMENT) != 0) flags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    if ((usage & (RHI_USAGE_SAMPLED_TEXTURE | RHI_USAGE_STORAGE_TEXTURE)) != 0) flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    if ((usage & (RHI_USAGE_TRANSFER_SRC | RHI_USAGE_TRANSFER_DST)) != 0) flags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    return flags != 0 ? flags : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  };

  auto getAccessFlags = [](RHIUsage usage) -> VkAccessFlags2 {
    VkAccessFlags2 flags = 0;
    if ((usage & RHI_USAGE_VERTEX_BUFFER) != 0) flags |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    if ((usage & RHI_USAGE_INDEX_BUFFER) != 0) flags |= VK_ACCESS_2_INDEX_READ_BIT;
    if ((usage & RHI_USAGE_UNIFORM_BUFFER) != 0) flags |= VK_ACCESS_2_UNIFORM_READ_BIT;
    if ((usage & RHI_USAGE_COLOR_ATTACHMENT) != 0) flags |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
    if ((usage & RHI_USAGE_DEPTH_ATTACHMENT) != 0) flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    if ((usage & RHI_USAGE_SAMPLED_TEXTURE) != 0) flags |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    if ((usage & RHI_USAGE_STORAGE_TEXTURE) != 0) flags |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    if ((usage & RHI_USAGE_TRANSFER_SRC) != 0) flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
    if ((usage & RHI_USAGE_TRANSFER_DST) != 0) flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    return flags;
  };

  if (info.resource_kind == RHI_RESOURCE_TEXTURE && info.handle != nullptr) {
    VkImageMemoryBarrier2 imageBarrier{};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    
    imageBarrier.srcStageMask = getStageFlags(info.before_usage);
    imageBarrier.srcAccessMask = getAccessFlags(info.before_usage);
    imageBarrier.dstStageMask = getStageFlags(info.after_usage);
    imageBarrier.dstAccessMask = getAccessFlags(info.after_usage);

    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;

    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    imageBarrier.image = static_cast<VkRHIImage*>(info.handle)->image;

    imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.baseMipLevel = 0;
    imageBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    imageBarrier.subresourceRange.baseArrayLayer = 0;
    imageBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
  }
  else if (info.resource_kind == RHI_RESOURCE_BUFFER && info.handle != nullptr) {
    VkBufferMemoryBarrier2 bufferBarrier{};
    bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    bufferBarrier.srcStageMask = getStageFlags(info.before_usage);
    bufferBarrier.srcAccessMask = getAccessFlags(info.before_usage);
    bufferBarrier.dstStageMask = getStageFlags(info.after_usage);
    bufferBarrier.dstAccessMask = getAccessFlags(info.after_usage);
    bufferBarrier.buffer = static_cast<VkBuffer>(info.handle);
    bufferBarrier.offset = 0;
    bufferBarrier.size = VK_WHOLE_SIZE;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.bufferMemoryBarrierCount = 1;
    dependencyInfo.pBufferMemoryBarriers = &bufferBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
  }
}

void VkRHIBackend::beginPass(void* cmdBuf, RHIPassType type, void* colorAttachmentHandle) {
  VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(cmdBuf);
  if (commandBuffer == VK_NULL_HANDLE) {
    commandBuffer = m_CommandBuffer;
  }

  if (type == RHI_PASS_RASTER) {
    auto* img = static_cast<VkRHIImage*>(colorAttachmentHandle);
    if (img == nullptr) return;

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = img->imageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue = { {0.05f, 0.05f, 0.08f, 1.0f} };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = m_SwapchainExtent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_SwapchainExtent.width);
    viewport.height = static_cast<float>(m_SwapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = m_SwapchainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
  }
}
void VkRHIBackend::endPass(void* cmdBuf) {
  VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(cmdBuf);
  if (commandBuffer == VK_NULL_HANDLE) {
    commandBuffer = m_CommandBuffer;
  }

  vkCmdEndRendering(commandBuffer);
}

void* VkRHIBackend::createPipeline(const RHIPipelineDesc& desc) {
  const std::vector<char> vertCode = readSpirvFile(desc.vertexShaderPath);
  const std::vector<char> fragCode = readSpirvFile(desc.fragmentShaderPath);
  VkShaderModule vertModule = createShaderModule(mDevice, vertCode);
  VkShaderModule fragModule = createShaderModule(mDevice, fragCode);

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertModule;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragModule;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = desc.vertexStride;
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::vector<VkVertexInputAttributeDescription> attributes(desc.vertexAttributeCount);
  for (u32 i = 0; i < desc.vertexAttributeCount; i++) {
    const RHIVertexAttribute& a = desc.vertexAttributes[i];
    attributes[i] = { a.location, 0, toVkFormat(a.format), a.offset };
  }

  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount = desc.vertexAttributeCount > 0 ? 1 : 0;
  vertexInput.pVertexBindingDescriptions = &binding;
  vertexInput.vertexAttributeDescriptionCount = desc.vertexAttributeCount;
  vertexInput.pVertexAttributeDescriptions = attributes.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  const bool hasDepth = desc.depthFormat != RHI_FORMAT_UNDEFINED;
  depthStencil.depthTestEnable = hasDepth ? VK_TRUE : VK_FALSE;
  depthStencil.depthWriteEnable = hasDepth ? VK_TRUE : VK_FALSE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlend{};
  colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlend.attachmentCount = 1;
  colorBlend.pAttachments = &colorBlendAttachment;

  VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = desc.pushConstantSize;

  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  if (desc.pushConstantSize > 0) {
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
  }

  VkPipelineLayout layout = VK_NULL_HANDLE;
  if (vkCreatePipelineLayout(mDevice, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create pipeline layout.");
  }

  const VkFormat colorFormat = toVkFormat(desc.colorFormat);
  const VkFormat depthFormatVk = hasDepth ? toVkFormat(desc.depthFormat) : VK_FORMAT_UNDEFINED;

  VkPipelineRenderingCreateInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachmentFormats = &colorFormat;
  renderingInfo.depthAttachmentFormat = depthFormatVk;

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.pNext = &renderingInfo;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = stages;
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisample;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlend;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = layout;

  VkPipeline pipeline = VK_NULL_HANDLE;
  const VkResult result = vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

  vkDestroyShaderModule(mDevice, vertModule, nullptr);
  vkDestroyShaderModule(mDevice, fragModule, nullptr);

  if (result != VK_SUCCESS) {
    throw std::runtime_error("Failed to create graphics pipeline.");
  }

  return new VkRHIPipeline{ pipeline, layout };
}

void VkRHIBackend::destroyPipeline(void* handle) {
  if (handle == nullptr) return;
  auto* p = static_cast<VkRHIPipeline*>(handle);
  vkDestroyPipeline(mDevice, p->pipeline, nullptr);
  vkDestroyPipelineLayout(mDevice, p->layout, nullptr);
  delete p;
}

void VkRHIBackend::bindPipeline(void* cmdBuf, void* pipelineHandle) {
  VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(cmdBuf);
  auto* p = static_cast<VkRHIPipeline*>(pipelineHandle);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
}

void VkRHIBackend::bindVertexBuffer(void* cmdBuf, void* bufferHandle, u64 offset) {
  VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(cmdBuf);
  VkBuffer buffer = static_cast<VkBuffer>(bufferHandle);
  VkDeviceSize vkOffset = offset;
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &buffer, &vkOffset);
}

void VkRHIBackend::bindIndexBuffer(void* cmdBuf, void* bufferHandle, u64 offset) {
  VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(cmdBuf);
  VkBuffer buffer = static_cast<VkBuffer>(bufferHandle);
  vkCmdBindIndexBuffer(commandBuffer, buffer, offset, VK_INDEX_TYPE_UINT32);
}

void VkRHIBackend::pushConstants(void* cmdBuf, void* pipelineHandle, u32 stageFlags,
                                  u32 offset, u32 size, const void* data) {
  VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(cmdBuf);
  auto* p = static_cast<VkRHIPipeline*>(pipelineHandle);

  VkShaderStageFlags vkStages = 0;
  if ((stageFlags & RHI_SHADER_STAGE_VERTEX) != 0)   vkStages |= VK_SHADER_STAGE_VERTEX_BIT;
  if ((stageFlags & RHI_SHADER_STAGE_FRAGMENT) != 0) vkStages |= VK_SHADER_STAGE_FRAGMENT_BIT;

  vkCmdPushConstants(commandBuffer, p->layout, vkStages, offset, size, data);
}

void VkRHIBackend::drawIndexed(void* cmdBuf, u32 indexCount, u32 instanceCount,
                                u32 firstIndex, i32 vertexOffset, u32 firstInstance) {
  VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(cmdBuf);
  vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}