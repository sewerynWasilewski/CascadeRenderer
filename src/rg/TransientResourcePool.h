#pragma once
#include <unordered_map>
#include <vector>
#include <functional>
#include <cassert>
#include "core/types.h"
#include "rhi/rhi_types.h"
#include "rhi/IRHIBackend.h"

struct AcquiredHandle {
  void*    handle;
  RHIUsage last_usage;
};

// Freelist pool for transient GPU resources. Sits below the render graph — the graph
// acquires handles at compile() and returns them at reset(). Handles are reused across
// frames as long as their type, descriptor, and memory type all match.
class TransientResourcePool {
public:
  void setBackend(IRHIBackend* backend) { mBackend = backend; }

  AcquiredHandle tryAcquire(RHIResourceKind kind, u64 descHash, u32 memType) {
    auto it = mFreeList.find({kind, descHash, memType});
    if (it == mFreeList.end() || it->second.empty()) return {nullptr, RHI_USAGE_NONE};
    AcquiredHandle result = {it->second.back().handle, it->second.back().last_usage};
    it->second.pop_back();
    return result;
  }

  void release(RHIResourceKind kind, u64 descHash, u32 memType, void* handle, RHIUsage last_usage) {
    mFreeList[{kind, descHash, memType}].push_back({handle, last_usage});
  }

  // Destroys all free handles for one memory type.
  // Must be called before rebinding memory — vkBindImageMemory/vkBindBufferMemory can
  // only be called once per Vulkan object, so a pooled handle from a previous frame
  // carries a stale memory binding and cannot be reused at a new offset.
  void flushMemoryType(u32 memType) {
    assert(mBackend);
    for (auto it = mFreeList.begin(); it != mFreeList.end(); ) {
      if (it->first.memType == memType) {
        switch(it->first.kind) { 
          case RHIResourceKind::RHI_RESOURCE_TEXTURE: 
            for (auto& e : it->second)
              mBackend->destroyImage(e.handle);
            break; 
          case RHIResourceKind::RHI_RESOURCE_BUFFER:
            for (auto& e : it->second)
              mBackend->destroyBuffer(e.handle);
            break; 
        }

        it = mFreeList.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Destroys all free handles. Call on shutdown or when memory pressure requires eviction.
  void flush() {
    assert(mBackend);
    for (auto& [key, entries] : mFreeList){
      switch(key.kind) { 
        case RHIResourceKind::RHI_RESOURCE_TEXTURE: 
          for (auto& e : entries)
            mBackend->destroyImage(e.handle);
          break; 
        case RHIResourceKind::RHI_RESOURCE_BUFFER:
          for (auto& e : entries)
            mBackend->destroyBuffer(e.handle);
          break; 
      }
    }

    mFreeList.clear();
  }

private:
  struct PoolKey {
    RHIResourceKind kind;
    u64 descHash;
    u32 memType;
    bool operator==(const PoolKey& o) const {
      return kind == o.kind && descHash == o.descHash && memType == o.memType;
    }
  };
  struct PoolKeyHash {
    size_t operator()(const PoolKey& k) const {
      return k.kind ^ (k.descHash * 11400714819323198485ull) ^ ((u64)k.memType * 2654435761ull);
    }
  };

  struct Entry {
    void*    handle;
    RHIUsage last_usage;
  };

  std::unordered_map<PoolKey, std::vector<Entry>, PoolKeyHash> mFreeList;
  IRHIBackend* mBackend = nullptr;
};
