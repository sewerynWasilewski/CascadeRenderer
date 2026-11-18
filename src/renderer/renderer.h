#pragma once
#include <memory>

#include "rg/RenderGraph.h"
#include "scene.h"

enum class RHIBackendType {
  Vulkan,
};

class Renderer {
public:
  Renderer() = default;
  ~Renderer() = default;
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void init(RHIBackendType type);
  void createSwapChain(void* nativeSurface, u32 framebufferWidth, u32 framebufferHeight);

  void beginFrame();
  void endFrame();

  RenderGraph& graph() { return mGraph; }

  void setBackbuffer(void* gpuHandle, u32 width, u32 height);
  // Describes one frame: GBuffer -> Lighting -> Present.
  void buildGraph(RenderGraph& rg, const Scene& scene);

  u32 width() const { return mWidth; }
  u32 height() const { return mHeight; }

  const void* getNativeHandles()          const { return mBackend->getNativeHandles(); }
  void*       getCurrentCommandBuffer()   const { return mBackend->getCurrentCommandBuffer(); }
  void*       getCurrentSwapchainTarget() const { return mBackend->getCurrentSwapchainTarget(); }
  void        getSwapchainExtent(u32& width, u32& height) const { mBackend->getSwapchainExtent(width, height); }

private:
  std::unique_ptr<IRHIBackend> mBackend;
  RenderGraph mGraph;

  void* mBackbufferHandle = nullptr; // VkImage owned by the swapchain
  u32   mWidth            = 1280;
  u32   mHeight           = 720;
};
