#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstdio>

#include <vector>

#include "rg/RenderGraph.h"
#include "rhi/vulkan/VkRHIBackend.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Cascade Renderer - ImGui & Vulkan", nullptr, nullptr);

    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;

    {
        VkRHIBackend rhiBackend(window);
        rhiBackend.createSwapChain(window);

        RenderGraph rg;
        rg.setBackend(&rhiBackend);

        {
            VkDescriptorPoolSize pool_sizes[] = {
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 }
            };
            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets = 100;
            pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
            pool_info.pPoolSizes = pool_sizes;
            vkCreateDescriptorPool(rhiBackend.getDevice(), &pool_info, nullptr, &imguiDescriptorPool);
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui_ImplGlfw_InitForVulkan(window, true);

        VkFormat swapchainFormat = rhiBackend.getSwapchainImageFormat();

        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = rhiBackend.getInstance();
        init_info.PhysicalDevice = rhiBackend.getPhysicalDevice();
        init_info.Device = rhiBackend.getDevice();
        init_info.QueueFamily = rhiBackend.getGraphicsQueueFamily();
        init_info.Queue = rhiBackend.getGraphicsQueue();
        init_info.DescriptorPool = imguiDescriptorPool;
        init_info.MinImageCount = 2;
        init_info.ImageCount = 3;
        init_info.UseDynamicRendering = true;

        init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;

        ImGui_ImplVulkan_Init(&init_info);

        uint64_t frameCount = 0;

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            rhiBackend.beginFrame();

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGui::Begin("Cascade Renderer Debug");
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::Text("Frame: %lu", frameCount++);
            ImGui::End();

            ImGui::Render();

            VkCommandBuffer cmd = rhiBackend.getCurrentCommandBuffer();
            VkImageView currentSwapchainImageView = rhiBackend.getCurrentSwapchainImageView(); 

            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = currentSwapchainImageView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue = { { 0.1f, 0.1f, 0.15f, 1.0f } };

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = { { 0, 0 }, { 1280, 720 } };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;

            vkCmdBeginRendering(cmd, &renderingInfo);
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
            vkCmdEndRendering(cmd);

            rhiBackend.endFrame();

            RGResourceHandle color = rg.createImage("color", RHI_MEMORY_GPU_ONLY, RHITextureDesc{1280, 720, 1, 1, 1});
            RGResourceHandle depth = rg.createImage("depth", RHI_MEMORY_GPU_ONLY, RHITextureDesc{1280, 720, 1, 1, 1});
            RGResourceHandle light = rg.createImage("light", RHI_MEMORY_GPU_ONLY, RHITextureDesc{1280, 720, 1, 1, 1});

            rg.addPass("GBuffer", RG_PASS_RASTER,
              [&](RenderGraph::PassBuilder& b) {
                color = b.write(color, RHI_USAGE_COLOR_ATTACHMENT);
                depth = b.write(depth, RHI_USAGE_DEPTH_ATTACHMENT);
              },
              [](RGResources&, void*) {}
            );

            rg.addPass("Lighting", RG_PASS_RASTER,
              [&](RenderGraph::PassBuilder& b) {
                b.read(color, RHI_USAGE_SAMPLED_TEXTURE);
                b.read(depth, RHI_USAGE_SAMPLED_TEXTURE);
                light = b.write(light, RHI_USAGE_COLOR_ATTACHMENT);
              },
              [](RGResources&, void*) {}
            );

            rg.addPass("Present", static_cast<RGPassFlags>(RG_PASS_RASTER | RG_PASS_NEVER_CULL),
              [&](RenderGraph::PassBuilder& b) { b.read(light, RHI_USAGE_SAMPLED_TEXTURE); },
              [](RGResources&, void*) {}
            );

            rg.compile();
            rg.reset();
        }

        rg.dumpJSON("graph.json");

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(rhiBackend.getDevice(), imguiDescriptorPool, nullptr);
    } 

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}