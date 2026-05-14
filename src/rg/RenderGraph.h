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
#include "../rhi/IRHIBackend.h"


// May it be that two passes write to same resource?

// Central render graph. Passes register resource reads/writes during setup; compile() resolves
// dependencies and generates barriers; allocate() assigns physical memory; execute() runs the passes.
class RenderGraph {
  friend class RGResources;
public:
  RenderGraph() = default;
  RenderGraph(const RenderGraph&) = delete;
  RenderGraph(RenderGraph&&) noexcept = delete;

  void setBackend(IRHIBackend* backend) { mBackend = backend; }

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
    RGResourceHandle write(RGResourceHandle handle, RGUsage usage) {
      assert(handle.valid());
      RGResourceData& res = mRG.mResources[handle.id];
      res.queue_mask |= rg_queue_bit(mRG.mPasses[mPassId].queue);
      res.version++;
      mRG.mUsages.push_back({ mPassId, handle.id, res.version, usage, true });
      return RGResourceHandle{ handle.id, res.version };
    }

    RGResourceHandle read(RGResourceHandle handle, RGUsage usage) {
      assert(handle.valid());
      RGResourceData& res = mRG.mResources[handle.id];
      res.queue_mask |= rg_queue_bit(mRG.mPasses[mPassId].queue);
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
  RGResourceHandle create(const char* name, RGResourceKind kind, RGMemoryType memoryType, const typename T::Desc& desc) {
    const u32 id = static_cast<u32>(mResources.size());

    RGResourceData res{};
    res.name           = name;
    res.kind           = kind;
    res.type           = RG_RESOURCE_TRANSIENT;
    res.memory_type    = memoryType;
    res.desc_index     = static_cast<u32>(mResourceHandlers.size());
    res.version        = 0;
    res.first_pass     = RG_INVALID_ID;
    res.last_pass      = RG_INVALID_ID;
    res.queue_mask     = 0;
    res.physical_range = RGMemoryRange{};
    mResources.push_back(res);
    mGPUHandles.push_back(nullptr);

    mResourceHandlers.push_back(
      RGResourceHandler(RG_RESOURCE_TRANSIENT, id, desc, T{})
    );

    return RGResourceHandle{ id, 0 };
  }

  template<VIRTUALIZABLE_RESOURCE(T)>
  RGResourceHandle import(const char* name, RGResourceKind kind, RGMemoryType memoryType, const typename T::Desc& desc) {
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

  void compile() {
    // 1. Build mEdges from matching (resource_id, version) write -> read pairs
    // O(U) - hash map keyed by (resource_id << 32 | version) gives O(1) write lookup per read.
    {
      mEdges.reserve(mUsages.size());
      std::unordered_map<u64, size_t> writeIndex;
      writeIndex.reserve(mUsages.size());

      for (size_t i = 0; i < mUsages.size(); i++) {
        const u64 key = (u64)mUsages[i].resource_id << 32 | mUsages[i].version;
        if (mUsages[i].is_write) {
          writeIndex[key] = mEdges.size();
          mEdges.push_back({ mUsages[i].pass_id, RG_INVALID_ID, mUsages[i].resource_id, mUsages[i].version });
        } else {
          auto it = writeIndex.find(key);
          if (it != writeIndex.end())
            mEdges[it->second].to_pass = mUsages[i].pass_id;
        }
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

    // 3. Fill first_pass / last_pass using global_index (must come after topo sort)
    // O(U)
    for (size_t i = 0; i < mUsages.size(); i++) {
      const u32 gidx = mPasses[mUsages[i].pass_id].global_index;
      if (gidx == RG_INVALID_ID) continue;
      RGResourceData& res = mResources[mUsages[i].resource_id];
      if (res.first_pass == RG_INVALID_ID || gidx < res.first_pass) res.first_pass = gidx;
      if (res.last_pass  == RG_INVALID_ID || gidx > res.last_pass)  res.last_pass  = gidx;
    }

    // 4. Ref-count based dead-pass culling
		// O(E)
		for (const RGEdge& edge : mEdges) {
			if (edge.to_pass != RG_INVALID_ID)
				mPasses[edge.from_pass].ref_count++;
		}

		// TO DO #1: dead-pass culling - remove passes with ref_count == 0 and no RG_PASS_NEVER_CULL from mSortedPasses

    // 5. Create unbound backend resources (no memory bound yet)
    // TO DO #23: skip recreation when desc hash matches cached value
    assert(mBackend);
    for (u32 i = 0; i < static_cast<u32>(mResources.size()); i++) {
      if (mResources[i].type != RG_RESOURCE_TRANSIENT) continue;
      mGPUHandles[i] = mResourceHandlers[mResources[i].desc_index].createGPUResource(mBackend);
    }
		
    // 6. Generate mBarriers from usage transitions
		{
			// O(U log U)
			std::vector<u32> order(mUsages.size());
			std::iota(order.begin(), order.end(), 0);
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
					RG_BARRIER_TRANSITION
				});
			}
		} 
  }

  // TO DO #5, #23: offline placement pass - runs after compile().
  // 1. Compute planHash over (resource_id, size, alignment, first_pass, last_pass) using mGPUHandles
  //    via mBackend->getMemoryRequirements() - early-return if hash matches cached value
  // 2. Group transient resources by RGMemoryType
  // 3. Per group: simulate lifetimes with internal free list, assign offsets, compute totalBytes
  void plan() {
    assert(mBackend);

    struct FreeRange { u64 offset; u64 size; };

    auto align_up = [](u64 value, u64 alignment) -> u64 {
      return (value + alignment - 1) & ~(alignment - 1);
    };

    std::vector<u64> planned_offsets(mResources.size(), 0);
    std::vector<u64> planned_sizes(mResources.size(), 0);

    // Run free-list simulation independently per RGMemoryType
    for (u32 memType = 0; memType < 3; memType++) {
      std::vector<u32> sorted;
      for (u32 i = 0; i < static_cast<u32>(mResources.size()); i++) {
        if (mResources[i].type        != RG_RESOURCE_TRANSIENT)              continue;
        if (mResources[i].memory_type != static_cast<RGMemoryType>(memType)) continue;
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
      }

      // TO DO #23: store pool_size per memType for allocate(), cache planHash
    }
  }

  void allocate() {
    assert(mBackend);
    // TO DO #5: full implementation - see plan() above and issue #23 for the three-level cache.
    // 1. If planHash matches cached hash: return early
    // 2. Per RGMemoryType: if totalBytes > pool capacity, IGPUAllocator::free + reallocate with 1.5x slack (mMemoryPools)
    // 3. For each entry in plan: vkBindImageMemory / vkBindBufferMemory at planned offset, store gpu handle in RGResourceHandler
    // 4. Set physical_range on each RGResourceData via IGPUAllocator::suballocate()
    // 5. Generate mBarriers for aliasing - resources sharing the same pool_id and overlapping byte range
  }

  void execute(void* cmdBuf = nullptr) {
    assert(mBackend);
    // TO DO #3:
    // 1. Iterate mSortedPasses in order
    // 2. For each pass emit mBarriers with dst_pass == global_index via mBackend->emitBarrier()
    // 3. mBackend->beginPass(cmdBuf)
    // 4. mExecutors[passId]->execute(resources, cmdBuf)
    // 5. mBackend->endPass(cmdBuf)
  }

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
      fprintf(f, "    {\"resource_id\":%u,\"src_pass\":%u,\"dst_pass\":%u,\"kind\":%u}%s\n",
        b.resource_id, b.src_pass, b.dst_pass, (u32)b.kind,
        i + 1 < mBarriers.size() ? "," : "");
    }
    fprintf(f, "  ]\n");

