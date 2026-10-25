#include <cstdio>
#include "rg/RenderGraph.h"
#include "rhi/vulkan/VkRHIBackend.h"

struct MockBackend final : IRHIBackend {
  void* createImage(const RHITextureDesc&)            override { printf("  [backend] createImage\n");  return reinterpret_cast<void*>(0xDEAD); }
  void* createBuffer(const RHIBufferDesc&)            override { printf("  [backend] createBuffer\n"); return reinterpret_cast<void*>(0xDEAD); }
  void  destroyImage(void*)                           override { printf("  [backend] destroyImage\n"); }
  void  destroyBuffer(void*)                          override { printf("  [backend] destroyBuffer\n"); }
  RHIMemoryRequirements getMemoryRequirements(void*, RHIResourceKind) override { return {0, 1}; }
  RHIMemoryBlock allocatePool(u64, RHIMemoryType)      override { return {}; }
  void           freePool(RHIMemoryBlock)             override {}
  void           bindMemory(void*, RHIMemoryBlock, u64) override {}
  void           emitBarrier(const RHIBarrierInfo&, void*) override {}
  void           beginPass(void*, RHIPassType)         override {}
  void           endPass(void*)                       override {}
};

int main() {
  VkRHIBackend backend;
  //MockBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  for (int frame = 0; frame < 3; frame++) {
    printf("\n=== frame %d ===\n", frame);

    RGResourceHandle color = rg.createImage("color", RHI_MEMORY_GPU_ONLY, RHITextureDesc{1280, 720, 1, 1, 1});
    RGResourceHandle depth = rg.createImage("depth", RHI_MEMORY_GPU_ONLY, RHITextureDesc{1280, 720, 1, 1, 1});
    RGResourceHandle light = rg.createImage("light", RHI_MEMORY_GPU_ONLY, RHITextureDesc{1280, 720, 1, 1, 1});

    rg.addPass("GBuffer", RG_PASS_RASTER,
      [&](RenderGraph::PassBuilder& b) {
        color = b.write(color, RHI_USAGE_COLOR_ATTACHMENT);
        depth = b.write(depth, RHI_USAGE_DEPTH_ATTACHMENT);
      },
      [](RGResources&, void*) {}
    );

    rg.addPass("Lighting", RG_PASS_RASTER,
      [&](RenderGraph::PassBuilder& b) {
        b.read(color, RHI_USAGE_SAMPLED_TEXTURE);
        b.read(depth, RHI_USAGE_SAMPLED_TEXTURE);
        light = b.write(light, RHI_USAGE_COLOR_ATTACHMENT);
      },
      [](RGResources&, void*) {}
    );

    rg.addPass("Present", static_cast<RGPassFlags>(RG_PASS_RASTER | RG_PASS_NEVER_CULL),
      [&](RenderGraph::PassBuilder& b) { b.read(light, RHI_USAGE_SAMPLED_TEXTURE); },
      [](RGResources&, void*) {}
    );

    printf("  [compile]\n");
    rg.compile();
    printf("  [reset]\n");
    rg.reset();
  }

  rg.dumpJSON("build/graph.json");
  return 0;  // ~RenderGraph() calls destroy() -> pool.flush()
}
