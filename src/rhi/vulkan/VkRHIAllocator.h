#pragma once
#include "../IRHIAllocator.h"

struct VmaAllocator_T;
struct VmaAllocation_T;
using VmaAllocator  = VmaAllocator_T*;
using VmaAllocation = VmaAllocation_T*;

// Vulkan raw block allocator - implements IRHIAllocator. Currently backed by VMA (see issue #10).
// Allocates one large persistent VkDeviceMemory block per RHIMemoryType.
// Placement of resources within the block is handled by plan() - not this class.
class VkRHIAllocator final : public IRHIAllocator {
public:
  // TO DO: accept VkInstance, VkPhysicalDevice, VkDevice and call vmaCreateAllocator
  VkRHIAllocator()  = default;
  ~VkRHIAllocator() = default;

  RHIMemoryBlock allocate(u64 size, RHIMemoryType memoryType) override {
    // TO DO: map RHIMemoryType to VMA_MEMORY_USAGE_* or VkMemoryPropertyFlags,
    // call vmaAllocateMemory, fill RHIMemoryBlock from VmaAllocationInfo
    return RHIMemoryBlock{ 0, size, nullptr };
  }

  void free(RHIMemoryBlock block) override {
    // TO DO: vmaFreeMemory using block.handle cast to VmaAllocation
  }

private:
  VmaAllocator mAllocator = nullptr;
};
