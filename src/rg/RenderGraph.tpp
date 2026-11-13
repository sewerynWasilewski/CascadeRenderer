#pragma once
// Template implementations of RenderGraph and RGResources.

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
