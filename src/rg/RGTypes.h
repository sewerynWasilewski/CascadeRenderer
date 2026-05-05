#pragma once
#include <cstdint>

using u32 = uint32_t;
using u64 = uint64_t;

constexpr u32 RG_INVALID_ID = UINT32_MAX;

// Opaque index into the graph's pass array. Returned by addPass, not versioned.
struct RGPassHandle {
  u32  id = RG_INVALID_ID;
  bool valid() const { return id != RG_INVALID_ID; }
};

// Opaque index into the graph's resource array. Carries a version so the graph
// can track which write produced the value a pass is reading.
struct RGResourceHandle {
  u32  id      = RG_INVALID_ID;
  u32  version = 0;
  bool valid() const { return id != RG_INVALID_ID; }
};

// Controls which GPU queue a pass runs on and whether it is exempt from culling.
enum RG_PassFlags : u32 {
  RG_PASS_NONE          = 0,
  RG_PASS_RASTER        = 1 << 0,
  RG_PASS_COMPUTE       = 1 << 1,
  RG_PASS_ASYNC_COMPUTE = 1 << 2,
  RG_PASS_COPY          = 1 << 3,
  RG_PASS_NEVER_CULL    = 1 << 4,
};

// GPU queue families. Each pass is assigned one; cross-queue resources need ownership transfers.
enum RG_QueueType : u32 {
  RG_QUEUE_GRAPHICS      = 0,
  RG_QUEUE_COMPUTE       = 1,
  RG_QUEUE_ASYNC_COMPUTE = 2,
  RG_QUEUE_TRANSFER      = 3,
};

// Bitmask of RG_QueueType values - tracks which queues access a resource.
using RGQueueMask = u32;
inline RGQueueMask rg_queue_bit(RG_QueueType q)     { return 1u << static_cast<u32>(q); }
inline bool        rg_is_cross_queue(RGQueueMask m) { return m != 0 && (m & (m - 1)) != 0; }

inline RG_QueueType rg_queue_from_flags(RG_PassFlags f) {
  if (f & RG_PASS_ASYNC_COMPUTE) return RG_QUEUE_ASYNC_COMPUTE;
  if (f & RG_PASS_COMPUTE)       return RG_QUEUE_COMPUTE;
  if (f & RG_PASS_COPY)          return RG_QUEUE_TRANSFER;
  return RG_QUEUE_GRAPHICS;
}

// How a pass accesses a resource. Declared on every read/write; drives barrier generation.
enum RG_Usage : u32 {
  RG_USAGE_NONE               = 0,
  RG_USAGE_COLOR_ATTACHMENT   = 1 << 0,
  RG_USAGE_DEPTH_ATTACHMENT   = 1 << 1,
  RG_USAGE_SAMPLED_TEXTURE    = 1 << 2,
  RG_USAGE_STORAGE_TEXTURE    = 1 << 3,
  RG_USAGE_TRANSFER_SRC       = 1 << 4,
  RG_USAGE_TRANSFER_DST       = 1 << 5,
  RG_USAGE_VERTEX_BUFFER      = 1 << 6,
  RG_USAGE_INDEX_BUFFER       = 1 << 7,
  RG_USAGE_UNIFORM_BUFFER     = 1 << 8,
};

// Whether a resource is a texture or a buffer.
enum RG_ResourceKind : u32 {
  RG_RESOURCE_TEXTURE = 0,
  RG_RESOURCE_BUFFER  = 1,
};

// Ownership model: transient resources are allocated by the graph; external ones are imported.
enum RG_ResourceType : u32 {
  RG_RESOURCE_TRANSIENT = 0,
  RG_RESOURCE_EXTERNAL  = 1,
};

// Transition: normal usage change. Aliasing: memory reuse - receiver starts from UNDEFINED layout.
enum RG_BarrierKind : u32 {
  RG_BARRIER_TRANSITION = 0,
  RG_BARRIER_ALIASING   = 1,
};
