#pragma once
#include "../gpu_types.h"

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
enum RGPassFlags : u32 {
  RG_PASS_NONE          = 0,
  RG_PASS_RASTER        = 1 << 0,
  RG_PASS_COMPUTE       = 1 << 1,
  RG_PASS_ASYNC_COMPUTE = 1 << 2,
  RG_PASS_COPY          = 1 << 3,
  RG_PASS_NEVER_CULL    = 1 << 4,
};

inline RGPassFlags operator|(RGPassFlags a, RGPassFlags b) {
  return static_cast<RGPassFlags>(static_cast<u32>(a) | static_cast<u32>(b));
}

inline RHIQueueType rg_queue_from_flags(RGPassFlags f) {
  if (f & RG_PASS_ASYNC_COMPUTE) return RHI_QUEUE_ASYNC_COMPUTE;
  if (f & RG_PASS_COMPUTE)       return RHI_QUEUE_COMPUTE;
  if (f & RG_PASS_COPY)          return RHI_QUEUE_TRANSFER;
  return RHI_QUEUE_GRAPHICS;
}

// Ownership model: transient resources are allocated by the graph; external ones are imported.
enum RGResourceType : u32 {
  RG_RESOURCE_TRANSIENT = 0,
  RG_RESOURCE_EXTERNAL  = 1,
};
