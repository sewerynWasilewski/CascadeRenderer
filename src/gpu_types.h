#pragma once
#include <cstdint>

using u32 = uint32_t;
using u64 = uint64_t;

constexpr u32 RG_INVALID_ID = UINT32_MAX;

// Opaque handle to a raw GPU memory block allocated by IGPUAllocator.
struct GPUMemoryBlock {
  u32   id;
  u64   size;
  void* handle;
};

// Backend-agnostic memory type. Maps to Vulkan memoryTypeIndex / D3D12 heap type / Metal storage mode.
// Declared per-resource at create() / import() time. plan() groups transient resources by type.
constexpr u32 MAX_RHI_MEMORY_TYPE_INDEX = 2;

enum RHIMemoryType : u32 {
  RHI_MEMORY_GPU_ONLY   = 0,  // device-local: render targets, depth, GPU-only buffers
  RHI_MEMORY_CPU_TO_GPU = 1,  // host-visible + coherent: staging buffers, per-frame uniforms
  RHI_MEMORY_GPU_TO_CPU = 2,  // host-visible + cached: readback buffers
};

// Whether a GPU resource is a texture or a buffer.
enum RHIResourceKind : u32 {
  RHI_RESOURCE_TEXTURE = 0,
  RHI_RESOURCE_BUFFER  = 1,
};

// GPU queue families. Each pass is assigned one; cross-queue resources need ownership transfers.
enum RHIQueueType : u32 {
  RHI_QUEUE_GRAPHICS      = 0,
  RHI_QUEUE_COMPUTE       = 1,
  RHI_QUEUE_ASYNC_COMPUTE = 2,
  RHI_QUEUE_TRANSFER      = 3,
};

// Bitmask of RHIQueueType values — tracks which queues access a resource.
using RHIQueueMask = u32;
inline RHIQueueMask rhi_queue_bit(RHIQueueType q)      { return 1u << static_cast<u32>(q); }
inline bool         rhi_is_cross_queue(RHIQueueMask m)  { return m != 0 && (m & (m - 1)) != 0; }

// How a pass accesses a resource. Declared on every read/write; drives barrier generation.
enum RHIUsage : u32 {
  RHI_USAGE_NONE               = 0,
  RHI_USAGE_COLOR_ATTACHMENT   = 1 << 0,
  RHI_USAGE_DEPTH_ATTACHMENT   = 1 << 1,
  RHI_USAGE_SAMPLED_TEXTURE    = 1 << 2,
  RHI_USAGE_STORAGE_TEXTURE    = 1 << 3,
  RHI_USAGE_TRANSFER_SRC       = 1 << 4,
  RHI_USAGE_TRANSFER_DST       = 1 << 5,
  RHI_USAGE_VERTEX_BUFFER      = 1 << 6,
  RHI_USAGE_INDEX_BUFFER       = 1 << 7,
  RHI_USAGE_UNIFORM_BUFFER     = 1 << 8,
};

// Transition: normal usage change. Aliasing: memory reuse — receiver starts from UNDEFINED layout.
enum RHIBarrierKind : u32 {
  RHI_BARRIER_TRANSITION = 0,
  RHI_BARRIER_ALIASING   = 1,
};

// Pass type forwarded to IRHIBackend::beginPass — lets the backend pick the right Vk call.
enum RHIPassType : u32 {
  RHI_PASS_RASTER  = 0,  // vkCmdBeginRendering
  RHI_PASS_COMPUTE = 1,  // debug marker or nothing
  RHI_PASS_COPY    = 2,  // debug marker or nothing
};

// Barrier description passed to IRHIBackend::emitBarrier(). Built by execute() from RGBarrier.
// src/dst_queue_family: use VK_QUEUE_FAMILY_IGNORED (0xFFFFFFFF) for single-queue rendering.
struct RHIBarrierInfo {
  void*           handle;           // VkImage or VkBuffer cast to void*
  u32             resource_id;      // for debug/logging
  RHIResourceKind resource_kind;    // TEXTURE or BUFFER — determines which Vk barrier struct to fill
  RHIUsage        before_usage;
  RHIUsage        after_usage;
  RHIBarrierKind  kind;
  u32             src_queue_family; // VK_QUEUE_FAMILY_IGNORED for single-queue
  u32             dst_queue_family;
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
