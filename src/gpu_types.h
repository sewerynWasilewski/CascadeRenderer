#pragma once
#include <cstdint>

using u32 = uint32_t;
using u64 = uint64_t;

constexpr u32 RG_INVALID_ID = UINT32_MAX;

// Opaque handle to a raw GPU memory block allocated by IGPUAllocator.
// handle is cast to the backend-specific type (e.g. VmaAllocation) by the allocator.
struct GPUMemoryBlock {
  u32   id;
  u64   size;
  void* handle;
};

// Backend-agnostic memory type. The backend maps this to Vulkan memoryTypeIndex / D3D12 heap type / Metal storage mode.
// Declared per-resource by the programmer at create() / import() time.
// plan() groups transient resources by type — each type gets a separate GPUMemoryBlock.

constexpr u32 MAX_RG_MEMORY_TYPE_INDEX = 2;

enum RGMemoryType : u32 {
  RG_MEMORY_GPU_ONLY   = 0,  // device-local: render targets, depth, GPU-only buffers
  RG_MEMORY_CPU_TO_GPU = 1,  // host-visible + coherent: staging buffers, per-frame uniforms
  RG_MEMORY_GPU_TO_CPU = 2,  // host-visible + cached: readback buffers
};

// Whether a resource is a texture or a buffer.
enum RGResourceKind : u32 {
  RG_RESOURCE_TEXTURE = 0,
  RG_RESOURCE_BUFFER  = 1,
};

// Transition: normal usage change. Aliasing: memory reuse - receiver starts from UNDEFINED layout.
enum RGBarrierKind : u32 {
  RG_BARRIER_TRANSITION = 0,
  RG_BARRIER_ALIASING   = 1,
};

// Barrier description passed to IRHIBackend::emitBarrier().
// before/after_usage store RGUsage bitmask values as u32 to avoid circular dependency on RGTypes.h.
struct RGBarrierInfo {
  u32           resource_id;
  u32           before_usage;
  u32           after_usage;
  RGBarrierKind kind;
};

struct RHITextureDesc {
  u32 width;
  u32 height;
  u32 depth;
  u32 mipLevels;
  u32 arrayLayers;
};

struct RHIBufferDesc {
  u64 size;
};

struct RHIMemoryRequirements {
  u64 size;
  u64 alignment;
};
