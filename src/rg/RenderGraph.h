#pragma once
#include <vector>
#include <memory>
#include <cassert>
#include <functional>
#include <unordered_map>
#include <numeric>
#include <algorithm>
#include <cstdio>

#include "RGTypes.h"
#include "RGData.h"
#include "RGResourceHandler.h"
#include "RGPass.h"
#include "RGTypeTraits.h"
#include "TransientResourcePool.h"
#include "../rhi/IRHIBackend.h"


// May it be that two passes write to same resource?

// Central render graph. Passes register resource reads/writes during setup; compile() resolves
// dependencies and generates barriers; allocate() assigns physical memory; execute() runs the passes.
class RenderGraph {
  friend class RGResources;
public:
  RenderGraph() = default;
  ~RenderGraph() { if (mBackend) destroy(); }
  RenderGraph(const RenderGraph&) = delete;
  RenderGraph(RenderGraph&&) noexcept = delete;

  void setBackend(IRHIBackend* backend) { mBackend = backend; mPool.setBackend(backend); }

  RenderGraph& operator=(const RenderGraph&) = delete;
  RenderGraph& operator=(RenderGraph&&) noexcept = delete;

  // Scoped helper handed to the setup callback of addPass. Declares the reads
  // and writes that define this pass's dependencies in the graph.
  class PassBuilder {
    friend class RenderGraph;
  public:
    PassBuilder() = delete;
    PassBuilder(const PassBuilder&) = delete;
    PassBuilder(PassBuilder&&) noexcept = delete;

    PassBuilder& operator=(const PassBuilder&) = delete;
    PassBuilder& operator=(PassBuilder&&) noexcept = delete;

    // Returns a new handle with incremented version.
    // Use the returned handle for any subsequent reads in other passes.
    RGResourceHandle write(RGResourceHandle handle, RHIUsage usage) {
      assert(handle.valid());
      RGResourceData& res = mRG.mResources[handle.id];
      res.queue_mask |= rhi_queue_bit(mRG.mPasses[mPassId].queue);
      res.version++;
      mRG.mUsages.push_back({ mPassId, handle.id, res.version, usage, true });
      return RGResourceHandle{ handle.id, res.version };
    }

    RGResourceHandle read(RGResourceHandle handle, RHIUsage usage) {
      assert(handle.valid());
      RGResourceData& res = mRG.mResources[handle.id];
      res.queue_mask |= rhi_queue_bit(mRG.mPasses[mPassId].queue);
      mRG.mUsages.push_back({ mPassId, handle.id, handle.version, usage, false });
      return handle;
    }

  private:
    PassBuilder(RenderGraph& rg, u32 passId)
      : mRG(rg), mPassId(passId) {}

    RenderGraph& mRG;
    u32          mPassId;
  };

  template<VIRTUALIZABLE_RESOURCE(T)>
  RGResourceHandle create(const char* name, RHIResourceKind kind, RHIMemoryType memoryType, const typename T::Desc& desc) {
    const u32 id = static_cast<u32>(mResources.size());
    mResourceHandlers.push_back(RGResourceHandler(RG_RESOURCE_TRANSIENT, id, desc, T{}));

    RGResourceData res{};
    res.name           = name;
    res.kind           = kind;
    res.type           = RG_RESOURCE_TRANSIENT;
    res.memory_type    = memoryType;
    res.desc_index     = id;
    res.version        = 0;
    res.first_pass     = RG_INVALID_ID;
    res.last_pass      = RG_INVALID_ID;
    res.queue_mask     = 0;
    res.physical_range = RGMemoryRange{};
    mResources.push_back(res);

    mGPUHandles.push_back(nullptr);
    return RGResourceHandle{ id, 0 };
  }

  template<VIRTUALIZABLE_RESOURCE(T)>
  RGResourceHandle import(const char* name, RHIResourceKind kind, RHIMemoryType memoryType, const typename T::Desc& desc) {
    // TO DO #4: external resource import
    return RGResourceHandle{};
  }

