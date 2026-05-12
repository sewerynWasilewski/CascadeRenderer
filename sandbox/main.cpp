#include <cstdio>
#include "rg/RenderGraph.h"

struct MockBackend final : IRHIBackend {
  void* createImage(const RHITextureDesc&)            override { printf("  [backend] createImage\n");  return reinterpret_cast<void*>(0xDEAD); }
  void* createBuffer(const RHIBufferDesc&)            override { printf("  [backend] createBuffer\n"); return reinterpret_cast<void*>(0xDEAD); }
  void  destroyImage(void*)                           override { printf("  [backend] destroyImage\n"); }
  void  destroyBuffer(void*)                          override { printf("  [backend] destroyBuffer\n"); }
  RHIMemoryRequirements getMemoryRequirements(void*, RGResourceKind) override { return {0, 1}; }
  GPUMemoryBlock allocatePool(u64, RGMemoryType)      override { return {}; }
  void           freePool(GPUMemoryBlock)             override {}
  void           bindMemory(void*, GPUMemoryBlock, u64) override {}
  void           emitBarrier(const RGBarrierInfo&, void*) override {}
  void           beginPass(void*)                     override {}
  void           endPass(void*)                       override {}
};

struct MockTexture {
  struct Desc { uint32_t width, height; };

  static void* createGPU(const Desc& d, IRHIBackend* backend) {
    return backend->createImage(RHITextureDesc{d.width, d.height, 1, 1, 1});
  }
  static void destroyGPU(const Desc&, IRHIBackend* backend, void* handle) {
    backend->destroyImage(handle);
  }
};

int main() {
  MockBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  RGResourceHandle color  = rg.create<MockTexture>("color",  RG_RESOURCE_TEXTURE, RG_MEMORY_GPU_ONLY, MockTexture::Desc{1280, 720});
  RGResourceHandle depth  = rg.create<MockTexture>("depth",  RG_RESOURCE_TEXTURE, RG_MEMORY_GPU_ONLY, MockTexture::Desc{1280, 720});
  RGResourceHandle light  = rg.create<MockTexture>("light",  RG_RESOURCE_TEXTURE, RG_MEMORY_GPU_ONLY, MockTexture::Desc{1280, 720});
  RGResourceHandle unused = rg.create<MockTexture>("unused", RG_RESOURCE_TEXTURE, RG_MEMORY_GPU_ONLY, MockTexture::Desc{1, 1});

  rg.addPass("GBuffer", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) {
      color = b.write(color, RG_USAGE_COLOR_ATTACHMENT);
      depth = b.write(depth, RG_USAGE_DEPTH_ATTACHMENT);
    },
    [](RGResources&, void*) { printf("  [exec] GBuffer\n"); }
  );

  rg.addPass("Lighting", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) {
      b.read(color, RG_USAGE_SAMPLED_TEXTURE);
      b.read(depth, RG_USAGE_SAMPLED_TEXTURE);
      light = b.write(light, RG_USAGE_COLOR_ATTACHMENT);
    },
    [](RGResources&, void*) { printf("  [exec] Lighting\n"); }
  );

  rg.addPass("DeadPass", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) {
      unused = b.write(unused, RG_USAGE_COLOR_ATTACHMENT);
    },
    [](RGResources&, void*) { printf("  [exec] DeadPass (should not run)\n"); }
  );

  rg.addPass("Present", static_cast<RGPassFlags>(RG_PASS_RASTER | RG_PASS_NEVER_CULL),
    [&](RenderGraph::PassBuilder& b) {
      b.read(light, RG_USAGE_SAMPLED_TEXTURE);
    },
    [](RGResources&, void*) { printf("  [exec] Present\n"); }
  );

  printf("compiling...\n");
  rg.compile();
  printf("done.\n");

  return 0;
}
