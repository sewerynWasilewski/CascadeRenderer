#include "VkRHIBackend.h"
#include <cstdint>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <algorithm>
#include <string>

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

// UNDEFINED discards prior contents - correct for the first transition of a resource, and for
// any usage whose exact prior layout we don't model (prefer a fresh transition over guessing wrong).
VkImageLayout toVkImageLayout(RHIUsage usage) {
  if (usage == RHI_USAGE_NONE) return VK_IMAGE_LAYOUT_UNDEFINED;
  if ((usage & RHI_USAGE_COLOR_ATTACHMENT) != 0) return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  if ((usage & RHI_USAGE_DEPTH_ATTACHMENT) != 0) return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  if ((usage & RHI_USAGE_STORAGE_TEXTURE) != 0) return VK_IMAGE_LAYOUT_GENERAL;
  if ((usage & RHI_USAGE_SAMPLED_TEXTURE) != 0) return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  if ((usage & RHI_USAGE_TRANSFER_SRC) != 0) return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  if ((usage & RHI_USAGE_TRANSFER_DST) != 0) return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  if ((usage & RHI_USAGE_PRESENT_SRC) != 0) return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  return VK_IMAGE_LAYOUT_GENERAL;
}

// Owns a VkPipeline + its VkPipelineLayout as one opaque handle, so callers never need
// to track the layout separately just to call pushConstants().
struct VkRHIPipeline {
  VkPipeline       pipeline = VK_NULL_HANDLE;
  VkPipelineLayout layout   = VK_NULL_HANDLE;
};

VkFilter toVkFilter(RHIFilterMode mode) {
  switch (mode) {
    case RHI_FILTER_NEAREST: return VK_FILTER_NEAREST;
    case RHI_FILTER_LINEAR:
    default:                 return VK_FILTER_LINEAR;
  }
}

VkSamplerMipmapMode toVkMipmapMode(RHIFilterMode mode) {
  switch (mode) {
    case RHI_FILTER_NEAREST: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case RHI_FILTER_LINEAR:
    default:                 return VK_SAMPLER_MIPMAP_MODE_LINEAR;
  }
}

VkSamplerAddressMode toVkAddressMode(RHIAddressMode mode) {
  switch (mode) {
    case RHI_ADDRESS_REPEAT:          return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case RHI_ADDRESS_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case RHI_ADDRESS_CLAMP_TO_EDGE:
    default:                          return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
}

VkDescriptorType toVkDescriptorType(RHIDescriptorType type) {
  switch (type) {
    case RHI_DESCRIPTOR_UNIFORM_BUFFER:
      return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case RHI_DESCRIPTOR_COMBINED_IMAGE_SAMPLER:
    default:
      return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  }
}

VkShaderStageFlags toVkShaderStages(u32 stages) {
  VkShaderStageFlags flags = 0;
  if ((stages & RHI_SHADER_STAGE_VERTEX) != 0) flags |= VK_SHADER_STAGE_VERTEX_BIT;
  if ((stages & RHI_SHADER_STAGE_FRAGMENT) != 0) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;

  if (flags == 0) throw std::runtime_error("Error parsing RHIShaderStage to VkShaderStages");

  return flags;
}

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
  if (mDevice != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(mDevice);
  }
  mAllocator.shutdown();

  if (mDevice != VK_NULL_HANDLE) {

    for (VkDescriptorSetLayout layout : m_DescriptorSetLayouts) {
      if (layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(mDevice, layout, nullptr);
      }
    }
    m_DescriptorSetLayouts.clear();

    if (m_DescriptorPool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(mDevice, m_DescriptorPool, nullptr);
      m_DescriptorPool = VK_NULL_HANDLE;
    }
    
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

    for (VkSampler sampler : m_Samplers) {
      if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(mDevice, sampler, nullptr);
      }
    }
    m_Samplers.clear();

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
      m_SwapchainImages[i], m_SwapchainImageViews[i], RHI_FORMAT_BGRA8_SRGB, extent
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

  // Entry transition for this frame's swapchain image - import it with initialUsage matching
  // RHI_USAGE_COLOR_ATTACHMENT so RenderGraph's own compile()-time barrier doesn't duplicate this.
  RHIBarrierInfo info{
    &m_SwapchainRHIImages[m_CurrentImageIndex],
    0,
    RHI_RESOURCE_TEXTURE,
    RHI_USAGE_NONE,
    RHI_USAGE_COLOR_ATTACHMENT,
    RHI_BARRIER_TRANSITION,
    RHI_QUEUE_IGNORED,
    RHI_QUEUE_IGNORED,
  };
  emitBarrier(info, m_CommandBuffer);
}

