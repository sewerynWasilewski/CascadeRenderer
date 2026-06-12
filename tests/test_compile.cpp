#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "rg/RenderGraph.h"
#include "mock_backend.h"
#include <algorithm>

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

  RGResourceHandle color = rg.create<MockTexture>("color", RG_RESOURCE_TEXTURE,
                                                  RG_MEMORY_GPU_ONLY, MockTexture::Desc{1280, 720});

  RGPassHandle writer = rg.addPass("Writer", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) { color = b.write(color, RG_USAGE_COLOR_ATTACHMENT); },
    [](RGResources&, void*) {}
  );

  RGPassHandle readerA = rg.addPass("ReaderA", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) { b.read(color, RG_USAGE_SAMPLED_TEXTURE); },
    [](RGResources&, void*) {}
  );

  RGPassHandle readerB = rg.addPass("ReaderB", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) { b.read(color, RG_USAGE_SAMPLED_TEXTURE); },
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

  RGResourceHandle color = rg.create<MockTexture>("color", RG_RESOURCE_TEXTURE,
                                                  RG_MEMORY_GPU_ONLY, MockTexture::Desc{1280, 720});

  RGPassHandle writer = rg.addPass("Writer", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) { color = b.write(color, RG_USAGE_COLOR_ATTACHMENT); },
    [](RGResources&, void*) {}
  );

  RGPassHandle readerA = rg.addPass("ReaderA", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) { b.read(color, RG_USAGE_SAMPLED_TEXTURE); },
    [](RGResources&, void*) {}
  );

  RGPassHandle readerB = rg.addPass("ReaderB", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) { b.read(color, RG_USAGE_SAMPLED_TEXTURE); },
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

  RGResourceHandle color = rg.create<MockTexture>("color", RG_RESOURCE_TEXTURE,
                                                  RG_MEMORY_GPU_ONLY, MockTexture::Desc{1280, 720});

  RGPassHandle writer = rg.addPass("Writer", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) { color = b.write(color, RG_USAGE_COLOR_ATTACHMENT); },
    [](RGResources&, void*) {}
  );

  rg.compile();

  bool hasTerminal = false;
  for (const auto& e : rg.edges())
    if (e.from_pass == writer.id && e.to_pass == RG_INVALID_ID)
      hasTerminal = true;

  CHECK(hasTerminal);
}
