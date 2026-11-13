#include "renderer/renderer.h"
#include "rhi/vulkan/VkRHIBackend.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <stdexcept>

#include "rg/RenderGraph.h"
#include "renderer/model_loader.h"

#include "app/debug_ui.h"
#include <imgui.h>

int main(int argc, char** argv) {
  if (argc > 1) {
    Scene scene;
    const ModelLoadResult r = loadOBJ(argv[1], scene);
    if (!r.ok) {
      printf("[model] load failed: %s\n", r.error.c_str());
      return 1;
    }
    printf("[model] %s\n", argv[1]);
    printf("  meshes:   %u\n", static_cast<unsigned>(r.meshCount));
    printf("  vertices: %u\n", static_cast<unsigned>(r.vertexCount));
    printf("  indices:  %u\n", static_cast<unsigned>(r.indexCount));
    for (const Mesh& m : scene.meshes) {
      printf("    '%s' firstIndex=%u indexCount=%u\n",
             m.name.c_str(), static_cast<unsigned>(m.firstIndex),
             static_cast<unsigned>(m.indexCount));
    }
    return 0;
  }

  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(1280, 720, "Cascade Renderer - ImGui & Vulkan", nullptr, nullptr);

  {
    Renderer renderer;
    renderer.init(RHIBackendType::Vulkan);

    auto* native = static_cast<const VkRHINativeHandles*>(renderer.getNativeHandles());

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(native->instance, window, nullptr, &surface) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create window surface.");
    }

    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    renderer.createSwapChain(static_cast<void*>(surface), fbWidth, fbHeight);

    RenderGraph& rg = renderer.graph();

    initDebugUI(renderer, window);

    uint64_t frameCount = 0;

    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      renderer.beginFrame();

      beginDebugUIFrame();
      ImGui::Begin("Cascade Renderer Debug");
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::Text("Frame: %llu", frameCount++);
      ImGui::End();
      renderDebugUI(renderer);

      renderer.endFrame();

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

    shutdownDebugUI();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
