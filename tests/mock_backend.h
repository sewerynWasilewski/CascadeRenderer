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
  void*          map(RHIMemoryBlock)                               override { return nullptr; }
  void           unmap(RHIMemoryBlock)                             override {}
  void           bindMemory(void*, RHIResourceKind, RHIMemoryBlock, u64) override {}
  void           emitBarrier(const RHIBarrierInfo&, void*)         override {}
  void           beginPass(void*, RHIPassType, void* const*, u32, void*) override {}
  void           endPass(void*)                                    override {}
  void*          createPipeline(const RHIPipelineDesc&)            override { return reinterpret_cast<void*>(0xDEAD); }
  void           destroyPipeline(void*)                            override {}
  void           bindPipeline(void*, void*)                        override {}
  void           bindVertexBuffer(void*, void*, u64)                override {}
  void           bindIndexBuffer(void*, void*, u64)                 override {}
  void           pushConstants(void*, void*, u32, u32, u32, const void*) override {}
  void           drawIndexed(void*, u32, u32, u32, i32, u32)        override {}
  void*          createSampler(const RHISamplerDesc&)              override { return reinterpret_cast<void*>(0xDEAD); }
  void           destroySampler(void*)                             override {}
  void           createSwapChain(void*, u32, u32)                   override {}
  void           beginFrame()                                      override {}
  void           endFrame()                                        override {}
  void*          getCurrentCommandBuffer()                    const override { return nullptr; }
  void*          getCurrentSwapchainTarget()                  const override { return nullptr; }
  void*          getCurrentSwapchainImage()                   const override { return nullptr; }
  void           getSwapchainExtent(u32& w, u32& h)           const override { w = 0; h = 0; }
  const void*    getNativeHandles()                           const override { return nullptr; }
};