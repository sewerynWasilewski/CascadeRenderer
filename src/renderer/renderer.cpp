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

  constexpr u32 kSampledColor = static_cast<u32>(RHI_USAGE_COLOR_ATTACHMENT | RHI_USAGE_SAMPLED_TEXTURE);
  constexpr u32 kSampledDepth = static_cast<u32>(RHI_USAGE_DEPTH_ATTACHMENT | RHI_USAGE_SAMPLED_TEXTURE);
  constexpr u32 kColorOnly    = static_cast<u32>(RHI_USAGE_COLOR_ATTACHMENT);

  const RHITextureDesc albedoDesc  { mWidth, mHeight, 1, 1, 1, RHI_FORMAT_RGBA8_UNORM,   kSampledColor };
  const RHITextureDesc normalDesc  { mWidth, mHeight, 1, 1, 1, RHI_FORMAT_RGBA16_FLOAT,  kSampledColor };
  const RHITextureDesc depthDesc   { mWidth, mHeight, 1, 1, 1, RHI_FORMAT_D32_FLOAT,     kSampledDepth };
  const RHITextureDesc hdrDesc     { mWidth, mHeight, 1, 1, 1, RHI_FORMAT_RGBA16_FLOAT,  kSampledColor };
  const RHITextureDesc backbufferDesc{ mWidth, mHeight, 1, 1, 1, RHI_FORMAT_RGBA8_UNORM, kColorOnly };

  RGResourceHandle gbufferAlbedo = rg.createImage("gbuffer_albedo", RHI_MEMORY_GPU_ONLY, albedoDesc);
  RGResourceHandle gbufferNormal = rg.createImage("gbuffer_normal", RHI_MEMORY_GPU_ONLY, normalDesc);
  RGResourceHandle gbufferDepth  = rg.createImage("gbuffer_depth",  RHI_MEMORY_GPU_ONLY, depthDesc);
  RGResourceHandle hdrColor      = rg.createImage("hdr_color",      RHI_MEMORY_GPU_ONLY, hdrDesc);

  RGResourceHandle backbuffer = rg.importImage("backbuffer", RHI_MEMORY_GPU_ONLY, backbufferDesc, mBackbufferHandle);

  rg.addPass("GBuffer", RG_PASS_RASTER,
    [&](RenderGraph::PassBuilder& b) {
      gbufferAlbedo = b.write(gbufferAlbedo, RHI_USAGE_COLOR_ATTACHMENT);
      gbufferNormal = b.write(gbufferNormal, RHI_USAGE_COLOR_ATTACHMENT);
      gbufferDepth  = b.write(gbufferDepth,  RHI_USAGE_DEPTH_ATTACHMENT);
    },
    [](RGResources&, void*) {
        
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
