#pragma once
#include "RGTypes.h"

// Per-pass metadata stored in the flat mPasses array. Filled by addPass; global_index set by compile().
struct RG_PassData {
  const char*  name;
  RG_PassFlags flags;
  RG_QueueType queue;         // derived from flags at addPass time
  u32          first_usage;   // index into RenderGraph::mUsages
  u32          usage_count;
  u32          ref_count;     // for dead-pass culling
  u32          global_index;  // position in topological order - set by compile()
};

// Backend allocation record assigned by allocate(). Aliased resources share a pool_id but differ in offset.
struct RG_PhysicalResource {
  u32 pool_id;  // which memory pool (placeholder - will become RHI_MemoryPool)
  u64 offset;   // byte offset within the pool
  u64 size;     // byte size of the allocation
};

// Logical resource metadata. physical_id is RG_INVALID_ID until allocate() runs.
struct RG_ResourceData {
  const char*     name;
  RG_ResourceKind kind;
  RG_ResourceType type;
  u32             desc_index;   // index into mResourceHandlers
  u32             version;      // incremented each time a pass writes this resource
  u32             first_pass;   // global pass index of first use - set by compile()
  u32             last_pass;    // global pass index of last use  - set by compile()
  RGQueueMask     queue_mask;   // which queues access this - accumulated during setup
  u32             physical_id;  // set by allocate() -- RG_INVALID_ID until then
};

// One read or write declaration from a pass setup callback. Stored flat in mUsages; passes index into it.
struct RG_ResourceUsage {
  u32      pass_id;
  u32      resource_id;
  u32      version;
  RG_Usage usage;
  bool     is_write;
};

// Dependency edge between two passes for a specific resource version. Built by compile() from matching write->read pairs.
struct RG_Edge {
  u32 from_pass;
  u32 to_pass;
  u32 resource_id;
  u32 version;
};

// GPU barrier scheduled before dst_pass. ALIASING barriers additionally signal memory reuse from UNDEFINED layout.
struct RG_Barrier {
  u32          resource_id;
  u32          before_usage;
  u32          after_usage;
  u32          src_pass;
  u32          dst_pass;
  RG_BarrierKind kind;
};
