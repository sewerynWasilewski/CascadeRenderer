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
