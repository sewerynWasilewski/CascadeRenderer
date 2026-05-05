#pragma once
#include "RGData.h"

class RGResources;

// Type-erased execute callback. One instance per pass; stored in mExecutors and invoked during execute().
struct RGPassExecute {
  RGPassExecute() = default;
  RGPassExecute(const RGPassExecute&) = delete;
  RGPassExecute(RGPassExecute&&) noexcept = delete;
  virtual ~RGPassExecute() = default;

  RGPassExecute& operator=(const RGPassExecute&) = delete;
  RGPassExecute& operator=(RGPassExecute&&) noexcept = delete;

  virtual void execute(RGResources& resources, void* ctx) = 0;
};

// Concrete callback wrapper - stores the user's execute lambda and forwards it through the virtual dispatch.
template<typename Execute>
struct RGPassCallback final : RGPassExecute {
  explicit RGPassCallback(Execute&& exec) : mExecFunc(std::forward<Execute>(exec)) {}

  void execute(RGResources& resources, void* ctx) override {
    mExecFunc(resources, ctx);
  }

  Execute mExecFunc{};
};

// Barrier description passed to IRGBarrierEmitter. Backend translates this into API-specific pipeline barriers.
struct RG_BarrierInfo {
  u32            resource_id;
  u32            before_usage;
  u32            after_usage;
  RG_BarrierKind kind;
};

// Backend-provided barrier emitter. Injected at execute() time so the graph layer stays API-agnostic.
struct IRGBarrierEmitter {
  IRGBarrierEmitter() = default;
  IRGBarrierEmitter(const IRGBarrierEmitter&) = delete;
  IRGBarrierEmitter(IRGBarrierEmitter&&) noexcept = delete;
  virtual ~IRGBarrierEmitter() = default;

  IRGBarrierEmitter& operator=(const IRGBarrierEmitter&) = delete;
  IRGBarrierEmitter& operator=(IRGBarrierEmitter&&) noexcept = delete;

  virtual void emit(const RG_BarrierInfo& info, void* ctx) = 0;
};
