#include "doctest/doctest.h"
#include "rg/RenderGraph.h"
#include "renderer/renderer.h"
#include "mock_backend.h"


TEST_CASE("Renderer::buildGraph schedules GBuffer -> Lighting -> Present") {
  MockBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  Scene scene;
  Renderer renderer;
  renderer.setBackbuffer(reinterpret_cast<void*>(0xBEEF), 1280, 720);

  renderer.buildGraph(rg, scene);
  rg.compile();

  REQUIRE(rg.sortedPasses().size() == 3);
  CHECK(rg.sortedPasses()[0] == 0); // GBuffer
  CHECK(rg.sortedPasses()[1] == 1); // Lighting
  CHECK(rg.sortedPasses()[2] == 2); // Present
}

TEST_CASE("Renderer::buildGraph emits GBuffer -> Lighting usage transition") {
  MockBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  Scene scene;
  Renderer renderer;
  renderer.setBackbuffer(reinterpret_cast<void*>(0xBEEF), 1280, 720);

  renderer.buildGraph(rg, scene);
  rg.compile();

  u32 colorToSampled = 0;
  u32 depthToSampled = 0;
  for (const auto& b : rg.barriers()) {
    if (b.src_pass == 0 && b.dst_pass == 1) {
      if (b.before_usage == RHI_USAGE_COLOR_ATTACHMENT && b.after_usage == RHI_USAGE_SAMPLED_TEXTURE)
        colorToSampled++;
      if (b.before_usage == RHI_USAGE_DEPTH_ATTACHMENT && b.after_usage == RHI_USAGE_SAMPLED_TEXTURE)
        depthToSampled++;
    }
  }

  CHECK(colorToSampled == 2); // albedo + normal
  CHECK(depthToSampled == 1); // depth
}

TEST_CASE("Renderer::buildGraph keeps the external backbuffer rendered into") {
  MockBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  Scene scene;
  Renderer renderer;
  renderer.setBackbuffer(reinterpret_cast<void*>(0xBEEF), 1280, 720);

  renderer.buildGraph(rg, scene);
  rg.compile();

  bool presentWritesExternal = false;
  for (const auto& e : rg.edges()) {
    if (e.from_pass == 2 && e.to_pass == RG_INVALID_ID)
      presentWritesExternal = true;
  }
  CHECK(presentWritesExternal);
}
