#pragma once
#include <unordered_map>
#include <vector>
#include <functional>
#include <cassert>
#include "../gpu_types.h"

struct IRHIBackend;

// Freelist pool for transient GPU resources. Sits below the render graph — the graph
// acquires handles at compile() and returns them at reset(). Handles are reused across
// frames as long as their type and descriptor match.
//
// The graph rebuilds every frame; this pool is what makes that cheap.
class TransientResourcePool {
public:
  void setBackend(IRHIBackend* backend) { mBackend = backend; }

  // Returns a cached handle matching (typeId, descHash), or nullptr if the freelist is empty.
  void* tryAcquire(u64 typeId, u64 descHash) {
    auto it = mFreeList.find({typeId, descHash});
    if (it == mFreeList.end() || it->second.empty()) return nullptr;
    void* h = it->second.back().handle;
    it->second.pop_back();
    return h;
  }

  // Returns a handle to the freelist. destroyFn is stored so flush() can clean up.
  void release(u64 typeId, u64 descHash, void* handle,
               std::function<void(IRHIBackend*, void*)> destroyFn) {
    mFreeList[{typeId, descHash}].push_back({handle, std::move(destroyFn)});
  }

  // Destroys all free handles. Call on shutdown or when memory pressure requires eviction.
  void flush() {
    assert(mBackend);
    for (auto& [key, entries] : mFreeList)
      for (auto& e : entries)
        e.destroyFn(mBackend, e.handle);
    mFreeList.clear();
  }

private:
  struct PoolKey {
    u64 typeId;
    u64 descHash;
    bool operator==(const PoolKey& o) const { return typeId == o.typeId && descHash == o.descHash; }
  };
  struct PoolKeyHash {
    size_t operator()(const PoolKey& k) const {
      // Fibonacci hashing to mix two 64-bit values
      return k.typeId ^ (k.descHash * 11400714819323198485ull);
    }
  };

  struct Entry {
    void* handle;
    std::function<void(IRHIBackend*, void*)> destroyFn;
  };

  std::unordered_map<PoolKey, std::vector<Entry>, PoolKeyHash> mFreeList;
  IRHIBackend* mBackend = nullptr;
};
