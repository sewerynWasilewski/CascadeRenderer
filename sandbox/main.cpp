#include "renderer/renderer.h"
#include "rhi/vulkan/VkRHIBackend.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "rg/RenderGraph.h"
#include "renderer/model_loader.h"

#include "app/debug_ui.h"
#include <imgui.h>

namespace {

void printSceneStats(const char* path, const Scene& scene) {
  printf("[model] %s\n", path);
  printf("  meshes:   %u\n", static_cast<unsigned>(scene.meshes.size()));
  printf("  vertices: %u\n", static_cast<unsigned>(scene.vertices.size()));
  printf("  indices:  %u\n", static_cast<unsigned>(scene.indices.size()));
  for (const Mesh& m : scene.meshes) {
    printf("    '%s' firstIndex=%u indexCount=%u\n",
           m.name.c_str(), static_cast<unsigned>(m.firstIndex),
           static_cast<unsigned>(m.indexCount));
  }
}

void drawScenePanel(Scene& scene, const char* modelPath) {
  ImGui::Text("Scene: %s", modelPath ? modelPath : "(empty)");
  ImGui::Text("Meshes: %u  Vertices: %u  Indices: %u",
              static_cast<unsigned>(scene.meshes.size()),
              static_cast<unsigned>(scene.vertices.size()),
              static_cast<unsigned>(scene.indices.size()));

  if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
    glm::vec3 position = scene.camera.position();
    if (ImGui::DragFloat3("Position", &position.x, 0.05f)) scene.camera.setPosition(position);

    float yaw   = scene.camera.yaw();
    float pitch = scene.camera.pitch();
    bool changed = ImGui::DragFloat("Yaw", &yaw, 0.5f);
    changed |= ImGui::DragFloat("Pitch", &pitch, 0.5f);
    if (changed) scene.camera.setRotation(yaw, pitch);

    float fov = scene.camera.fovY();
    if (ImGui::DragFloat("FOV", &fov, 0.1f, 1.0f, 179.0f))
      scene.camera.setPerspective(fov, scene.camera.nearPlane(), scene.camera.farPlane());

    ImGui::Text("Aspect: %.3f", scene.camera.aspect());
  }

  if (ImGui::CollapsingHeader("Directional light", ImGuiTreeNodeFlags_DefaultOpen)) {
    glm::vec3 direction = scene.sun.direction;
    if (ImGui::DragFloat3("Direction", &direction.x, 0.01f)) {
      // Avoid a zero vector: directionToLight() normalizes it.
      if (glm::dot(direction, direction) > 1e-8f) scene.sun.direction = direction;
    }
    ImGui::ColorEdit3("Color", &scene.sun.color.x);
    ImGui::DragFloat("Intensity", &scene.sun.intensity, 0.01f, 0.0f, 100.0f);
    ImGui::ColorEdit3("Ambient", &scene.ambient.x);
  }
}

} // namespace

int main(int argc, char** argv) {
  const char* modelPath = nullptr;
  bool infoOnly = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--info") == 0) infoOnly = true;
    else if (!modelPath) modelPath = argv[i];
  }

  Scene scene;
  if (modelPath) {
    const ModelLoadResult r = loadOBJ(modelPath, scene);
    if (!r.ok) {
      fprintf(stderr, "[model] load failed: %s\n", r.error.c_str());
      return 1;
    }
    printSceneStats(modelPath, scene);
  }
  if (infoOnly) return 0;

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
    scene.camera.setAspect(static_cast<float>(fbWidth) / static_cast<float>(fbHeight));

    RenderGraph& rg = renderer.graph();

    initDebugUI(renderer, window);

    uint64_t frameCount = 0;

    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
      if (fbWidth > 0 && fbHeight > 0)
        scene.camera.setAspect(static_cast<float>(fbWidth) / static_cast<float>(fbHeight));

      renderer.beginFrame();

      beginDebugUIFrame();
      ImGui::Begin("Cascade Renderer Debug");
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::Text("Frame: %llu", frameCount++);
      ImGui::Separator();
      drawScenePanel(scene, modelPath);
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
