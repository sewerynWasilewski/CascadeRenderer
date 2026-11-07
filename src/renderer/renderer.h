#pragma once
#include "rg/RenderGraph.h"
#include <memory>

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

  const void* getNativeHandles()          const { return mBackend->getNativeHandles(); }
  void*       getCurrentCommandBuffer()   const { return mBackend->getCurrentCommandBuffer(); }
  void*       getCurrentSwapchainTarget() const { return mBackend->getCurrentSwapchainTarget(); }
  void        getSwapchainExtent(u32& width, u32& height) const { mBackend->getSwapchainExtent(width, height); }

private:
  std::unique_ptr<IRHIBackend> mBackend;
  RenderGraph mGraph;
};
