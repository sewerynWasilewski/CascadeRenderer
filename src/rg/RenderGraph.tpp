#pragma once
// Template implementations of RenderGraph and RGResources.
// 

template<VIRTUALIZABLE_RESOURCE(T)>
RGResourceHandle RenderGraph::create(const char* name, RHIResourceKind kind, RHIMemoryType memoryType, const typename T::Desc& desc) {
  
  const u32 id = registerResource(name, kind, memoryType, RG_RESOURCE_TRANSIENT, desc, T{}); 

  mGPUHandles.push_back(nullptr);
  mLastUsages.push_back(RHI_USAGE_NONE);
  return RGResourceHandle{ id, 0, mEpoch };
}

template<VIRTUALIZABLE_RESOURCE(T)>
RGResourceHandle RenderGraph::import(const char* name, RHIResourceKind kind, RHIMemoryType memoryType, const typename T::Desc& desc, T&& resource, void* gpuHandle) {
  assert(gpuHandle != nullptr && "import: gpuHandle must not be null");
  const u32 id = registerResource(name, kind, memoryType, RG_RESOURCE_EXTERNAL, desc, resource);

  mGPUHandles.push_back(gpuHandle);
  mLastUsages.push_back(RHI_USAGE_NONE);  // caller is responsible for the real initial state
  return RGResourceHandle{ id, 0, mEpoch };
}

template<typename Setup, typename Execute>
RGPassHandle RenderGraph::addPass(const char* name, RGPassFlags flags, Setup&& setup, Execute&& exec) {
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

template<VIRTUALIZABLE_RESOURCE(T)>
inline u32 RenderGraph::registerResource(const char* name, RHIResourceKind kind, RHIMemoryType memoryType, RGResourceType resType, const typename T::Desc& desc, T&& resource) { 
  const u32 id = static_cast<u32>(mResources.size());
  mResourceHandlers.push_back(RGResourceHandler(resType, id, desc, std::forward<T>(resource)));

  RGResourceData res{};
  res.name           = name;
  res.kind           = kind;
  res.type           = resType;
  res.memory_type    = memoryType;
  res.desc_index     = id;
  res.version        = 0;
  res.first_pass     = RG_INVALID_ID;
  res.last_pass      = RG_INVALID_ID;
  res.queue_mask     = 0;
  res.physical_range = RGMemoryRange{};
  mResources.push_back(res);

  return id; 
}

template<VIRTUALIZABLE_RESOURCE(T)>
T& RGResources::get(RGResourceHandle handle) {
  assert(handle.valid()                                             && "invalid handle");
  assert(handle.epoch == mRG.mEpoch                                && "stale handle: used across reset()");
  assert(handle.id < mRG.mResources.size()                         && "stale handle: id out of range");
  assert(mRG.mResources[handle.id].first_pass != RG_INVALID_ID     && "resource was culled this frame");
  return mRG.mResourceHandlers[mRG.mResources[handle.id].desc_index].get<T>();
}