  template<typename Setup, typename Execute>
  RGPassHandle addPass(const char* name, RGPassFlags flags, Setup&& setup, Execute&& exec) {
    static_assert(std::is_invocable_v<Setup, PassBuilder&>,
      "Invalid setup callback");
    static_assert(std::is_invocable_v<Execute, RGResources&, void*>,
      "Invalid exec callback");

    const u32 passId = static_cast<u32>(mPasses.size());

    RGPassData pass{};
    pass.name         = name;
    pass.flags        = flags;
    pass.queue        = rg_queue_from_flags(flags);
    pass.first_usage  = static_cast<u32>(mUsages.size());
    pass.usage_count  = 0;
    pass.ref_count    = 0;
    pass.global_index = RG_INVALID_ID;
    mPasses.push_back(pass);

    mExecutors.emplace_back(std::make_unique<RGPassCallback<Execute>>(std::forward<Execute>(exec)));

    PassBuilder builder(*this, passId);
    std::invoke(setup, builder);

    mPasses[passId].usage_count = static_cast<u32>(mUsages.size()) - mPasses[passId].first_usage;

    return RGPassHandle{ passId };
  }

  bool isValid(RGResourceHandle handle) const {
    if (!handle.valid()) return false;
    return mResources[handle.id].version == handle.version;
  }

#ifdef RG_ENABLE_TESTS
  const std::vector<u32>& sortedPasses()   const { return mSortedPasses; }
  const std::vector<RGEdge>& edges()       const { return mEdges; }
  const std::vector<RGBarrier>& barriers() const { return mBarriers; }
#endif

