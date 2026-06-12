#pragma once
#include "rhi/IRHIBackend.h"

struct MockBackend final : IRHIBackend {
  void* createImage(const RHITextureDesc&)                       override { return reinterpret_cast<void*>(0xDEAD); }
  void* createBuffer(const RHIBufferDesc&)                       override { return reinterpret_cast<void*>(0xDEAD); }
  void  destroyImage(void*)                                      override {}
  void  destroyBuffer(void*)                                     override {}
  RHIMemoryRequirements getMemoryRequirements(void*, RGResourceKind) override { return {1024, 256}; }
  GPUMemoryBlock allocatePool(u64, RGMemoryType)                 override { return {}; }
  void           freePool(GPUMemoryBlock)                        override {}
  void           bindMemory(void*, GPUMemoryBlock, u64)          override {}
  void           emitBarrier(const RGBarrierInfo&, void*)        override {}
  void           beginPass(void*)                                override {}
  void           endPass(void*)                                  override {}
};

struct MockTexture {
  struct Desc { uint32_t width, height; };
  static void* createGPU(const Desc& d, IRHIBackend* b) { return b->createImage({d.width, d.height, 1, 1, 1}); }
  static void  destroyGPU(const Desc&, IRHIBackend* b, void* h) { b->destroyImage(h); }
};
