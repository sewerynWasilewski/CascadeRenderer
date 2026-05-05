#pragma once
#include <cassert>
#include <memory>
#include "RGTypes.h"
#include "RGTypeTraits.h"

// Owns one logical resource's virtual dispatch table. Wraps any Virtualizable T behind IResource so the
// graph can call create/destroy/preRead/preWrite without knowing the concrete backend type.
class RGResourceHandler {
  friend class RenderGraph;

public:
  enum class Ownership : u32 {
    Transient = 0,
    Imported  = 1,
  };

  RGResourceHandler() = delete;
  ~RGResourceHandler() = default;
  RGResourceHandler(const RGResourceHandler&) = delete;
  RGResourceHandler(RGResourceHandler&&) noexcept = default;

  RGResourceHandler& operator=(const RGResourceHandler&) = delete;
  RGResourceHandler& operator=(RGResourceHandler&&) noexcept = delete;

  void create(void* allocator)  { assert(isTransient()); mResource->create(allocator); }
  void destroy(void* allocator) { assert(isTransient()); mResource->destroy(allocator); }
  void preRead(void* context)   { mResource->preRead(context); }
  void preWrite(void* context)  { mResource->preWrite(context); }

  bool isTransient() const { return mOwnership == Ownership::Transient; }
  bool isImported()  const { return mOwnership == Ownership::Imported; }

  template<typename T>
  T& get() {
    assert(mResource->typeId == typeIdOf<T>() && "Wrong resource type requested");
    return static_cast<ResourceModel<T>*>(mResource.get())->mResource;
  }

private:
  // Each instantiation has its own static tag - its address is a unique per-type ID without RTTI.
  template<typename T>
  static u64 typeIdOf() {
    static const char tag = 0;
    return reinterpret_cast<u64>(&tag);
  }

  // Non-template base so the handler can store heterogeneous resources without knowing T.
  struct IResource {
    u64 typeId = 0;  // set by ResourceModel<T> at construction
    virtual void create(void* allocator) = 0;
    virtual void destroy(void* allocator) = 0;
    virtual void preRead(void* context)  = 0;
    virtual void preWrite(void* context) = 0;
    virtual ~IResource() = default;
  };

  // Concrete model - holds the descriptor and the resource together, forwards calls to T's interface.
  template<typename T>
  struct ResourceModel final : IResource {
    ResourceModel(const typename T::Desc& desc, T&& resource)
      : mDescriptor(desc), mResource(std::move(resource)) { IResource::typeId = RGResourceHandler::typeIdOf<T>(); }

    void create(void* allocator) override  { mResource.create(mDescriptor, allocator); }
    void destroy(void* allocator) override { mResource.destroy(mDescriptor, allocator); }
    void preRead(void* context) override   { if constexpr (has_preRead<T>)  mResource.preRead(mDescriptor, context); }
    void preWrite(void* context) override  { if constexpr (has_preWrite<T>) mResource.preWrite(mDescriptor, context); }

    const typename T::Desc mDescriptor;
    T                      mResource;
  };

  template<typename T>
  RGResourceHandler(Ownership ownership, u32 id, const typename T::Desc& desc, T&& resource)
    : mOwnership(ownership), mId(id),
      mResource(std::make_unique<ResourceModel<T>>(desc, std::forward<T>(resource))) {}

  Ownership                  mOwnership;
  const u32                  mId;
  std::unique_ptr<IResource> mResource;
};
