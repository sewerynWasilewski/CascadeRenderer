#pragma once
#include <vector>
#include <memory>
#include <cassert>
#include <functional>
#include <unordered_map>

#include "RGTypes.h"
#include "RGData.h"
#include "RGResourceHandler.h"
#include "RGPass.h"
#include "RGTypeTraits.h"


// May it be that two passes write to same resource?

// Central render graph. Passes register resource reads/writes during setup; compile() resolves
// dependencies and generates barriers; allocate() assigns physical memory; execute() runs the passes.
class RenderGraph {
  friend class RGResources;
public:
  RenderGraph() = default;
  RenderGraph(const RenderGraph&) = delete;
  RenderGraph(RenderGraph&&) noexcept = delete;

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
      mRG.mUsages.push_back({ mPassId, handle.id, handle.version, usage, true });
      res.version++;
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
  RGResourceHandle create(const char* name, RGResourceKind kind, const typename T::Desc& desc) {
    const u32 id = static_cast<u32>(mResources.size());

    RGResourceData res{};
    res.name        = name;
    res.kind        = kind;
    res.type        = RG_RESOURCE_TRANSIENT;
    res.desc_index  = static_cast<u32>(mResourceHandlers.size());
    res.version     = 0;
    res.first_pass  = RG_INVALID_ID;
    res.last_pass   = RG_INVALID_ID;
    res.queue_mask  = 0;
    res.physical_id = RG_INVALID_ID;
    mResources.push_back(res);

    mResourceHandlers.push_back(
      RGResourceHandler(RGResourceHandler::Ownership::Transient, id, desc, T{})
    );

    return RGResourceHandle{ id, 0 };
  }

  template<VIRTUALIZABLE_RESOURCE(T)>
  RGResourceHandle import(const char* name, RGResourceKind kind, const typename T::Desc& desc) {
    // TO DO: external resource import
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

		// TO DO #1: dead-pass culling — remove passes with ref_count == 0 and no RG_PASS_NEVER_CULL from mSortedPasses

    // 5. Create unbound backend resources and query memory requirements
    // TO DO #23: for each transient resource call vkCreateImage / vkCreateBuffer (unbound),
    // then vkGetImageMemoryRequirements / vkGetBufferMemoryRequirements to get size and alignment.
    // Store results so plan() has real sizes. Cache unbound resources by desc hash — if hash matches
    // next frame, skip recreation entirely.

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

  // TO DO #5, #23: plan() goes here — offline placement pass that runs after compile().
  // Simulates resource lifetimes in global_index order using FreeListSubAllocator to assign
  // byte offsets, computes totalBytes, and caches the result by planHash so allocate() can
  // skip work on frames where nothing changed.

  void allocate() {
    // TO DO #5: full implementation — see plan() above and issue #23 for the three-level cache.
    // 1. If planHash matches cached hash: return early
    // 2. If totalBytes > block capacity: IGPUAllocator::free + reallocate with 1.5x slack
    // 3. Bind each resource to its planned offset via vkBindImageMemory / vkBindBufferMemory
    // 4. Populate mPhysicalResources, set physical_id on each RGResourceData
    // 5. Generate mBarriers for aliasing
		{
			std::unordered_map<u32, std::vector<u32>> byPhysical;
			for (size_t i = 0; i < mResources.size(); i++) {
				if (mResources[i].physical_id != RG_INVALID_ID)
					byPhysical[mResources[i].physical_id].push_back(static_cast<u32>(i));
			}

			for (auto& [pid, resIds] : byPhysical) {
				if (resIds.size() < 2) continue;
				std::sort(resIds.begin(), resIds.end(), [&](u32 a, u32 b) {
					return mResources[a].first_pass < mResources[b].first_pass;
				});

				for (size_t i = 0; i + 1 < resIds.size(); i++) {
					const RGResourceData& a = mResources[resIds[i]];
					const RGResourceData& b = mResources[resIds[i + 1]];

					RGUsage lastUsageA  = RG_USAGE_NONE;
					RGUsage firstUsageB = RG_USAGE_NONE;
					for (const RGResourceUsage& u : mUsages) {
						if (u.resource_id == resIds[i]   && mPasses[u.pass_id].global_index == a.last_pass)
							lastUsageA  = u.usage;
						if (u.resource_id == resIds[i+1] && mPasses[u.pass_id].global_index == b.first_pass)
							firstUsageB = u.usage;
					}

					mBarriers.push_back({
						resIds[i + 1], lastUsageA, firstUsageB,
						a.last_pass, b.first_pass,
						RG_BARRIER_ALIASING
					});
				}
			}
		}

  }

  void execute(void* ctx = nullptr) {
    // TO DO:
    // 1. Iterate passes in global_index order
    // 2. Emit mBarriers scheduled before each pass
    // 3. Invoke mExecutors[pass_id]->execute(resources, ctx)
	}

	// persistent resources (#16 issue) -
	// they can't be in mResources if that gets cleared every frame. 
	// will likely want a separate mPersistentResources array that survives reset().
	void reset(){
		mPasses.clear();
    mSortedPasses.clear();
    mResources.clear();
    mPhysicalResources.clear();
    mUsages.clear();
    mEdges.clear();
    mBarriers.clear();
    mResourceHandlers.clear();
    mExecutors.clear();
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
  std::vector<RGSubAllocation>                mPhysicalResources;

  bool doPhysicalResourcesOverlap(const RGSubAllocation& a, const RGSubAllocation& b) {
    if (a.pool_id != b.pool_id) return false;
    if (a.offset + a.size <= b.offset || b.offset + b.size <= a.offset) return false;
    return true;
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
