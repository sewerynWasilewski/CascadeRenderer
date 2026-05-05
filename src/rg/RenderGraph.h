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
    RGResourceHandle write(RGResourceHandle handle, RG_Usage usage) {
      assert(handle.valid());
      RG_ResourceData& res = mRG.mResources[handle.id];
      res.queue_mask |= rg_queue_bit(mRG.mPasses[mPassId].queue);
      mRG.mUsages.push_back({ mPassId, handle.id, handle.version, usage, true });
      res.version++;
      return RGResourceHandle{ handle.id, res.version };
    }

    RGResourceHandle read(RGResourceHandle handle, RG_Usage usage) {
      assert(handle.valid());
      RG_ResourceData& res = mRG.mResources[handle.id];
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
  RGResourceHandle create(const char* name, RG_ResourceKind kind, const typename T::Desc& desc) {
    const u32 id = static_cast<u32>(mResources.size());

    RG_ResourceData res{};
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
  RGResourceHandle import(const char* name, RG_ResourceKind kind, const typename T::Desc& desc) {
    // TO DO: external resource import
    return RGResourceHandle{};
  }

  template<typename Setup, typename Execute>
  RGPassHandle addPass(const char* name, RG_PassFlags flags, Setup&& setup, Execute&& exec) {
    static_assert(std::is_invocable_v<Setup, PassBuilder&>,
      "Invalid setup callback");
    static_assert(std::is_invocable_v<Execute, RGResources&, void*>,
      "Invalid exec callback");

    const u32 passId = static_cast<u32>(mPasses.size());

    RG_PassData pass{};
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

      for (const RG_Edge& edge : mEdges) {
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

        for (const RG_Edge& edge : mEdges) {
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
      RG_ResourceData& res = mResources[mUsages[i].resource_id];
      if (res.first_pass == RG_INVALID_ID || gidx < res.first_pass) res.first_pass = gidx;
      if (res.last_pass  == RG_INVALID_ID || gidx > res.last_pass)  res.last_pass  = gidx;
    }

    // 4. Ref-count based dead-pass culling
		// O(E)
		for (const RG_Edge& edge : mEdges) {
			if (edge.to_pass != RG_INVALID_ID)
				mPasses[edge.from_pass].ref_count++;
		}

    // 5. Generate mBarriers from usage transitions
    // TO DO
  }

	// TO DO when allocate is invoked? 
  void allocate() {
    // TO DO:
    // 1. Interval graph coloring on transient resources using first_pass / last_pass
    // 2. Assign physical_id - aliased resources share the same id
    // 3. Call backend to create memory pool and bind resources at computed offsets
  }

  void execute(void* ctx = nullptr) {
    // TO DO:
    // 1. Iterate passes in global_index order
    // 2. Emit mBarriers scheduled before each pass
    // 3. Invoke mExecutors[pass_id]->execute(resources, ctx)
  }

private:
  std::vector<RG_PassData>                     mPasses;
  std::vector<u32>                             mSortedPasses;
  std::vector<RG_ResourceData>                 mResources;
  std::vector<RG_ResourceUsage>                mUsages;
  std::vector<RG_Edge>                         mEdges;
  std::vector<RG_Barrier>                      mBarriers;
  std::vector<RGResourceHandler>               mResourceHandlers;
  std::vector<std::unique_ptr<RGPassExecute>>  mExecutors;
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
