#pragma once
#include "../rg/RGTypes.h"

struct RGMemoryBlock {
  u32   id;
  u64   size;
  void* handle;  // opaque backend handle
};

struct RGSubAllocation {
  u32 pool_id;
  u64 offset;
  u64 size;
};

// Raw block allocator — see issue #10 for multi-memory-type design notes.
// TO DO #10
struct IGPUAllocator {
  IGPUAllocator() = default;
  IGPUAllocator(const IGPUAllocator&) = delete;
  IGPUAllocator& operator=(const IGPUAllocator&) = delete;
  virtual ~IGPUAllocator() = default;

  virtual RGMemoryBlock allocate(u64 size, u32 memoryTypeIndex) = 0;
  virtual void          free(RGMemoryBlock block) = 0;
};

// Byte-range manager within a RGMemoryBlock — see issues #22, #23 for aliasing and caching design.
// TO DO #22, #23
struct IGPUSubAllocator {
  IGPUSubAllocator() = default;
  IGPUSubAllocator(const IGPUSubAllocator&) = delete;
  IGPUSubAllocator& operator=(const IGPUSubAllocator&) = delete;
  virtual ~IGPUSubAllocator() = default;

  virtual void init(RGMemoryBlock block) = 0;
  virtual void free(RGSubAllocation alloc) = 0;

  virtual RGSubAllocation suballocate(u64 size, u64 alignment) = 0;
  virtual RGSubAllocation alias(u64 offset, u64 size, u64 alignment) = 0;
};
