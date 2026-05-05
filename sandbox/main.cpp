#include <cstdio>
#include <cassert>

#include "rg/RenderGraph.h"

struct MockTexture {
  struct Desc { uint32_t width, height; };
  void create(const Desc&, void*)  { printf("  [MockTexture] create\n"); }
  void destroy(const Desc&, void*) { printf("  [MockTexture] destroy\n"); }
};

int main() {
  RenderGraph rg;

  RGResourceHandle color  = rg.create<MockTexture>("color",  RG_RESOURCE_TEXTURE, MockTexture::Desc{1280, 720});
  RGResourceHandle depth  = rg.create<MockTexture>("depth",  RG_RESOURCE_TEXTURE, MockTexture::Desc{1280, 720});
  RGResourceHandle light  = rg.create<MockTexture>("light",  RG_RESOURCE_TEXTURE, MockTexture::Desc{1280, 720});
  RGResourceHandle unused = rg.create<MockTexture>("unused", RG_RESOURCE_TEXTURE, MockTexture::Desc{1,    1   });

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

  // This pass writes to unused — nobody reads it, so it should be culled.
  rg.addPass("DeadPass", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) {
      unused = b.write(unused, RG_USAGE_COLOR_ATTACHMENT);
    },
    [](RGResources&, void*) { printf("  [exec] DeadPass (should not run)\n"); }
  );

  rg.addPass("Present", static_cast<RG_PassFlags>(RG_PASS_RASTER | RG_PASS_NEVER_CULL),
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
