#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "rg/RenderGraph.h"
#include "mock_backend.h"
#include <algorithm>

// Note: findings/finding2 (fnv1a ODR violation) has no runtime test — the bug manifests
// as a linker error when two TUs include algorithm.h. The fix (inline) is verified by the
// fact that this TU and any future TU can both include RenderGraph.h without a link failure.

// Helper: return the global_index (topo position) of a pass by its RGPassHandle.
// Lower index = earlier in execution order.
static u32 passOrder(const RenderGraph& rg, RGPassHandle h) {
  return rg.sortedPasses()[h.id];  // sortedPasses()[pass_id] == global_index
}

// Helper: true if pass A is scheduled before pass B.
static bool before(const RenderGraph& rg, RGPassHandle a, RGPassHandle b) {
  const auto& sorted = rg.sortedPasses();
  // Find positions in sorted order
  auto posA = std::find(sorted.begin(), sorted.end(), a.id);
  auto posB = std::find(sorted.begin(), sorted.end(), b.id);
  return posA < posB;
}

// ─── Finding 1: edge fan-out for multiple readers ────────────────────────────
// A single write with two readers must create two independent edges so both
// readers get correct indegree and are scheduled after the writer.

TEST_CASE("single writer two readers: both readers scheduled after writer") {
  MockBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  RGResourceHandle color = rg.create<MockTexture>("color", RHI_RESOURCE_TEXTURE,
                                                  RHI_MEMORY_GPU_ONLY, MockTexture::Desc{1280, 720});

  RGPassHandle writer = rg.addPass("Writer", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) { color = b.write(color, RHI_USAGE_COLOR_ATTACHMENT); },
    [](RGResources&, void*) {}
  );

  // NEVER_CULL: leaf passes (no consumers) simulate a swapchain present pass.
  // Without the flag dead-pass culling removes them from sortedPasses() and before() returns false.
  RGPassHandle readerA = rg.addPass("ReaderA", RG_PASS_RASTER | RG_PASS_NEVER_CULL,
    [&](RenderGraph::PassBuilder& b) { b.read(color, RHI_USAGE_SAMPLED_TEXTURE); },
    [](RGResources&, void*) {}
  );

  RGPassHandle readerB = rg.addPass("ReaderB", RG_PASS_RASTER | RG_PASS_NEVER_CULL,
    [&](RenderGraph::PassBuilder& b) { b.read(color, RHI_USAGE_SAMPLED_TEXTURE); },
    [](RGResources&, void*) {}
  );

  rg.compile();

  CHECK(before(rg, writer, readerA));
  CHECK(before(rg, writer, readerB));
}

TEST_CASE("single writer two readers: two separate edges emitted") {
  MockBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  RGResourceHandle color = rg.create<MockTexture>("color", RHI_RESOURCE_TEXTURE,
                                                  RHI_MEMORY_GPU_ONLY, MockTexture::Desc{1280, 720});

  RGPassHandle writer = rg.addPass("Writer", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) { color = b.write(color, RHI_USAGE_COLOR_ATTACHMENT); },
    [](RGResources&, void*) {}
  );

  RGPassHandle readerA = rg.addPass("ReaderA", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) { b.read(color, RHI_USAGE_SAMPLED_TEXTURE); },
    [](RGResources&, void*) {}
  );

  RGPassHandle readerB = rg.addPass("ReaderB", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) { b.read(color, RHI_USAGE_SAMPLED_TEXTURE); },
    [](RGResources&, void*) {}
  );

  rg.compile();

  // One edge per reader, not one edge total with the last reader winning
  u32 readerAEdges = 0, readerBEdges = 0;
  for (const auto& e : rg.edges()) {
    if (e.to_pass == readerA.id) readerAEdges++;
    if (e.to_pass == readerB.id) readerBEdges++;
  }
  CHECK(readerAEdges == 1);
  CHECK(readerBEdges == 1);
}

TEST_CASE("write with no readers produces terminal edge") {
  MockBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  RGResourceHandle color = rg.create<MockTexture>("color", RHI_RESOURCE_TEXTURE,
                                                  RHI_MEMORY_GPU_ONLY, MockTexture::Desc{1280, 720});

  RGPassHandle writer = rg.addPass("Writer", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) { color = b.write(color, RHI_USAGE_COLOR_ATTACHMENT); },
    [](RGResources&, void*) {}
  );

  rg.compile();

  bool hasTerminal = false;
  for (const auto& e : rg.edges())
    if (e.from_pass == writer.id && e.to_pass == RG_INVALID_ID)
      hasTerminal = true;

  CHECK(hasTerminal);
}

// ─── Finding 3: compile() twice without reset() must not leak handles ────────
// If compile() is called twice, the handle from the first call must be released
// back to the pool before acquiring a new one. Without the fix, the first handle
// is orphaned — never returned to the pool, never destroyed.

TEST_CASE("compile() twice without reset() does not leak GPU handles") {
  struct CountingBackend final : MockBackend {
    int created = 0, destroyed = 0;
    void* createImage(const RHITextureDesc&) override { created++;  return reinterpret_cast<void*>(0xDEAD); }
    void  destroyImage(void*)               override { destroyed++; }
  };

  CountingBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  RGResourceHandle color = rg.create<MockTexture>("color", RHI_RESOURCE_TEXTURE,
                                                  RHI_MEMORY_GPU_ONLY, MockTexture::Desc{1280, 720});
  // NEVER_CULL: Writer is a leaf pass. Without the flag it is culled and no GPU handle
  // is created, making the create/destroy count assertions meaningless.
  rg.addPass("Writer", RG_PASS_RASTER | RG_PASS_NEVER_CULL,
    [&](RenderGraph::PassBuilder& b) { color = b.write(color, RHI_USAGE_COLOR_ATTACHMENT); },
    [](RGResources&, void*) {}
  );

  rg.compile();   // creates 1 handle, created==1
  CHECK(backend.created == 1);

  rg.compile();   // releases handle back to pool, re-acquires it — pool hit, no new creation
  CHECK(backend.created   == 1);  // no second createImage
  CHECK(backend.destroyed == 0);  // handle not destroyed, just cycled through pool

  rg.reset();     // returns handle to pool
  rg.destroy();   // pool flush destroys it
  CHECK(backend.destroyed == 1);
}
