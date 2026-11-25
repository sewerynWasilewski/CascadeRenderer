// Exercises the real VkRHIBackend (no mock) to verify that RenderGraph::allocate()
// actually creates/binds GPU memory correctly and that aliasing barriers are only
// generated between transient resources whose lifetimes do not overlap.
// Compiled out entirely when Vulkan isn't available (see tests/CMakeLists.txt).
#ifdef RENDERER_VULKAN

#include "doctest/doctest.h"
#include "rg/RenderGraph.h"
#include "rhi/vulkan/VkRHIBackend.h"

namespace {

RHITextureDesc colorImageDesc() {
  return RHITextureDesc{64, 64, 1, 1, 1, RHI_FORMAT_RGBA8_UNORM, RHI_USAGE_COLOR_ATTACHMENT};
}

RHIBufferDesc uniformBufferDesc() {
  return RHIBufferDesc{256, RHI_USAGE_UNIFORM_BUFFER};
}

} // namespace

TEST_CASE("VkRHIBackend creates and binds a transient image through RenderGraph::allocate()") {
  VkRHIBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  RGResourceHandle img = rg.createImage("img", RHI_MEMORY_GPU_ONLY, colorImageDesc());
  rg.addPass("Write", RG_PASS_RASTER | RG_PASS_NEVER_CULL,
    [&](RenderGraph::PassBuilder& b) { img = b.write(img, RHI_USAGE_COLOR_ATTACHMENT); },
    [](RGResources&, void*) {});

  rg.compile();
  rg.plan();
  rg.allocate(); // real vkCreateImage + vkGetImageMemoryRequirements + vkBindImageMemory

  REQUIRE(rg.resources()[img.id].first_pass != RG_INVALID_ID);
  CHECK(rg.resources()[img.id].physical_range.size > 0);
}

TEST_CASE("VkRHIBackend creates and binds a transient buffer through RenderGraph::allocate()") {
  VkRHIBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  RGResourceHandle buf = rg.createBuffer("buf", RHI_MEMORY_CPU_TO_GPU, uniformBufferDesc());
  rg.addPass("Write", RG_PASS_RASTER | RG_PASS_NEVER_CULL,
    [&](RenderGraph::PassBuilder& b) { buf = b.write(buf, RHI_USAGE_UNIFORM_BUFFER); },
    [](RGResources&, void*) {});

  rg.compile();
  rg.plan();
  rg.allocate(); // real vkCreateBuffer + vkGetBufferMemoryRequirements + vkBindBufferMemory

  REQUIRE(rg.resources()[buf.id].first_pass != RG_INVALID_ID);
  CHECK(rg.resources()[buf.id].physical_range.size >= 256);
}

TEST_CASE("Non-overlapping transient images alias the same memory range") {
  VkRHIBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  RGResourceHandle imgA = rg.createImage("imgA", RHI_MEMORY_GPU_ONLY, colorImageDesc());
  RGResourceHandle imgB = rg.createImage("imgB", RHI_MEMORY_GPU_ONLY, colorImageDesc());

  rg.addPass("PassA", RG_PASS_RASTER | RG_PASS_NEVER_CULL,
    [&](RenderGraph::PassBuilder& b) { imgA = b.write(imgA, RHI_USAGE_COLOR_ATTACHMENT); },
    [](RGResources&, void*) {});

  rg.addPass("PassB", RG_PASS_RASTER | RG_PASS_NEVER_CULL,
    [&](RenderGraph::PassBuilder& b) { imgB = b.write(imgB, RHI_USAGE_COLOR_ATTACHMENT); },
    [](RGResources&, void*) {});

  rg.compile();

  REQUIRE(rg.resources()[imgA.id].last_pass  == 0);
  REQUIRE(rg.resources()[imgB.id].first_pass == 1);

  rg.plan();
  rg.allocate();

  const auto& rangeA = rg.resources()[imgA.id].physical_range;
  const auto& rangeB = rg.resources()[imgB.id].physical_range;
  CHECK(rangeA.offset == rangeB.offset); // A's lifetime ends before B's starts - must be reused

  bool foundAliasingBarrier = false;
  for (const auto& barrier : rg.barriers()) {
    if (barrier.kind == RHI_BARRIER_ALIASING && barrier.resource_id == imgB.id &&
        barrier.src_pass == 0 && barrier.dst_pass == 1)
      foundAliasingBarrier = true;
  }
  CHECK(foundAliasingBarrier);
}

TEST_CASE("Transient images with overlapping lifetimes do not alias") {
  VkRHIBackend backend;
  RenderGraph rg;
  rg.setBackend(&backend);

  RGResourceHandle imgC = rg.createImage("imgC", RHI_MEMORY_GPU_ONLY, colorImageDesc());
  RGResourceHandle imgD = rg.createImage("imgD", RHI_MEMORY_GPU_ONLY, colorImageDesc());

  rg.addPass("Produce", RG_PASS_RASTER | RG_PASS_NEVER_CULL,
    [&](RenderGraph::PassBuilder& b) {
      imgC = b.write(imgC, RHI_USAGE_COLOR_ATTACHMENT);
      imgD = b.write(imgD, RHI_USAGE_COLOR_ATTACHMENT);
    },
    [](RGResources&, void*) {});

  rg.addPass("Consume", RG_PASS_RASTER | RG_PASS_NEVER_CULL,
    [&](RenderGraph::PassBuilder& b) {
      b.read(imgC, RHI_USAGE_SAMPLED_TEXTURE);
      b.read(imgD, RHI_USAGE_SAMPLED_TEXTURE);
    },
    [](RGResources&, void*) {});

  rg.compile();

  REQUIRE(rg.resources()[imgC.id].first_pass == 0);
  REQUIRE(rg.resources()[imgC.id].last_pass  == 1);
  REQUIRE(rg.resources()[imgD.id].first_pass == 0);
  REQUIRE(rg.resources()[imgD.id].last_pass  == 1);

  rg.plan();
  rg.allocate();

  const auto& rangeC = rg.resources()[imgC.id].physical_range;
  const auto& rangeD = rg.resources()[imgD.id].physical_range;
  const bool overlap = rangeC.offset + rangeC.size > rangeD.offset &&
                        rangeD.offset + rangeD.size > rangeC.offset;
  CHECK_FALSE(overlap); // both live across the same two passes - must not share memory

  for (const auto& barrier : rg.barriers())
    CHECK(!(barrier.kind == RHI_BARRIER_ALIASING &&
            ((barrier.resource_id == imgC.id) || (barrier.resource_id == imgD.id))));
}

#endif // RENDERER_VULKAN
