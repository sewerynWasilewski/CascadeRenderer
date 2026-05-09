#pragma once
#include "../IRHIBackend.h"
#include "VkGPUAllocator.h"

// Vulkan backend - implements IRHIBackend and owns the VkGPUAllocator.
// TO DO #7, #8: initialize with VkInstance, VkPhysicalDevice, VkDevice, VkQueue.
class VkRHIBackend final : public IRHIBackend {
public:
  VkRHIBackend()  = default;
  ~VkRHIBackend() = default;

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
    // TO DO #12: vkBeginCommandBuffer or vkCmdBeginRendering depending on pass type
  }

  void endPass(void* cmdBuf) override {
    // TO DO #12: vkCmdEndRendering / submit
  }

private:
  VkGPUAllocator mAllocator;
};
