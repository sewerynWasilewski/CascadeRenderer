#pragma once
#include "../IGPUAllocator.h"

struct VmaAllocator_T;
struct VmaAllocation_T;
using VmaAllocator  = VmaAllocator_T*;
using VmaAllocation = VmaAllocation_T*;

// Vulkan raw block allocator - implements IGPUAllocator. Currently backed by VMA (see issue #10).
// Allocates one large persistent VkDeviceMemory block per RHIMemoryType.
// Placement of resources within the block is handled by plan() - not this class.
class VkGPUAllocator final : public IGPUAllocator {
public:
  // TO DO: accept VkInstance, VkPhysicalDevice, VkDevice and call vmaCreateAllocator
  VkGPUAllocator()  = default;
  ~VkGPUAllocator() = default;

  GPUMemoryBlock allocate(u64 size, RHIMemoryType memoryType) override {
    // TO DO: map RHIMemoryType to VMA_MEMORY_USAGE_* or VkMemoryPropertyFlags,
    // call vmaAllocateMemory, fill GPUMemoryBlock from VmaAllocationInfo
    return GPUMemoryBlock{ 0, size, nullptr };
  }

  void free(GPUMemoryBlock block) override {
    // TO DO: vmaFreeMemory using block.handle cast to VmaAllocation
  }

private:
  VmaAllocator mAllocator = nullptr;
};
