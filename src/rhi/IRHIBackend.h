#pragma once
#include "../gpu_types.h"

// Backend interface the render graph calls into for all GPU operations.
// The concrete implementation (e.g. VkRHIBackend) owns its IGPUAllocator internally.
// The render graph stores one IRHIBackend* set via RenderGraph::setBackend().
struct IRHIBackend {
  IRHIBackend() = default;
  IRHIBackend(const IRHIBackend&) = delete;
  IRHIBackend& operator=(const IRHIBackend&) = delete;
  virtual ~IRHIBackend() = default;

  // Memory pool management - called by allocate(). Wraps the internal IGPUAllocator.
  virtual GPUMemoryBlock allocatePool(u64 size, RGMemoryType memoryType) = 0;
  virtual void           freePool(GPUMemoryBlock block) = 0;

  // Bind a GPU resource handle to a memory block at a given offset - called by allocate().
  // gpuHandle is VkImage/VkBuffer (or equivalent) cast to void*.
  virtual void bindMemory(void* gpuHandle, GPUMemoryBlock block, u64 offset) = 0;

  // Emit a pipeline barrier before a pass - called by execute(). See issue #11.
  virtual void emitBarrier(const RGBarrierInfo& info, void* cmdBuf) = 0;

  // Pass framing - called by execute() around each pass executor. See issue #12.
  virtual void beginPass(void* cmdBuf) = 0;
  virtual void endPass(void* cmdBuf) = 0;
};
