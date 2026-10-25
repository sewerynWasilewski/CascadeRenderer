#pragma once
#include <vulkan/vulkan.h>
#include <unordered_map>
#include "../IRHIAllocator.h"

struct VmaAllocator_T;
struct VmaAllocation_T;
using VmaAllocator  = VmaAllocator_T*;
using VmaAllocation = VmaAllocation_T*;

class VkRHIAllocator final : public IRHIAllocator {
public:
  VkRHIAllocator()  = default;
  ~VkRHIAllocator() override;

  void init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
  void shutdown();

  RHIMemoryBlock allocate(u64 size, RHIMemoryType memoryType) override;
  void           free(RHIMemoryBlock block) override;

  struct AllocationDetails {
    VmaAllocation  allocation   = nullptr;
    VkDeviceMemory deviceMemory = VK_NULL_HANDLE;
    VkDeviceSize   baseOffset   = 0;
  };

  const AllocationDetails* getAllocationDetails(u32 blockId) const;

private:
  VmaAllocator mAllocator   = nullptr;
  VkDevice     mDevice      = VK_NULL_HANDLE;
  u32          mNextBlockId = 1;

  std::unordered_map<u32, AllocationDetails> mAllocations;
};