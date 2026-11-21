#pragma once
#include "core/types.h"
#include "rhi/types.h"

// Raw block allocator - see issue #10 for VMA implementation notes.
// One RHIMemoryBlock per RHIMemoryType is allocated and persisted across frames.
struct IRHIAllocator {
  IRHIAllocator() = default;
  IRHIAllocator(const IRHIAllocator&) = delete;
  IRHIAllocator& operator=(const IRHIAllocator&) = delete;
  virtual ~IRHIAllocator() = default;

  virtual RHIMemoryBlock allocate(u64 size, RHIMemoryType memoryType) = 0;
  virtual void           free(RHIMemoryBlock block) = 0;

  // Host-visible memory only (RHI_MEMORY_CPU_TO_GPU / RHI_MEMORY_GPU_TO_CPU).
  virtual void* map(RHIMemoryBlock block) = 0;
  virtual void  unmap(RHIMemoryBlock block) = 0;
};
