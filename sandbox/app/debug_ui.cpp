#include "debug_ui.h"
#include "rhi/vulkan/VkRHIBackend.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

void initDebugUI(Renderer& renderer, GLFWwindow* window) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui_ImplGlfw_InitForVulkan(window, true);

  auto* native = static_cast<const VkRHINativeHandles*>(renderer.getNativeHandles());
  VkFormat swapchainFormat = native->swapchainColorFormat;

  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = native->instance;
  init_info.PhysicalDevice = native->physicalDevice;
  init_info.Device = native->device;
  init_info.QueueFamily = native->queueFamily;
  init_info.Queue = native->queue;
  init_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
  init_info.MinImageCount = 2;
  init_info.ImageCount = 3;
  init_info.UseDynamicRendering = true;

  init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
  init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
  init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;

  ImGui_ImplVulkan_Init(&init_info);
}

void beginDebugUIFrame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void renderDebugUI(Renderer& renderer) {
  ImGui::Render();

  VkCommandBuffer cmd = static_cast<VkCommandBuffer>(renderer.getCurrentCommandBuffer());
  VkImageView currentSwapchainImageView = static_cast<VkImageView>(renderer.getCurrentSwapchainTarget());

  VkRenderingAttachmentInfo colorAttachment{};
  colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachment.imageView = currentSwapchainImageView;
  colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.clearValue = { { 0.1f, 0.1f, 0.15f, 1.0f } };

  u32 width, height;
  renderer.getSwapchainExtent(width, height);

  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = { { 0, 0 }, { width, height } };
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachment;

  vkCmdBeginRendering(cmd, &renderingInfo);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
  vkCmdEndRendering(cmd);
}

void shutdownDebugUI() {
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}