  void compile() {
    // 1. Build mEdges from matching (resource_id, version) write -> read pairs.
    {
      mEdges.reserve(mUsages.size());
      std::unordered_map<u64, u32> writePass;   // key -> from_pass
      std::unordered_map<u64, bool> wasRead;
      writePass.reserve(mUsages.size());

      for (const auto& u : mUsages) {
        if (!u.is_write) continue;
        writePass[(u64)u.resource_id << 32 | u.version] = u.pass_id;
      }

      for (const auto& u : mUsages) {
        if (u.is_write) continue;
        const u64 key = (u64)u.resource_id << 32 | u.version;
        auto it = writePass.find(key);
        if (it != writePass.end()) {
          mEdges.push_back({ it->second, u.pass_id, u.resource_id, u.version });
          wasRead[key] = true;
        }
      }

      for (auto& [key, fromPass] : writePass) {
        if (!wasRead.count(key))
          mEdges.push_back({ fromPass, RG_INVALID_ID,
            static_cast<u32>(key >> 32), static_cast<u32>(key) });
      }
    }

    // 2. Topological sort - assign global_index to each pass, populate mSortedPasses
    // O(V*E) flat edge scan instead of O(V+E) adjacency list - at render graph scale
    // (~50 passes, ~200 edges) the difference is ~10k vs ~250 ops; not worth the allocations.
    {
      const u32 passCount = static_cast<u32>(mPasses.size());
      std::vector<u32> indegree(passCount, 0);

      for (const RGEdge& edge : mEdges) {
        if (edge.to_pass != RG_INVALID_ID)
          indegree[edge.to_pass]++;
      }

      std::vector<u32> queue;
      queue.reserve(passCount);
      mSortedPasses.reserve(passCount);

      for (u32 i = 0; i < passCount; i++) {
        if (indegree[i] == 0)
          queue.push_back(i);
      }

      for (size_t qi = 0; qi < queue.size(); qi++) {
        const u32 passId = queue[qi];
        mPasses[passId].global_index = static_cast<u32>(mSortedPasses.size());
        mSortedPasses.push_back(passId);

        for (const RGEdge& edge : mEdges) {
          if (edge.from_pass == passId && edge.to_pass != RG_INVALID_ID) {
            if (--indegree[edge.to_pass] == 0)
              queue.push_back(edge.to_pass);
          }
        }
      }
    }

    // 3. Dead-pass culling — must come before first_pass/last_pass so culled passes
    // don't pollute resource lifetimes and orphan GPU handles.
    // O(E + P)
    {
      for (const RGEdge& edge : mEdges) {
        if (edge.to_pass != RG_INVALID_ID)
          mPasses[edge.from_pass].ref_count++;
      }
      // Mark culled passes by resetting their global_index to RG_INVALID_ID.
      for (u32 passId : mSortedPasses) {
        if (mPasses[passId].ref_count == 0 && !(mPasses[passId].flags & RG_PASS_NEVER_CULL))
          mPasses[passId].global_index = RG_INVALID_ID;
      }
      mSortedPasses.erase(
        std::remove_if(mSortedPasses.begin(), mSortedPasses.end(),
          [&](u32 id) { return mPasses[id].global_index == RG_INVALID_ID; }),
        mSortedPasses.end()
      );
      // Re-assign global_index to reflect the post-culling order used by execute() and barriers.
      for (u32 i = 0; i < static_cast<u32>(mSortedPasses.size()); i++)
        mPasses[mSortedPasses[i]].global_index = i;
    }

    // 4. Fill first_pass / last_pass using post-culling global_index.
    // Culled passes have global_index == RG_INVALID_ID and are automatically skipped,
    // so resources used only by culled passes stay at RG_INVALID_ID and are skipped
    // by step 5 (no GPU handle created) and plan() (no placement computed).
    // O(U)
    for (size_t i = 0; i < mUsages.size(); i++) {
      const u32 gidx = mPasses[mUsages[i].pass_id].global_index;
      if (gidx == RG_INVALID_ID) continue;
      RGResourceData& res = mResources[mUsages[i].resource_id];
      if (res.first_pass == RG_INVALID_ID || gidx < res.first_pass) res.first_pass = gidx;
      if (res.last_pass  == RG_INVALID_ID || gidx > res.last_pass)  res.last_pass  = gidx;
    }

    // 5. Create unbound backend resources (no memory bound yet).
    // Skip resources never referenced by any live pass (first_pass == RG_INVALID_ID) —
    // allocating handles for them is wasteful and leaves physical_range in an undefined state.
    // Release any handle from a previous compile() call before acquiring a new one —
    // compile() may be called multiple times without reset() in between (e.g. late pass added).
    assert(mBackend);
    for (u32 i = 0; i < static_cast<u32>(mResources.size()); i++) {
      if (mResources[i].type       != RG_RESOURCE_TRANSIENT) continue;
      if (mResources[i].first_pass == RG_INVALID_ID)         continue;
      const u32 memType = static_cast<u32>(mResources[i].memory_type);
      auto& handler = mResourceHandlers[mResources[i].desc_index];
      if (mGPUHandles[i])
        handler.releaseTo(mPool, mGPUHandles[i], memType);
      mGPUHandles[i] = handler.acquireFrom(mPool, mBackend, memType);
    }
		
    // 6. Generate mBarriers from usage transitions
		{
			// O(U log U) — exclude usages from culled passes (global_index == RG_INVALID_ID):
			// they sort to the end and would emit barriers with src/dst_pass = UINT32_MAX,
			// corrupting dumpJSON output and crashing execute() when it indexes mSortedPasses.
			std::vector<u32> order;
			order.reserve(mUsages.size());
			for (u32 i = 0; i < static_cast<u32>(mUsages.size()); i++) {
				if (mPasses[mUsages[i].pass_id].global_index != RG_INVALID_ID)
					order.push_back(i);
			}
			std::sort(order.begin(), order.end(), [&](u32 a, u32 b) {
				if (mUsages[a].resource_id != mUsages[b].resource_id)
					return mUsages[a].resource_id < mUsages[b].resource_id;
				return mPasses[mUsages[a].pass_id].global_index <
							mPasses[mUsages[b].pass_id].global_index;
			});

			// O(U) linear walk over consecutive pairs
			for (size_t i = 0; i + 1 < order.size(); i++) {
				const RGResourceUsage& curr = mUsages[order[i]];
				const RGResourceUsage& next = mUsages[order[i + 1]];

				if (curr.resource_id != next.resource_id) continue;
				if (!curr.is_write && !next.is_write && curr.usage == next.usage) continue;

				mBarriers.push_back({
					curr.resource_id,
					curr.usage,
					next.usage,
					mPasses[curr.pass_id].global_index,
					mPasses[next.pass_id].global_index,
					RHI_BARRIER_TRANSITION
				});
			}
		} 
  }

