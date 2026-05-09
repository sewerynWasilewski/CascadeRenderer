#pragma once
#include "gpu_types.h"

// Raw block allocator - see issue #10 for VMA implementation notes.
// One GPUMemoryBlock per RGMemoryType is allocated and persisted across frames.
struct IGPUAllocator {
  IGPUAllocator() = default;
  IGPUAllocator(const IGPUAllocator&) = delete;
  IGPUAllocator& operator=(const IGPUAllocator&) = delete;
  virtual ~IGPUAllocator() = default;

  virtual GPUMemoryBlock allocate(u64 size, RGMemoryType memoryType) = 0;
  virtual void           free(GPUMemoryBlock block) = 0;
};
