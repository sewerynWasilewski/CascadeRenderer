#pragma once
#include "RGTypes.h"
#include "../rhi/IGPUAllocator.h"

// Per-pass metadata stored in the flat mPasses array. Filled by addPass; global_index set by compile().
struct RGPassData {
  const char*  name;
  RGPassFlags flags;
  RGQueueType queue;         // derived from flags at addPass time
  u32          first_usage;   // index into RenderGraph::mUsages
  u32          usage_count;
  u32          ref_count;     // for dead-pass culling
  u32          global_index;  // position in topological order - set by compile()
};

// Logical resource metadata. physical_id is RG_INVALID_ID until allocate() runs.
// physical_id indexes into RenderGraph::mPhysicalResources (vector of RGSubAllocation).
struct RGResourceData {
  const char*     name;
  RGResourceKind kind;
  RGResourceType type;
  u32             desc_index;   // index into mResourceHandlers
  u32             version;      // incremented each time a pass writes this resource
  u32             first_pass;   // global pass index of first use - set by compile()
  u32             last_pass;    // global pass index of last use  - set by compile()
  RGQueueMask     queue_mask;   // which queues access this - accumulated during setup
  u32             physical_id;  // set by allocate() -- RG_INVALID_ID until then
};

// One read or write declaration from a pass setup callback. Stored flat in mUsages; passes index into it.
struct RGResourceUsage {
  u32      pass_id;
  u32      resource_id;
  u32      version;
  RGUsage usage;
  bool     is_write;
};

// Dependency edge between two passes for a specific resource version. Built by compile() from matching write->read pairs.
struct RGEdge {
  u32 from_pass;
  u32 to_pass;
  u32 resource_id;
  u32 version;
};

// GPU barrier scheduled before dst_pass. ALIASING barriers additionally signal memory reuse from UNDEFINED layout.
struct RGBarrier {
  u32          resource_id;
  u32          before_usage;
  u32          after_usage;
  u32          src_pass;
  u32          dst_pass;
  RGBarrierKind kind;
};
