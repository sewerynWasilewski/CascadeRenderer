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
