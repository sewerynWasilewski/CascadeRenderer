#pragma once
#include <cassert>
#include <memory>
#include "RGTypes.h"
#include "RGTypeTraits.h"
#include "TransientResourcePool.h"
#include "core/algorithm.h"

struct IRHIBackend;

class RGResourceHandler {
  friend class RenderGraph;
public:
  RGResourceHandler() = delete;
  ~RGResourceHandler() = default;
  RGResourceHandler(const RGResourceHandler&) = delete;
  RGResourceHandler(RGResourceHandler&&) noexcept = default;

  RGResourceHandler& operator=(const RGResourceHandler&) = delete;
  RGResourceHandler& operator=(RGResourceHandler&&) noexcept = default;

  bool isTransient() const { return mType == RG_RESOURCE_TRANSIENT; }
  bool isExternal()  const { return mType == RG_RESOURCE_EXTERNAL; }

  u64 typeId() { return mSlot->typeId; }

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
    u64 typeId   = 0;
    u64 descHash = 0;  // pool key: FNV-1a of T::Desc bytes
    virtual ~SlotBase() = default;
    virtual void* createGPUResource(IRHIBackend* backend) const = 0;
    virtual void  destroyGPUResource(IRHIBackend* backend, void* handle) const = 0;
    virtual void* acquireFrom(TransientResourcePool& pool, IRHIBackend* backend, u32 memType) const = 0;
    virtual void  releaseTo(TransientResourcePool& pool, void* handle, u32 memType) const = 0;
  };

  template<typename T>
  struct Slot final : SlotBase {
    Slot(const typename T::Desc& d, T&& r)
      : desc(d), resource(std::move(r)) {
        typeId   = typeIdOf<T>();
        descHash = fnv1a(&d, sizeof(d));
      }

    void* createGPUResource(IRHIBackend* backend) const override {
      return T::createGPU(desc, backend);
    }
    void destroyGPUResource(IRHIBackend* backend, void* handle) const override {
      T::destroyGPU(desc, backend, handle);
    }
    void* acquireFrom(TransientResourcePool& pool, IRHIBackend* backend, u32 memType) const override {
      if (void* h = pool.tryAcquire(typeId, descHash, memType)) return h;
      return T::createGPU(desc, backend);
    }
    void releaseTo(TransientResourcePool& pool, void* handle, u32 memType) const override {
      pool.release(typeId, descHash, memType, handle,
        [d = desc](IRHIBackend* be, void* h) { T::destroyGPU(d, be, h); });
    }

    typename T::Desc desc;
    T                resource;
  };

  void* createGPUResource(IRHIBackend* backend)                { return mSlot->createGPUResource(backend); }
  void  destroyGPUResource(IRHIBackend* backend, void* handle) { mSlot->destroyGPUResource(backend, handle); }
  void* acquireFrom(TransientResourcePool& pool, IRHIBackend* backend, u32 memType) const { return mSlot->acquireFrom(pool, backend, memType); }
  void  releaseTo(TransientResourcePool& pool, void* handle, u32 memType)           const { mSlot->releaseTo(pool, handle, memType); }

  template<typename T>
  RGResourceHandler(RGResourceType type, u32 id, const typename T::Desc& desc, T&& resource)
    : mType(type), mId(id),
      mSlot(std::make_unique<Slot<T>>(desc, std::forward<T>(resource))) {}

  RGResourceType            mType;
  u32                       mId;
  std::unique_ptr<SlotBase> mSlot;
};
