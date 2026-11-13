#include "renderer.h"
#include <stdexcept>

#ifdef RENDERER_VULKAN
#include "rhi/vulkan/VkRHIBackend.h"
#endif

void Renderer::init(RHIBackendType type) {
  switch (type) {
#ifdef RENDERER_VULKAN
    case RHIBackendType::Vulkan:
      mBackend = std::make_unique<VkRHIBackend>();
      break;
#endif
    default:
      throw std::runtime_error("Requested RHIBackendType was not compiled into this build.");
  }
  mGraph.setBackend(mBackend.get());
}

void Renderer::createSwapChain(void* nativeSurface, u32 framebufferWidth, u32 framebufferHeight) {
  mBackend->createSwapChain(nativeSurface, framebufferWidth, framebufferHeight);
}

void Renderer::beginFrame() { mBackend->beginFrame(); }
void Renderer::endFrame()   { mBackend->endFrame(); }

void Renderer::setBackbuffer(void* gpuHandle, u32 width, u32 height) {
  mBackbufferHandle = gpuHandle;
  mWidth            = width;
  mHeight           = height;
}

void Renderer::buildGraph(RenderGraph& rg, const Scene& scene) {
  (void)scene;

  // RHITextureDesc has no format yet, so every graph image uses the same placeholder desc.
  const RHITextureDesc colorDesc{ mWidth, mHeight, 1, 1, 1 };

  RGResourceHandle gbufferAlbedo = rg.createImage("gbuffer_albedo", RHI_MEMORY_GPU_ONLY, colorDesc);
  RGResourceHandle gbufferNormal = rg.createImage("gbuffer_normal", RHI_MEMORY_GPU_ONLY, colorDesc);
  RGResourceHandle gbufferDepth  = rg.createImage("gbuffer_depth",  RHI_MEMORY_GPU_ONLY, colorDesc);
  RGResourceHandle hdrColor      = rg.createImage("hdr_color",      RHI_MEMORY_GPU_ONLY, colorDesc);

  RGResourceHandle backbuffer = rg.importImage("backbuffer", RHI_MEMORY_GPU_ONLY, colorDesc, mBackbufferHandle);

  rg.addPass("GBuffer", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) {
      gbufferAlbedo = b.write(gbufferAlbedo, RHI_USAGE_COLOR_ATTACHMENT);
      gbufferNormal = b.write(gbufferNormal, RHI_USAGE_COLOR_ATTACHMENT);
      gbufferDepth  = b.write(gbufferDepth,  RHI_USAGE_DEPTH_ATTACHMENT);
    },
    [](RGResources&, void*) {
      // Sprint 3/4: bind GBuffer pipeline, draw scene.meshes with MVP push constants.
    });

  rg.addPass("Lighting", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) {
      b.read(gbufferAlbedo, RHI_USAGE_SAMPLED_TEXTURE);
      b.read(gbufferNormal, RHI_USAGE_SAMPLED_TEXTURE);
      b.read(gbufferDepth,  RHI_USAGE_SAMPLED_TEXTURE);
      hdrColor = b.write(hdrColor, RHI_USAGE_COLOR_ATTACHMENT);
    },
    [](RGResources&, void*) {
    });

  rg.addPass("Present", RG_PASS_RASTER | RG_PASS_NEVER_CULL,
    [&](RenderGraph::PassBuilder& b) {
      b.read(hdrColor, RHI_USAGE_SAMPLED_TEXTURE);
      backbuffer = b.write(backbuffer, RHI_USAGE_COLOR_ATTACHMENT);
    },
    [](RGResources&, void*) {
    });
}
