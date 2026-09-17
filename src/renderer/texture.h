#pragma once
#include "gpu_types.h"
#include "rhi/IRHIBackend.h"

enum class TextureFormat : u32 {
  RGBA8_UNORM,   // gbuffer albedo, backbuffer
  RGBA16_SFLOAT, // gbuffer normal, HDR color
  D32_SFLOAT,    // depth
};

struct Texture {
  struct Desc {
    u32           width     = 1;
    u32           height    = 1;
    u32           mipLevels = 1;
    TextureFormat format    = TextureFormat::RGBA8_UNORM;
  };

  static void* createGPU(const Desc& d, IRHIBackend* backend) {
    return backend->createImage(RHITextureDesc{d.width, d.height, 1, d.mipLevels, 1});
  }

  static void destroyGPU(const Desc&, IRHIBackend* backend, void* handle) {
    backend->destroyImage(handle);
  }
};
