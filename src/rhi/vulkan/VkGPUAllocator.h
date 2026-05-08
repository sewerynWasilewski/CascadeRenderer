#pragma once
#include <vector>
#include "../IGPUAllocator.h"

struct VmaAllocator_T;
struct VmaAllocation_T;
using VmaAllocator  = VmaAllocator_T*;
using VmaAllocation = VmaAllocation_T*;

// Vulkan raw block allocator — implements IGPUAllocator. Currently backed by VMA (see issue #10).
// Allocates one large persistent VkDeviceMemory block per render graph.
// Does not know about resources, lifetimes, or aliasing — that is IGPUSubAllocator's job.
class VkGPUAllocator final : public IGPUAllocator {
public:
  // TO DO: accept VkInstance, VkPhysicalDevice, VkDevice and call vmaCreateAllocator
  VkGPUAllocator()  = default;
  ~VkGPUAllocator() = default;

  RGMemoryBlock allocate(u64 size, u32 memoryTypeIndex) override {
    // TO DO: vmaAllocateMemory with size and memoryTypeIndex, fill RGMemoryBlock from VmaAllocationInfo
    return RGMemoryBlock{ 0, size, nullptr };
  }

  void free(RGMemoryBlock block) override {
    // TO DO: vmaFreeMemory using block.handle
  }

private:
  VmaAllocator mAllocator = nullptr;
};

// Vulkan sub-allocator — manages byte ranges within an RGMemoryBlock via a sorted free list.
// See issues #22, #23 for the full aliasing and caching design.
class VkGPUSubAllocator final : public IGPUSubAllocator {
public:
  void init(RGMemoryBlock block) override;

  // Best-fit search through the free list. Splits the chosen range.
  RGSubAllocation suballocate(u64 size, u64 alignment) override;

  // Pre-planned placement at an explicit offset. Removes the range from the free list.
  RGSubAllocation alias(u64 offset, u64 size, u64 alignment) override;

  // Returns range to the free list and merges adjacent ranges (enables cross-boundary aliasing).
  void free(RGSubAllocation alloc) override;

private:
  struct Range { u64 offset; u64 size; };

  RGMemoryBlock      mBlock{};
  std::vector<Range> mFreeList;
};
