#pragma once
#include "../IRHIBackend.h"
#include "VkGPUAllocator.h"

// Vulkan backend — implements IRHIBackend and owns the VkGPUAllocator.
// TO DO #7, #8: initialize with VkInstance, VkPhysicalDevice, VkDevice, VkQueue.
class VkRHIBackend final : public IRHIBackend {
public:
  VkRHIBackend()  = default;
  ~VkRHIBackend() = default;

  void* createImage(const RHITextureDesc& desc) override {
    // TO DO #23: fill VkImageCreateInfo from desc, call vkCreateImage (unbound — no memory yet)
    return nullptr;
  }

  void* createBuffer(const RHIBufferDesc& desc) override {
    // TO DO #23: fill VkBufferCreateInfo from desc, call vkCreateBuffer (unbound — no memory yet)
    return nullptr;
  }

  void destroyImage(void* handle) override {
    // TO DO: vkDestroyImage
  }

  void destroyBuffer(void* handle) override {
    // TO DO: vkDestroyBuffer
  }

  RHIMemoryRequirements getMemoryRequirements(void* gpuHandle, RGResourceKind kind) override {
    // TO DO #23: vkGetImageMemoryRequirements / vkGetBufferMemoryRequirements
    // return .size and .alignment from VkMemoryRequirements
    return RHIMemoryRequirements{ 0, 1 };
  }

  GPUMemoryBlock allocatePool(u64 size, RGMemoryType memoryType) override {
    return mAllocator.allocate(size, memoryType);
  }

  void freePool(GPUMemoryBlock block) override {
    mAllocator.free(block);
  }

  void bindMemory(void* gpuHandle, GPUMemoryBlock block, u64 offset) override {
    // TO DO #10: cast block.handle to VkDeviceMemory, call vkBindImageMemory / vkBindBufferMemory
  }

  void emitBarrier(const RGBarrierInfo& info, void* cmdBuf) override {
    // TO DO #11: translate RGBarrierInfo to VkImageMemoryBarrier2, call vkCmdPipelineBarrier2
  }

  void beginPass(void* cmdBuf) override {
    // TO DO #12: vkCmdBeginRendering
  }

  void endPass(void* cmdBuf) override {
    // TO DO #12: vkCmdEndRendering
  }

private:
  VkGPUAllocator mAllocator;
};
