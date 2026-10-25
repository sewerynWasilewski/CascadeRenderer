#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "VkRHIAllocator.h"
#include <stdexcept>
#include <iostream>

VkRHIAllocator::~VkRHIAllocator() {
    shutdown();
}

void VkRHIAllocator::init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
    mDevice = device;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice   = physicalDevice;
    allocatorInfo.device           = device;
    allocatorInfo.instance         = instance;

    if (vmaCreateAllocator(&allocatorInfo, &mAllocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator.");
    }
    std::cout << "[VkRHIAllocator] VMA initialized successfully.\n";
}

void VkRHIAllocator::shutdown() {
    if (mAllocator != nullptr) {
        for (auto& [id, details] : mAllocations) {
            if (details.allocation != nullptr) {
                vmaFreeMemory(mAllocator, details.allocation);
            }
        }
        mAllocations.clear();

        vmaDestroyAllocator(mAllocator);
        mAllocator = nullptr;
        std::cout << "[VkRHIAllocator] VMA destroyed.\n";
    }
}

RHIMemoryBlock VkRHIAllocator::allocate(u64 size, RHIMemoryType memoryType) {
    if (mAllocator == nullptr || size == 0) {
        return RHIMemoryBlock{ 0, 0, nullptr };
    }

    VkMemoryRequirements memReq{};
    memReq.size           = size;
    memReq.alignment      = 256;
    memReq.memoryTypeBits = 0xFFFFFFFF;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

    switch (memoryType) {
        case RHI_MEMORY_GPU_ONLY:
            allocCreateInfo.requiredFlags  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case RHI_MEMORY_CPU_TO_GPU:
            allocCreateInfo.requiredFlags  = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            allocCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            allocCreateInfo.flags          = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            break;
        case RHI_MEMORY_GPU_TO_CPU:
            allocCreateInfo.requiredFlags  = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            allocCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            allocCreateInfo.flags          = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            break;
        default:
            throw std::runtime_error("Unsupported RHIMemoryType");
    }

    VmaAllocation allocation = nullptr;
    VmaAllocationInfo allocInfo{};

    if (vmaAllocateMemory(mAllocator, &memReq, &allocCreateInfo, &allocation, &allocInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate GPU memory block via VMA");
    }

    u32 blockId = mNextBlockId++;
    mAllocations[blockId] = AllocationDetails{
        allocation,
        allocInfo.deviceMemory,
        allocInfo.offset
    };

    return RHIMemoryBlock{
        blockId,
        size,
        reinterpret_cast<void*>(allocation)
    };
}

void VkRHIAllocator::free(RHIMemoryBlock block) {
    if (mAllocator == nullptr || block.handle == nullptr) {
        return;
    }

    auto allocation = reinterpret_cast<VmaAllocation>(block.handle);
    vmaFreeMemory(mAllocator, allocation);
    mAllocations.erase(block.id);
}

const VkRHIAllocator::AllocationDetails* VkRHIAllocator::getAllocationDetails(u32 blockId) const {
    auto it = mAllocations.find(blockId);
    return (it != mAllocations.end()) ? &it->second : nullptr;
}