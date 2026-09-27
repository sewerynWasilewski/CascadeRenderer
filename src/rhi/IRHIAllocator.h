#pragma once
#include "../rhi_types.h"

// Raw block allocator - see issue #10 for VMA implementation notes.
// One RHIMemoryBlock per RHIMemoryType is allocated and persisted across frames.
struct IRHIAllocator {
  IRHIAllocator() = default;
  IRHIAllocator(const IRHIAllocator&) = delete;
  IRHIAllocator& operator=(const IRHIAllocator&) = delete;
  virtual ~IRHIAllocator() = default;

  virtual RHIMemoryBlock allocate(u64 size, RHIMemoryType memoryType) = 0;
  virtual void           free(RHIMemoryBlock block) = 0;
};
