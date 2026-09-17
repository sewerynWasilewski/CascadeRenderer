#include "renderer.h"

#include "rg/RenderGraph.h"

void Renderer::setBackbuffer(void* gpuHandle, u32 width, u32 height) {
  mBackbufferHandle = gpuHandle;
  mWidth            = width;
  mHeight           = height;
}

void Renderer::buildGraph(RenderGraph& rg, const Scene& scene) {
  (void)scene;

  const Texture::Desc gbufferColorDesc{ .width = mWidth, .height = mHeight, .format = TextureFormat::RGBA8_UNORM };
  const Texture::Desc gbufferNormalDesc{ .width = mWidth, .height = mHeight, .format = TextureFormat::RGBA16_SFLOAT };
  const Texture::Desc gbufferDepthDesc{ .width = mWidth, .height = mHeight, .format = TextureFormat::D32_SFLOAT };
  const Texture::Desc hdrDesc{ .width = mWidth, .height = mHeight, .format = TextureFormat::RGBA16_SFLOAT };

  RGResourceHandle gbufferAlbedo = rg.create<Texture>("gbuffer_albedo", RHI_RESOURCE_TEXTURE, RHI_MEMORY_GPU_ONLY, gbufferColorDesc);
  RGResourceHandle gbufferNormal = rg.create<Texture>("gbuffer_normal", RHI_RESOURCE_TEXTURE, RHI_MEMORY_GPU_ONLY, gbufferNormalDesc);
  RGResourceHandle gbufferDepth  = rg.create<Texture>("gbuffer_depth",  RHI_RESOURCE_TEXTURE, RHI_MEMORY_GPU_ONLY, gbufferDepthDesc);
  RGResourceHandle hdrColor      = rg.create<Texture>("hdr_color",      RHI_RESOURCE_TEXTURE, RHI_MEMORY_GPU_ONLY, hdrDesc);

  RGResourceHandle backbuffer = rg.import<Texture>(
    "backbuffer", RHI_RESOURCE_TEXTURE, RHI_MEMORY_GPU_ONLY, gbufferColorDesc,
    Texture{}, mBackbufferHandle);

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