  // TO DO #5, #23: offline placement pass - runs after compile().
  // 1. Compute planHash over (resource_id, size, alignment, first_pass, last_pass) using mGPUHandles
  //    via mBackend->getMemoryRequirements() - early-return if hash matches cached value
  // 2. Group transient resources by RHIMemoryType
  // 3. Per group: simulate lifetimes with internal free list, assign offsets, compute totalBytes
  void plan() {
    assert(mBackend);

    struct FreeRange { u64 offset; u64 size; };
		// FNV-1a hash algorithm constants
		constexpr u64 FNV_BASIS = 14695981039346656037ull;
    constexpr u64 FNV_PRIME = 1099511628211ull; 

    struct PlanKey { u32 resource_id, first_pass, last_pass; u64 size, alignment; };

    auto align_up = [](u64 value, u64 alignment) -> u64 {
      return (value + alignment - 1) & ~(alignment - 1);
    };

    std::vector<u64> planned_offsets(mResources.size(), 0);
    std::vector<u64> planned_sizes(mResources.size(), 0);

    // Run free-list simulation independently per RHIMemoryType
    for (u32 memType = 0; memType <= MAX_RHI_MEMORY_TYPE_INDEX; memType++) {
      std::vector<u32> sorted;
      for (u32 i = 0; i < static_cast<u32>(mResources.size()); i++) {
        if (mResources[i].type        != RG_RESOURCE_TRANSIENT)              continue;
        if (mResources[i].memory_type != static_cast<RHIMemoryType>(memType)) continue;
        if (mResources[i].first_pass  == RG_INVALID_ID)                      continue;
        sorted.push_back(i);
      }

      std::sort(sorted.begin(), sorted.end(), [&](u32 a, u32 b) {
        if (mResources[a].first_pass != mResources[b].first_pass)
          return mResources[a].first_pass < mResources[b].first_pass;
        return mResources[a].last_pass < mResources[b].last_pass;
      });

      std::vector<u32>       active;
      std::vector<FreeRange> free_list;
      u64                    pool_size = 0;
			// FNV-1a hash algorithm
			u64 hash = FNV_BASIS;
			auto feed = [&](const void* data, size_t len) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; i++)
          hash = (hash ^ p[i]) * FNV_PRIME;
      };

      for (u32 index : sorted) {
        // Expire resources whose lifetime ended before this one starts
        std::vector<u32> to_expire;
        for (u32 a : active) {
          if (mResources[a].last_pass < mResources[index].first_pass)
            to_expire.push_back(a);
        }
        for (u32 expired : to_expire) {
          free_list.push_back({ planned_offsets[expired], planned_sizes[expired] });
          active.erase(std::remove(active.begin(), active.end(), expired), active.end());
        }

        // Merge adjacent free ranges
        std::sort(free_list.begin(), free_list.end(), [](const FreeRange& a, const FreeRange& b) {
          return a.offset < b.offset;
        });
        for (size_t i = 0; i + 1 < free_list.size(); ) {
          if (free_list[i].offset + free_list[i].size == free_list[i + 1].offset) {
            free_list[i].size += free_list[i + 1].size;
            free_list.erase(free_list.begin() + i + 1);
          } else {
            i++;
          }
        }

        const RHIMemoryRequirements req = mBackend->getMemoryRequirements(mGPUHandles[index], mResources[index].kind);
        const u64 alignment = req.alignment;

        // Best-fit search - smallest range that fits after alignment padding
        FreeRange* best = nullptr;
        for (FreeRange& range : free_list) {
          const u64 aligned_start = align_up(range.offset, alignment);
          const u64 padding       = aligned_start - range.offset;
          if (range.size >= req.size + padding) {
            if (!best || range.size < best->size)
              best = &range;
          }
        }

        u64 offset;
        if (best) {
          const u64 aligned_start = align_up(best->offset, alignment);
          const u64 padding       = aligned_start - best->offset;
          offset       = aligned_start;
          best->offset = aligned_start + req.size;
          best->size  -= padding + req.size;
          if (best->size == 0)
            free_list.erase(std::remove_if(free_list.begin(), free_list.end(),
              [](const FreeRange& r) { return r.size == 0; }), free_list.end());
        } else {
          offset    = align_up(pool_size, alignment);
          pool_size = offset + req.size;
        }

        planned_offsets[index] = offset;
        planned_sizes[index]   = req.size;
        mResources[index].physical_range = { static_cast<u32>(memType), offset, req.size };
        active.push_back(index);

				const PlanKey key{ index, mResources[index].first_pass, mResources[index].last_pass, req.size, req.alignment };
				feed(&key, sizeof(key));
      }

