#pragma once
#include <vulkan/vulkan.h>
#include "../IRHIBackend.h"
#include "VkRHIAllocator.h"
#include "../../rg/RGTypes.h"

class VkRHIBackend final : public IRHIBackend {
public:
  VkRHIBackend();
  ~VkRHIBackend() override;

  void* createImage(const RHITextureDesc& desc) override;
  void* createBuffer(const RHIBufferDesc& desc) override;
  void  destroyImage(void* handle) override;
  void  destroyBuffer(void* handle) override;

  RHIMemoryRequirements getMemoryRequirements(void* gpuHandle, RHIResourceKind kind) override;

  RHIMemoryBlock allocatePool(u64 size, RHIMemoryType memoryType) override;
  void           freePool(RHIMemoryBlock block) override;

  void bindMemory(void* gpuHandle, RHIMemoryBlock block, u64 offset) override;
  void emitBarrier(const RHIBarrierInfo& info, void* cmdBuf) override;
  void beginPass(void* cmdBuf, RHIPassType type) override;
  void endPass(void* cmdBuf) override;

  VkInstance       getInstance()       const { return mInstance; }
  VkPhysicalDevice getPhysicalDevice() const { return mPhysicalDevice; }
  VkDevice         getDevice()         const { return mDevice; }
  VkQueue          getGraphicsQueue()  const { return mGraphicsQueue; }
  u32              getGraphicsQueueFamily() const { return mGraphicsQueueFamily; }

private:
  void createInstance();
  void setupDebugMessenger();
  void pickPhysicalDevice();
  void createLogicalDevice();

  bool checkValidationLayerSupport();

  VkInstance               mInstance            = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT mDebugMessenger      = VK_NULL_HANDLE;
  VkPhysicalDevice         mPhysicalDevice      = VK_NULL_HANDLE;
  VkDevice                 mDevice              = VK_NULL_HANDLE;
  VkQueue                  mGraphicsQueue       = VK_NULL_HANDLE;
  u32                      mGraphicsQueueFamily = RG_INVALID_ID;

  VkRHIAllocator mAllocator;
};

// Vulkan backend - implements IRHIBackend and owns the VkRHIAllocator.
// TO DO #7, #8: initialize with VkInstance, VkPhysicalDevice, VkDevice, VkQueue.
// TO DO #23: fill VkImageCreateInfo from desc, call vkCreateImage (unbound - no memory yet)
// TO DO: vkDestroyImage
// TO DO #23: fill VkBufferCreateInfo from desc, call vkCreateBuffer (unbound - no memory yet)
// TO DO: vkDestroyBuffer
// TO DO #23: vkGetImageMemoryRequirements / vkGetBufferMemoryRequirements
// TO DO #10: cast block.handle to VkDeviceMemory, call vkBindImageMemory / vkBindBufferMemory
// TO DO #11: translate RHIBarrierInfo to VkImageMemoryBarrier2, call vkCmdPipelineBarrier2
// Queue translation: RHI_QUEUE_IGNORED → VK_QUEUE_FAMILY_IGNORED (0xFFFFFFFF)
// Other RHIQueueType values map to the VkQueueFamilyIndex acquired during device init.
// TO DO #12: vkCmdBeginRendering for RHI_PASS_RASTER; debug marker for compute/copy
// TO DO #12: vkCmdEndRendering