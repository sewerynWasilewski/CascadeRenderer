#pragma once
#include "gpu_types.h"

// Raw block allocator - see issue #10 for VMA implementation notes.
// One GPUMemoryBlock per RHIMemoryType is allocated and persisted across frames.
struct IGPUAllocator {
  IGPUAllocator() = default;
  IGPUAllocator(const IGPUAllocator&) = delete;
  IGPUAllocator& operator=(const IGPUAllocator&) = delete;
  virtual ~IGPUAllocator() = default;

  virtual GPUMemoryBlock allocate(u64 size, RHIMemoryType memoryType) = 0;
  virtual void           free(GPUMemoryBlock block) = 0;
};
