#pragma once
#include "rhi/IRHIBackend.h"

struct MockBackend : IRHIBackend {
  void* createImage(const RHITextureDesc&)                         override { return reinterpret_cast<void*>(0xDEAD); }
  void* createBuffer(const RHIBufferDesc&)                         override { return reinterpret_cast<void*>(0xDEAD); }
  void  destroyImage(void*)                                        override {}
  void  destroyBuffer(void*)                                       override {}
  RHIMemoryRequirements getMemoryRequirements(void*, RHIResourceKind) override { return {1024, 256}; }
  RHIMemoryBlock allocatePool(u64, RHIMemoryType)                  override { return {}; }
  void           freePool(RHIMemoryBlock)                          override {}
  void           bindMemory(void*, RHIMemoryBlock, u64)            override {}
  void           emitBarrier(const RHIBarrierInfo&, void*)         override {}
  void           beginPass(void*, RHIPassType)                     override {}
  void           endPass(void*)                                    override {}
};

struct MockTexture {
  struct Desc { uint32_t width, height; };
  static void* createRHIHandle(const Desc& d, IRHIBackend* b) { return b->createImage({d.width, d.height, 1, 1, 1}); }
  static void  destroyRHIHandle(const Desc&, IRHIBackend* b, void* h) { b->destroyImage(h); }
};