			mPoolSizes[memType] = pool_size;

			if (mPlanHashes[memType] != hash) {
				mShouldReallocate[memType] = true;
				mPlanHashes[memType] = hash;
			} else {
				mShouldReallocate[memType] = false;
			}
    }
  }

  void allocate() {
    assert(mBackend);
    // TO DO #5: full implementation - see plan() above and issue #23 for the three-level cache.
    // 1. If planHash matches cached hash: return early
    // 2. Per RHIMemoryType where mShouldReallocate[memType] is true:
    //    - call mPool.flushMemoryType(memType) BEFORE rebinding - vkBindImageMemory is permanent,
    //      so any handle the pool cached from a previous frame is bound to a stale offset and
    //      cannot be reused. Flushing forces fresh creation via acquireFrom() on the next compile().
    //    - IGPUAllocator::free + reallocate with 1.5x slack (mMemoryPools)
    // 3. For each entry in plan: vkBindImageMemory / vkBindBufferMemory at planned offset
    // 4. Set physical_range on each RGResourceData via IGPUAllocator::suballocate()
    // 5. Generate mBarriers for aliasing - resources sharing the same pool_id and overlapping byte range
    //    (TransientResourcePool is not involved here - aliasing is a plan() decision, not a handle decision)
  }

  void execute(void* cmdBuf = nullptr);

  void dumpJSON(const char* path) const {
    FILE* f = fopen(path, "w");
    assert(f && "dumpJSON: failed to open file");

    auto id_or_null = [&](u32 val) {
      if (val == RG_INVALID_ID) fprintf(f, "null");
      else fprintf(f, "%u", val);
    };

    fprintf(f, "{\n");

    fprintf(f, "  \"passes\": [\n");
    for (u32 i = 0; i < static_cast<u32>(mPasses.size()); i++) {
      const RGPassData& p = mPasses[i];
      const bool culled = p.ref_count == 0 && !(p.flags & RG_PASS_NEVER_CULL);
      fprintf(f, "    {\"id\":%u,\"name\":\"%s\",\"queue\":%u,\"global_index\":", i, p.name, (u32)p.queue);
      id_or_null(p.global_index);
      fprintf(f, ",\"culled\":%s}%s\n", culled ? "true" : "false", i + 1 < mPasses.size() ? "," : "");
    }
    fprintf(f, "  ],\n");

    fprintf(f, "  \"resources\": [\n");
    for (u32 i = 0; i < static_cast<u32>(mResources.size()); i++) {
      const RGResourceData& r = mResources[i];
      fprintf(f, "    {\"id\":%u,\"name\":\"%s\",\"kind\":%u,\"memory_type\":%u,\"first_pass\":", i, r.name, (u32)r.kind, (u32)r.memory_type);
      id_or_null(r.first_pass);
      fprintf(f, ",\"last_pass\":");
      id_or_null(r.last_pass);
      fprintf(f, "}%s\n", i + 1 < mResources.size() ? "," : "");
    }
    fprintf(f, "  ],\n");

    fprintf(f, "  \"edges\": [\n");
    for (u32 i = 0; i < static_cast<u32>(mEdges.size()); i++) {
      const RGEdge& e = mEdges[i];
      fprintf(f, "    {\"from_pass\":%u,\"to_pass\":", e.from_pass);
      id_or_null(e.to_pass);
      fprintf(f, ",\"resource_id\":%u,\"version\":%u}%s\n", e.resource_id, e.version, i + 1 < mEdges.size() ? "," : "");
    }
    fprintf(f, "  ],\n");

    fprintf(f, "  \"barriers\": [\n");
    for (u32 i = 0; i < static_cast<u32>(mBarriers.size()); i++) {
      const RGBarrier& b = mBarriers[i];
      fprintf(f, "    {\"resource_id\":%u,\"src_pass\":%u,\"dst_pass\":%u,\"before_usage\":%u,\"after_usage\":%u,\"kind\":%u}%s\n",
        b.resource_id, b.src_pass, b.dst_pass, b.before_usage, b.after_usage, (u32)b.kind,
        i + 1 < mBarriers.size() ? "," : "");
    }
    fprintf(f, "  ]\n");

    fprintf(f, "}\n");
    fclose(f);
  }

  void reset() {
    if (mBackend) {
      for (u32 i = 0; i < static_cast<u32>(mResources.size()); i++) {
        if (mResources[i].type != RG_RESOURCE_TRANSIENT) continue;
        if (!mGPUHandles[i]) continue;
        mResourceHandlers[mResources[i].desc_index].releaseTo(mPool, mGPUHandles[i], static_cast<u32>(mResources[i].memory_type));
      }
    }
    mPasses.clear();
    mSortedPasses.clear();
    mResources.clear();
    mUsages.clear();
    mEdges.clear();
    mBarriers.clear();
    mResourceHandlers.clear();
    mExecutors.clear();
    mGPUHandles.clear();
  }

  void destroy() {
    reset();
    mPool.flush();
    if (mBackend) {
      for (auto& pool : mMemoryPools)
        mBackend->freePool(pool);
    }
    mMemoryPools.clear();
    mPoolSizes        = {};
    mPlanHashes       = {};
    mShouldReallocate = {};
    mBackend = nullptr;
  }

