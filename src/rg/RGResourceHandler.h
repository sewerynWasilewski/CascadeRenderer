#pragma once
#include <cassert>
#include <memory>
#include "RGTypes.h"
#include "RGTypeTraits.h"

struct IRHIBackend;

class RGResourceHandler {
  friend class RenderGraph;
public:
  RGResourceHandler() = delete;
  ~RGResourceHandler() = default;
  RGResourceHandler(const RGResourceHandler&) = delete;
  RGResourceHandler(RGResourceHandler&&) noexcept = default;

  RGResourceHandler& operator=(const RGResourceHandler&) = delete;
  RGResourceHandler& operator=(RGResourceHandler&&) noexcept = delete;

  bool isTransient() const { return mType == RG_RESOURCE_TRANSIENT; }
  bool isExternal()  const { return mType == RG_RESOURCE_EXTERNAL; }

  template<typename T>
  T& get() {
    assert(mSlot->typeId == typeIdOf<T>() && "Wrong resource type requested");
    return static_cast<Slot<T>*>(mSlot.get())->resource;
  }

private:
  // Address of a per-type static local is unique without RTTI.
  template<typename T>
  static u64 typeIdOf() {
    static const char tag = 0;
    return reinterpret_cast<u64>(&tag);
  }

  struct SlotBase {
    u64 typeId = 0;
    virtual ~SlotBase() = default;
    virtual void* createGPUResource(IRHIBackend* backend) const = 0;
    virtual void  destroyGPUResource(IRHIBackend* backend, void* handle) const = 0;
  };

  template<typename T>
  struct Slot final : SlotBase {
    Slot(const typename T::Desc& d, T&& r)
      : desc(d), resource(std::move(r)) { typeId = typeIdOf<T>(); }

    void* createGPUResource(IRHIBackend* backend) const override {
      return T::createGPU(desc, backend);
    }
    void destroyGPUResource(IRHIBackend* backend, void* handle) const override {
      T::destroyGPU(desc, backend, handle);
    }

    typename T::Desc desc;
    T                resource;
  };

  void* createGPUResource(IRHIBackend* backend)                { return mSlot->createGPUResource(backend); }
  void  destroyGPUResource(IRHIBackend* backend, void* handle) { mSlot->destroyGPUResource(backend, handle); }

  template<typename T>
  RGResourceHandler(RGResourceType type, u32 id, const typename T::Desc& desc, T&& resource)
    : mType(type), mId(id),
      mSlot(std::make_unique<Slot<T>>(desc, std::forward<T>(resource))) {}

  RGResourceType            mType;
  const u32                 mId;
  std::unique_ptr<SlotBase> mSlot;
};
