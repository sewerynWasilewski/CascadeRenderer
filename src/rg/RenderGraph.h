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

class RenderGraph {
  friend class RGResources;
public:
  RenderGraph() = default;
  ~RenderGraph() { if (mBackend) destroy(); }
  RenderGraph(const RenderGraph&) = delete;
  RenderGraph(RenderGraph&&) noexcept = delete;

  void setBackend(IRHIBackend* backend) { mBackend = backend; mResourcePool.setBackend(backend); }

  RenderGraph& operator=(const RenderGraph&) = delete;
  RenderGraph& operator=(RenderGraph&&) noexcept = delete;

  class PassBuilder {
    friend class RenderGraph;
  public:
    PassBuilder() = delete;
    PassBuilder(const PassBuilder&) = delete;
    PassBuilder(PassBuilder&&) noexcept = delete;

    PassBuilder& operator=(const PassBuilder&) = delete;
    PassBuilder& operator=(PassBuilder&&) noexcept = delete;

    RGResourceHandle write(RGResourceHandle handle, RHIUsage usage); 
    RGResourceHandle read(RGResourceHandle handle, RHIUsage usage); 

  private:
    PassBuilder(RenderGraph& rg, u32 passId)
      : mRG(rg), mPassId(passId) {}

    RenderGraph& mRG;
    u32          mPassId;
  };

  template<VIRTUALIZABLE_RESOURCE(T)>
  RGResourceHandle create(const char* name, RHIResourceKind kind, RHIMemoryType memoryType, const typename T::Desc& desc); 
  
  template<VIRTUALIZABLE_RESOURCE(T)>
  RGResourceHandle import(const char* name, RHIResourceKind kind, RHIMemoryType memoryType, const typename T::Desc& desc, T&& resource, void* gpuHandle); 

  template<typename Setup, typename Execute>
  RGPassHandle addPass(const char* name, RGPassFlags flags, Setup&& setup, Execute&& exec); 
  
  bool isValid(RGResourceHandle handle) const; 

#ifdef RG_ENABLE_TESTS
  const std::vector<u32>& sortedPasses()   const { return mSortedPasses; }
  const std::vector<RGEdge>& edges()       const { return mEdges; }
  const std::vector<RGBarrier>& barriers() const { return mBarriers; }
#endif

  void compile(); 

  // TO DO #5, #23: offline placement pass - runs after compile().
  // 1. Compute planHash over (resource_id, size, alignment, first_pass, last_pass) using mGPUHandles
  //    via mBackend->getMemoryRequirements() - early-return if hash matches cached value
  // 2. Group transient resources by RHIMemoryType
  // 3. Per group: simulate lifetimes with internal free list, assign offsets, compute totalBytes
  void plan();
  void allocate(); 
  void execute(void* cmdBuf = nullptr);
  void dumpJSON(const char* path) const; 
  void reset(); 
  void destroy(); 

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
  std::vector<RHIUsage>                       mLastUsages;  // last GPU state per handle
  bool                                        mCompiled = false;
  u32                                         mEpoch    = 0; // detects cross-frame handle reuse

  // Persistent (survive reset, freed in destroy)
  std::array<RHIMemoryBlock, MAX_RHI_MEMORY_TYPE_INDEX + 1> mMemoryPools = {};

  std::array<u64,  MAX_RHI_MEMORY_TYPE_INDEX + 1> mPlannedPoolSizes        = {};
  std::array<u64,  MAX_RHI_MEMORY_TYPE_INDEX + 1> mPlanHashes       = {};
  std::array<bool, MAX_RHI_MEMORY_TYPE_INDEX + 1> mShouldReallocate = {};

  IRHIBackend*          mBackend = nullptr;
  TransientResourcePool mResourcePool;

  template<VIRTUALIZABLE_RESOURCE(T)>
  inline u32 registerResource(const char* name, RHIResourceKind kind, RHIMemoryType memoryType, RGResourceType resType, const typename T::Desc& desc, T&& resource); 
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
  T& get(RGResourceHandle handle);

private:
  explicit RGResources(RenderGraph& rg) : mRG(rg) {}
  RenderGraph& mRG;
};


#include "RenderGraph.tpp"