private:
  // Per-frame
  std::vector<RGPassData>                     mPasses;
  std::vector<u32>                            mSortedPasses;
  std::vector<RGResourceData>                 mResources;
  std::vector<RGResourceUsage>                mUsages;
  std::vector<RGEdge>                         mEdges;
  std::vector<RGBarrier>                      mBarriers;
  std::vector<RGResourceHandler>              mResourceHandlers;
  std::vector<std::unique_ptr<RGPassExecute>> mExecutors;
  std::vector<void*>                          mGPUHandles;  // parallel to mResources

  // Persistent (survive reset, freed in destroy)
  std::vector<GPUMemoryBlock> mMemoryPools;

  std::array<u64,  MAX_RHI_MEMORY_TYPE_INDEX + 1> mPoolSizes        = {};
  std::array<u64,  MAX_RHI_MEMORY_TYPE_INDEX + 1> mPlanHashes       = {};
  std::array<bool, MAX_RHI_MEMORY_TYPE_INDEX + 1> mShouldReallocate = {};

  IRHIBackend*          mBackend = nullptr;
  TransientResourcePool mPool;
};

// Read-only resource accessor passed into execute callbacks. Passes retrieve their concrete
// resource objects through this without touching graph internals directly.
class RGResources {
  friend class RenderGraph;
public:
  RGResources() = delete;
  RGResources(const RGResources&) = delete;
  RGResources(RGResources&&) noexcept = delete;
  ~RGResources() = default;

  RGResources& operator=(const RGResources&) = delete;
  RGResources& operator=(RGResources&&) noexcept = delete;

  template<VIRTUALIZABLE_RESOURCE(T)>
  T& get(RGResourceHandle handle) {
    return mRG.mResourceHandlers[mRG.mResources[handle.id].desc_index].get<T>();
  }

private:
  explicit RGResources(RenderGraph& rg) : mRG(rg) {}
  RenderGraph& mRG;
};