void VkRHIBackend::endFrame() {
  // Overlay (ImGui) draws directly on the swapchain view after this point, outside RenderGraph -
  // see ideas.md #11. This is the one present-layout transition RenderGraph can't own yet.
  RHIBarrierInfo info{
    &m_SwapchainRHIImages[m_CurrentImageIndex],
    0,
    RHI_RESOURCE_TEXTURE,
    RHI_USAGE_COLOR_ATTACHMENT,
    RHI_USAGE_PRESENT_SRC,
    RHI_BARRIER_TRANSITION,
    RHI_QUEUE_IGNORED,
    RHI_QUEUE_IGNORED,
  };
  emitBarrier(info, m_CommandBuffer);

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

  return new VkRHIImage{image, VK_NULL_HANDLE, desc.format, { desc.width, desc.height }};
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
    if ((usage & RHI_USAGE_PRESENT_SRC) != 0) flags |= VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
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
    auto* img = static_cast<VkRHIImage*>(info.handle);

    VkImageMemoryBarrier2 imageBarrier{};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

    imageBarrier.srcStageMask = getStageFlags(info.before_usage);
    imageBarrier.srcAccessMask = getAccessFlags(info.before_usage);
    imageBarrier.dstStageMask = getStageFlags(info.after_usage);
    imageBarrier.dstAccessMask = getAccessFlags(info.after_usage);

    imageBarrier.oldLayout = (info.kind == RHI_BARRIER_ALIASING) ? VK_IMAGE_LAYOUT_UNDEFINED : toVkImageLayout(info.before_usage);
    imageBarrier.newLayout = toVkImageLayout(info.after_usage);

    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    imageBarrier.image = img->image;

    imageBarrier.subresourceRange.aspectMask =
      img->format == RHI_FORMAT_D32_FLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
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

void VkRHIBackend::beginPass(void* cmdBuf, RHIPassType type, void* const* colorAttachments, u32 colorAttachmentCount, void* depthAttachment) {
  VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(cmdBuf);
  if (commandBuffer == VK_NULL_HANDLE) {
    commandBuffer = m_CommandBuffer;
  }

  if (type != RHI_PASS_RASTER) return;

  auto adoptExtent = [](VkExtent2D& extent, bool& haveExtent, VkExtent2D candidate) {
    if (!haveExtent && candidate.width > 0 && candidate.height > 0) {
      extent = candidate;
      haveExtent = true;
    }
  };

  if (colorAttachmentCount > RHI_MAX_COLOR_ATTACHMENTS) {
    throw std::runtime_error("beginPass: colorAttachmentCount exceeds RHI_MAX_COLOR_ATTACHMENTS.");
  }

  VkRenderingAttachmentInfo vkAttachments[RHI_MAX_COLOR_ATTACHMENTS]{};
  VkExtent2D extent = m_SwapchainExtent;
  bool haveExtent = false;

  for (u32 i = 0; i < colorAttachmentCount; ++i) {
    auto* img = static_cast<VkRHIImage*>(colorAttachments != nullptr ? colorAttachments[i] : nullptr);
    if (img == nullptr || img->imageView == VK_NULL_HANDLE)
    {throw std::runtime_error("beginPass: color attachment " + std::to_string(i) + " is null or has no image view (was bindMemory() called).");}

    adoptExtent(extent, haveExtent, img->extent); 

    VkRenderingAttachmentInfo& att = vkAttachments[i];
    att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    att.imageView = img->imageView;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.clearValue.color = (i == 0) ? VkClearColorValue{ {0.05f, 0.05f, 0.08f, 1.0f} } : VkClearColorValue{ {0.0f, 0.0f, 0.0f, 0.0f} };
  }

  auto* depthImg = static_cast<VkRHIImage*>(depthAttachment);
  if (depthImg != nullptr && depthImg->imageView == VK_NULL_HANDLE)
  {throw std::runtime_error("beginPass: depth attachment has no image view (was bindMemory() called).");}
  
  const bool hasDepthAttachment = (depthImg != nullptr);

  VkRenderingAttachmentInfo depthInfo{};
  if (hasDepthAttachment) {
    depthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthInfo.imageView = depthImg->imageView;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthInfo.clearValue.depthStencil = { 1.0f, 0 };

    adoptExtent(extent, haveExtent, depthImg->extent); 
  }

  if (colorAttachmentCount == 0 && !hasDepthAttachment) return;

  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea.offset = { 0, 0 };
  renderingInfo.renderArea.extent = extent;
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = colorAttachmentCount;
  renderingInfo.pColorAttachments = colorAttachmentCount > 0 ? vkAttachments : nullptr;
  renderingInfo.pDepthAttachment = hasDepthAttachment ? &depthInfo : nullptr;

  vkCmdBeginRendering(commandBuffer, &renderingInfo);
  m_InRendering = true;

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(extent.width);
  viewport.height = static_cast<float>(extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = { 0, 0 };
  scissor.extent = extent;
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void VkRHIBackend::endPass(void* cmdBuf) {
  VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(cmdBuf);
  if (commandBuffer == VK_NULL_HANDLE) {
    commandBuffer = m_CommandBuffer;
  }

  if (m_InRendering) {
    vkCmdEndRendering(commandBuffer);
    m_InRendering = false;
  }
}

void* VkRHIBackend::createPipeline(const RHIPipelineDesc& desc) {
  if (desc.colorAttachmentCount > RHI_MAX_COLOR_ATTACHMENTS) {
    throw std::runtime_error("createPipeline: colorAttachmentCount exceeds RHI_MAX_COLOR_ATTACHMENTS.");
  }

  for (u32 i = 0; i < desc.colorAttachmentCount; ++i) {
    if (desc.colorFormats[i] == RHI_FORMAT_UNDEFINED) {
      throw std::runtime_error("createPipeline: colorFormats[" + std::to_string(i) +
                               "] is UNDEFINED but colorAttachmentCount = " + std::to_string(desc.colorAttachmentCount) + ".");
    }
  }

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

  VkPipelineColorBlendAttachmentState colorBlendAttachments[RHI_MAX_COLOR_ATTACHMENTS]{};
  VkFormat vkColorFormats[RHI_MAX_COLOR_ATTACHMENTS]{};

  for (u32 i = 0; i < desc.colorAttachmentCount; ++i) {
    colorBlendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[i].blendEnable = VK_FALSE;
    vkColorFormats[i] = toVkFormat(desc.colorFormats[i]);
  }

  VkPipelineColorBlendStateCreateInfo colorBlend{};
  colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlend.attachmentCount = desc.colorAttachmentCount;
  colorBlend.pAttachments = desc.colorAttachmentCount > 0 ? colorBlendAttachments : nullptr;

  VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = toVkShaderStages(desc.pushConstantStages);
  pushConstantRange.offset = 0;
  pushConstantRange.size = desc.pushConstantSize;

  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  if (desc.pushConstantSize > 0) {
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
  }
  
  VkDescriptorSetLayout vkSetLayout = static_cast<VkDescriptorSetLayout>(desc.descriptorSetLayout);
  if (vkSetLayout != VK_NULL_HANDLE) {
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts    = &vkSetLayout;
  }

  VkPipelineLayout layout = VK_NULL_HANDLE;
  if (vkCreatePipelineLayout(mDevice, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
    vkDestroyShaderModule(mDevice, vertModule, nullptr);
    vkDestroyShaderModule(mDevice, fragModule, nullptr);
    throw std::runtime_error("Failed to create pipeline layout.");
  }

  const VkFormat depthFormatVk = hasDepth ? toVkFormat(desc.depthFormat) : VK_FORMAT_UNDEFINED;

  VkPipelineRenderingCreateInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  renderingInfo.colorAttachmentCount = desc.colorAttachmentCount;
  renderingInfo.pColorAttachmentFormats = desc.colorAttachmentCount > 0 ? vkColorFormats : nullptr;
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
    vkDestroyPipelineLayout(mDevice, layout, nullptr);
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

  vkCmdPushConstants(commandBuffer, p->layout, toVkShaderStages(stageFlags), offset, size, data);
}

void VkRHIBackend::drawIndexed(void* cmdBuf, u32 indexCount, u32 instanceCount,
                                u32 firstIndex, i32 vertexOffset, u32 firstInstance) {
  VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(cmdBuf);
  vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void* VkRHIBackend::createSampler(const RHISamplerDesc& desc) {
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = toVkFilter(desc.magFilter);
  samplerInfo.minFilter = toVkFilter(desc.minFilter);
  samplerInfo.mipmapMode = toVkMipmapMode(desc.mipmapMode);
  samplerInfo.addressModeU = toVkAddressMode(desc.addressMode);
  samplerInfo.addressModeV = toVkAddressMode(desc.addressMode);
  samplerInfo.addressModeW = toVkAddressMode(desc.addressMode);
  samplerInfo.mipLodBias = 0.0f;

  const bool anisoEnabled = desc.maxAnisotropy > 1.0f;
  samplerInfo.anisotropyEnable = anisoEnabled ? VK_TRUE : VK_FALSE;
  samplerInfo.maxAnisotropy = anisoEnabled ? desc.maxAnisotropy : 1.0f;

  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
  samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;

  VkSampler sampler = VK_NULL_HANDLE;
  if (vkCreateSampler(mDevice, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan sampler.");
  }

  m_Samplers.push_back(sampler);
  return static_cast<void*>(sampler);
}

void VkRHIBackend::destroySampler(void* handle) {
  if (handle == nullptr) return;
  VkSampler sampler = static_cast<VkSampler>(handle);
  auto it = std::find(m_Samplers.begin(), m_Samplers.end(), sampler);
  if (it != m_Samplers.end()) {
    vkDestroySampler(mDevice, *it, nullptr);
    m_Samplers.erase(it);
  }
}

void* VkRHIBackend::createDescriptorSetLayout(const RHIDescriptorBinding* bindings, u32 bindingCount) {
  std::vector<VkDescriptorSetLayoutBinding> vkBindings;
  vkBindings.reserve(bindingCount);

  for (u32 i = 0; i < bindingCount; ++i) {
    VkDescriptorSetLayoutBinding b{};
    b.binding         = bindings[i].binding;
    b.descriptorType  = toVkDescriptorType(bindings[i].type);
    b.descriptorCount = bindings[i].count > 0 ? bindings[i].count : 1;
    b.stageFlags      = toVkShaderStages(bindings[i].stages);
    vkBindings.push_back(b);
  }

  VkDescriptorSetLayoutCreateInfo info{};
  info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.bindingCount = static_cast<u32>(vkBindings.size());
  info.pBindings    = vkBindings.empty() ? nullptr : vkBindings.data();

  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  if (vkCreateDescriptorSetLayout(mDevice, &info, nullptr, &layout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create descriptor set layout.");
  }

  m_DescriptorSetLayouts.push_back(layout);
  return static_cast<void*>(layout);
}

void VkRHIBackend::destroyDescriptorSetLayout(void* layoutHandle) {
  if (layoutHandle == nullptr) return;

  VkDescriptorSetLayout layout = static_cast<VkDescriptorSetLayout>(layoutHandle);
  auto it = std::find(m_DescriptorSetLayouts.begin(), m_DescriptorSetLayouts.end(), layout);
  if (it != m_DescriptorSetLayouts.end()) {
    vkDestroyDescriptorSetLayout(mDevice, *it, nullptr);
    m_DescriptorSetLayouts.erase(it);
  }
}

void* VkRHIBackend::allocateDescriptorSet(void* layoutHandle) {
  if (layoutHandle == nullptr) return nullptr;

  if (m_DescriptorPool == VK_NULL_HANDLE) {
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512 };
    poolSizes[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128 };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 256;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create descriptor pool.");
    }
  }

  VkDescriptorSetLayout layout = static_cast<VkDescriptorSetLayout>(layoutHandle);
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = m_DescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &layout;

  VkDescriptorSet set = VK_NULL_HANDLE;
  if (vkAllocateDescriptorSets(mDevice, &allocInfo, &set) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate descriptor set.");
  }

  return static_cast<void*>(set);
}

void VkRHIBackend::freeDescriptorSet(void* setHandle) {
  if (setHandle == nullptr || m_DescriptorPool == VK_NULL_HANDLE) return;
  VkDescriptorSet set = static_cast<VkDescriptorSet>(setHandle);
  vkFreeDescriptorSets(mDevice, m_DescriptorPool, 1, &set);
}

void VkRHIBackend::updateDescriptorSet(void* setHandle, const RHIDescriptorWrite* writes, u32 writeCount) {
  if (setHandle == nullptr || writes == nullptr || writeCount == 0) return;

  VkDescriptorSet set = static_cast<VkDescriptorSet>(setHandle);
  std::vector<VkDescriptorImageInfo> imageInfos;
  std::vector<VkDescriptorBufferInfo> bufferInfos;
  std::vector<VkWriteDescriptorSet> vkWrites;

  imageInfos.reserve(writeCount);
  bufferInfos.reserve(writeCount);
  vkWrites.reserve(writeCount);

  for (u32 i = 0; i < writeCount; ++i) {
    const RHIDescriptorWrite& w = writes[i];
    VkWriteDescriptorSet out{};
    out.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    out.dstSet = set;
    out.dstBinding = w.binding;
    out.dstArrayElement = 0;
    out.descriptorCount = 1;
    out.descriptorType = toVkDescriptorType(w.type);

    switch (w.type) {
    case RHI_DESCRIPTOR_COMBINED_IMAGE_SAMPLER: {
      auto* img = static_cast<VkRHIImage*>(w.textureHandle);
      auto sampler = static_cast<VkSampler>(w.samplerHandle);
      if (img == nullptr || img->imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
        throw std::runtime_error("updateDescriptorSet: binding " + std::to_string(w.binding) + " needs a texture with an image view and a sampler.");
      }

      imageInfos.push_back(VkDescriptorImageInfo{
        sampler,
        img->imageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
      });
      out.pImageInfo = &imageInfos.back();
      vkWrites.push_back(out);
      break;
    }
    
    case RHI_DESCRIPTOR_UNIFORM_BUFFER: {
      VkBuffer buffer = static_cast<VkBuffer>(w.bufferHandle);
      if (buffer == VK_NULL_HANDLE)
        {throw std::runtime_error("updateDescriptorSet: binding " + std::to_string(w.binding) + " needs a buffer handle.");}

      bufferInfos.push_back(VkDescriptorBufferInfo{buffer, w.bufferOffset, w.bufferRange > 0 ? w.bufferRange : VK_WHOLE_SIZE});
      out.pBufferInfo = &bufferInfos.back();
      vkWrites.push_back(out);
      break;
    }
    }
  } 

  if (!vkWrites.empty()) {
    vkUpdateDescriptorSets(mDevice, static_cast<u32>(vkWrites.size()), vkWrites.data(), 0, nullptr);
  }
}

void VkRHIBackend::bindDescriptorSet(void* cmdBuf, void* pipelineHandle, u32 setIndex, void* setHandle) {
  if (pipelineHandle == nullptr || setHandle == nullptr) return;
  if (setIndex != 0) {
    throw std::runtime_error("bindDescriptorSet: pipeline layouts currently expose only set 0.");
  }

  VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(cmdBuf); 
  if (commandBuffer == VK_NULL_HANDLE) {
    commandBuffer = m_CommandBuffer;
  }

  auto* p = static_cast<VkRHIPipeline*>(pipelineHandle);
  VkDescriptorSet set = static_cast<VkDescriptorSet>(setHandle);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, p->layout, setIndex, 1, &set, 0, nullptr);
}