#pragma once
#include "gpu_types.h"
#include "scene.h"
#include "texture.h"

class RenderGraph;


class Renderer {
public:
  Renderer() = default;

  void setBackbuffer(void* gpuHandle, u32 width, u32 height);

  // Describes one frame: GBuffer -> Lighting -> Present.
  void buildGraph(RenderGraph& rg, const Scene& scene);

  u32 width() const { return mWidth; }
  u32 height() const { return mHeight; }

private:
  void* mBackbufferHandle = nullptr; // VkImage owned by the swapchain
  u32   mWidth            = 1280;
  u32   mHeight           = 720;
};
