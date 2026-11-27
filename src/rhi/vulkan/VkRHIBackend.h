#pragma once
#include <volk.h>
#include "../IRHIBackend.h"
#include "VkRHIAllocator.h"
#include "../../rg/RGTypes.h"
#include <vector>

struct VkRHINativeHandles {
  VkInstance       instance;
  VkPhysicalDevice physicalDevice;
  VkDevice         device;
  VkQueue          queue;
  u32              queueFamily;
  VkFormat         swapchainColorFormat;
};

// imageView is VK_NULL_HANDLE until bindMemory() creates it (needs bound memory first).
struct VkRHIImage {
  VkImage          image     = VK_NULL_HANDLE;
  VkImageView      imageView = VK_NULL_HANDLE;
  RHITextureFormat format    = RHI_FORMAT_UNDEFINED;
};

class VkRHIBackend final : public IRHIBackend {
public:
  VkRHIBackend();
  ~VkRHIBackend() override;

  void* createImage(const RHITextureDesc& desc) override;
  void* createBuffer(const RHIBufferDesc& desc) override;
  void  destroyImage(void* handle) override;
  void  destroyBuffer(void* handle) override;

  RHIMemoryRequirements getMemoryRequirements(void* gpuHandle, RHIResourceKind kind) override;

  RHIMemoryBlock allocatePool(u64 size, RHIMemoryType memoryType) override;
  void           freePool(RHIMemoryBlock block) override;
  void*          map(RHIMemoryBlock block) override;
  void           unmap(RHIMemoryBlock block) override;

  void bindMemory(void* gpuHandle, RHIResourceKind kind, RHIMemoryBlock block, u64 offset) override;
  void emitBarrier(const RHIBarrierInfo& info, void* cmdBuf) override;
  void beginPass(void* cmdBuf, RHIPassType type, void* const* colorAttachments, u32 colorAttachmentCount, void* depthAttachment) override;
  void endPass(void* cmdBuf) override;
  void* createSampler(const RHISamplerDesc& desc) override;
  void  destroySampler(void* handle) override;

  void* createPipeline(const RHIPipelineDesc& desc) override;
  void  destroyPipeline(void* handle) override;
  void  bindPipeline(void* cmdBuf, void* pipelineHandle) override;
  void  bindVertexBuffer(void* cmdBuf, void* bufferHandle, u64 offset) override;
  void  bindIndexBuffer(void* cmdBuf, void* bufferHandle, u64 offset) override;
  void  pushConstants(void* cmdBuf, void* pipelineHandle, u32 stageFlags,
                       u32 offset, u32 size, const void* data) override;
  void  drawIndexed(void* cmdBuf, u32 indexCount, u32 instanceCount,
                     u32 firstIndex, i32 vertexOffset, u32 firstInstance) override;
  void beginFrame() override;
  void endFrame() override;
  void createSwapChain(void* nativeSurface, u32 framebufferWidth, u32 framebufferHeight) override;

  void* getCurrentCommandBuffer()    const override { return m_CommandBuffer; }
  void* getCurrentSwapchainTarget()  const override;
  void* getCurrentSwapchainImage() const override;
  void  getSwapchainExtent(u32& width, u32& height) const override {
    width = m_SwapchainExtent.width;
    height = m_SwapchainExtent.height;
  }

  const void* getNativeHandles() const override { return &mNativeHandles; }
private:
  void createInstance();
  void setupDebugMessenger();
  void pickPhysicalDevice();
  void createLogicalDevice();

  bool checkValidationLayerSupport();

  VkSurfaceKHR m_Surface          = VK_NULL_HANDLE;
  VkCommandPool m_CommandPool     = VK_NULL_HANDLE;
  VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
  VkInstance               mInstance       = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT mDebugMessenger = VK_NULL_HANDLE;
  VkPhysicalDevice         mPhysicalDevice = VK_NULL_HANDLE;
  VkDevice                 mDevice         = VK_NULL_HANDLE;
  VkQueue                  mGraphicsQueue  = VK_NULL_HANDLE;
  u32                      mGraphicsQueueFamily = RG_INVALID_ID;
  VkSwapchainKHR           m_Swapchain = VK_NULL_HANDLE;
  std::vector<VkSampler> m_Samplers;
  std::vector<VkImage>     m_SwapchainImages;
  std::vector<VkImageView> m_SwapchainImageViews;
  std::vector<VkRHIImage>  m_SwapchainRHIImages; // built once, no per-frame allocation
  uint32_t m_CurrentImageIndex = 0;
  VkFormat m_SwapchainImageFormat = VK_FORMAT_UNDEFINED;
  VkExtent2D m_SwapchainExtent = {0, 0};
  VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
  VkSemaphore m_RenderFinishedSemaphore = VK_NULL_HANDLE;
  bool m_InRendering = false; // true between vkCmdBeginRendering and vkCmdEndRendering - passes with no color attachment skip both

  VkRHINativeHandles mNativeHandles{};
  VkRHIAllocator mAllocator;
};

// Queue translation: RHI_QUEUE_IGNORED → VK_QUEUE_FAMILY_IGNORED (0xFFFFFFFF)
// Other RHIQueueType values map to the VkQueueFamilyIndex acquired during device init.