    fprintf(f, "}\n");
    fclose(f);
  }

	// persistent resources (#16 issue) -
	// they can't be in mResources if that gets cleared every frame. 
	// will likely want a separate mPersistentResources array that survives reset().
	void reset(){
    if (mBackend) {
      for (u32 i = 0; i < static_cast<u32>(mResources.size()); i++) {
        if (mGPUHandles[i])
          mResourceHandlers[mResources[i].desc_index].destroyGPUResource(mBackend, mGPUHandles[i]);
      }
    }
		mPasses.clear();
    mSortedPasses.clear();
    mResources.clear();
    mMemoryPools.clear();
    mUsages.clear();
    mEdges.clear();
    mBarriers.clear();
    mResourceHandlers.clear();
    mExecutors.clear();
    mGPUHandles.clear();
	}

private:
  std::vector<RGPassData>                     mPasses;
  std::vector<u32>                             mSortedPasses;
  std::vector<RGResourceData>                 mResources;
  std::vector<RGResourceUsage>                mUsages;
  std::vector<RGEdge>                         mEdges;
  std::vector<RGBarrier>                      mBarriers;
  std::vector<RGResourceHandler>               mResourceHandlers;
  std::vector<std::unique_ptr<RGPassExecute>>  mExecutors;
  std::vector<GPUMemoryBlock> mMemoryPools;
  std::vector<void*>          mGPUHandles;  // parallel to mResources
  IRHIBackend*                mBackend = nullptr;

  // FNV-1a hash algorithm
	u64 computePlanHash() const {
      constexpr u64 FNV_BASIS = 14695981039346656037ull;
      constexpr u64 FNV_PRIME = 1099511628211ull;

      u64 hash = FNV_BASIS;
      auto feed = [&](const void* data, size_t len) {
          const auto* p = static_cast<const uint8_t*>(data);
          for (size_t i = 0; i < len; i++)
              hash = (hash ^ p[i]) * FNV_PRIME;
      };

      for (const RGResourceData& res : mResources) {
          if (res.type != RG_RESOURCE_TRANSIENT) continue;
          const u32 res_id = static_cast<u32>(&res - mResources.data());
          feed(&res_id, sizeof(res_id));
          feed(&res.first_pass,  sizeof(res.first_pass));
          feed(&res.last_pass,   sizeof(res.last_pass));
          // size + alignment come from VkMemoryRequirements - feed those too once #23 lands
      }
      return hash;
  }
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
