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
  void           createSwapChain(void*, u32, u32)                   override {}
  void           beginFrame()                                      override {}
  void           endFrame()                                        override {}
  void*          getCurrentCommandBuffer()                    const override { return nullptr; }
  void*          getCurrentSwapchainTarget()                  const override { return nullptr; }
  void           getSwapchainExtent(u32& w, u32& h)           const override { w = 0; h = 0; }
  const void*    getNativeHandles()                           const override { return nullptr; }
};
