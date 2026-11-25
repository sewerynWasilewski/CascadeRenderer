#include "renderer/renderer.h"
#include "rhi/vulkan/VkRHIBackend.h"

#include <GLFW/glfw3.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

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

// One quad (4 vertices, CCW as seen from outside) per cube face, with a correct
// per-face normal - dedicated vertices per face (24, not 8) so each face gets flat
// shading instead of blended corner normals.
std::array<Vertex, 24> cubeVertices() {
  auto face = [](glm::vec3 n, glm::vec3 u, glm::vec3 v, int base, std::array<Vertex, 24>& out) {
    const glm::vec3 c = n * 0.5f;
    const glm::vec2 uvs[4] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
    const float s[4] = { -1, 1, 1, -1 };
    const float t[4] = { -1, -1, 1, 1 };
    for (int i = 0; i < 4; i++) {
      out[base + i] = Vertex{ c + u * (s[i] * 0.5f) + v * (t[i] * 0.5f), n, uvs[i] };
    }
  };

  std::array<Vertex, 24> verts{};
  face({ 1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 0,  verts); // +X
  face({-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 4,  verts); // -X
  face({ 0, 1, 0}, {0, 0, 1}, {1, 0, 0}, 8,  verts); // +Y
  face({ 0,-1, 0}, {1, 0, 0}, {0, 0, 1}, 12, verts); // -Y
  face({ 0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 16, verts); // +Z
  face({ 0, 0,-1}, {0, 1, 0}, {1, 0, 0}, 20, verts); // -Z
  return verts;
}

std::array<u32, 36> cubeIndices() {
  std::array<u32, 36> idx{};
  for (u32 face = 0; face < 6; face++) {
    const u32 base = face * 4;
    const u32 o = face * 6;
    idx[o + 0] = base + 0; idx[o + 1] = base + 1; idx[o + 2] = base + 2;
    idx[o + 3] = base + 0; idx[o + 4] = base + 2; idx[o + 5] = base + 3;
  }
  return idx;
}

// Uploads via map()+memcpy() on host-visible memory - no staging buffer, see issue #14.
void* uploadBuffer(IRHIBackend* backend, const void* data, u64 size, u32 usage) {
  void* buffer = backend->createBuffer(RHIBufferDesc{ size, usage });
  const RHIMemoryRequirements req = backend->getMemoryRequirements(buffer, RHI_RESOURCE_BUFFER);
  const RHIMemoryBlock block = backend->allocatePool(req.size, RHI_MEMORY_CPU_TO_GPU);
  backend->bindMemory(buffer, RHI_RESOURCE_BUFFER, block, 0);

  void* mapped = backend->map(block);
  std::memcpy(mapped, data, size);
  backend->unmap(block);

  return buffer;
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

    scene.camera.setPerspective(60.0f, 0.1f, 100.0f);
    scene.camera.setPosition({0.0f, 0.0f, 3.0f});
    scene.camera.setRotation(-90.0f, 0.0f);
    scene.camera.setAspect(static_cast<float>(fbWidth) / static_cast<float>(fbHeight));

    RenderGraph& rg = renderer.graph();
    IRHIBackend* backend = renderer.backend();

    const std::array<Vertex, 24> vertices = cubeVertices();
    const std::array<u32, 36>    indices  = cubeIndices();
    void* vertexBuffer = uploadBuffer(backend, vertices.data(), sizeof(vertices), RHI_USAGE_VERTEX_BUFFER);
    void* indexBuffer  = uploadBuffer(backend, indices.data(), sizeof(indices), RHI_USAGE_INDEX_BUFFER);

    const RHIVertexAttribute cubeAttributes[3] = {
      { 0, offsetof(Vertex, position), RHI_VERTEX_FLOAT3 },
      { 1, offsetof(Vertex, normal),   RHI_VERTEX_FLOAT3 },
      { 2, offsetof(Vertex, uv),       RHI_VERTEX_FLOAT2 },
    };
    void* cubePipeline = backend->createPipeline(RHIPipelineDesc{
      SHADER_DIR "/basic.vert.spv",
      SHADER_DIR "/basic.frag.spv",
      RHI_FORMAT_BGRA8_SRGB, // matches chooseSwapSurfaceFormat()'s preferred format - TODO:
                             // query the real swapchain format instead of assuming it
      RHI_FORMAT_UNDEFINED,   // no depth attachment - cube is convex, backface cull is enough
      sizeof(glm::mat4),
      cubeAttributes,
      3,
      sizeof(Vertex),
    });

    initDebugUI(renderer, window);

    uint64_t frameCount = 0;

    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
      if (fbWidth > 0 && fbHeight > 0)
        scene.camera.setAspect(static_cast<float>(fbWidth) / static_cast<float>(fbHeight));

      renderer.beginFrame();

      {
        void* cmdBuf = renderer.getCurrentCommandBuffer();
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), static_cast<float>(glfwGetTime()),
                                       glm::vec3(0.3f, 1.0f, 0.0f));
        glm::mat4 mvp = scene.camera.viewProjection() * model;

        // initialUsage=COLOR_ATTACHMENT: beginFrame() already transitioned this image there,
        // so RenderGraph's own entry barrier for "Draw"'s write below becomes a no-op.
        RGResourceHandle backbuffer = rg.importImage("backbuffer", RHI_MEMORY_GPU_ONLY,
          RHITextureDesc{static_cast<u32>(fbWidth), static_cast<u32>(fbHeight), 1, 1, 1, RHI_FORMAT_BGRA8_SRGB},
          backend->getCurrentSwapchainImage(), RHI_USAGE_COLOR_ATTACHMENT);

        rg.addPass("Draw", RG_PASS_RASTER,
          [&](RenderGraph::PassBuilder& b) { backbuffer = b.write(backbuffer, RHI_USAGE_COLOR_ATTACHMENT); },
          [&](RGResources&, void* cmd) {
            backend->bindPipeline(cmd, cubePipeline);
            backend->bindVertexBuffer(cmd, vertexBuffer, 0);
            backend->bindIndexBuffer(cmd, indexBuffer, 0);
            backend->pushConstants(cmd, cubePipeline, RHI_SHADER_STAGE_VERTEX, 0, sizeof(mvp), &mvp);
            backend->drawIndexed(cmd, static_cast<u32>(indices.size()), 1, 0, 0, 0);
          }
        );

        rg.compile();
        rg.plan();
        rg.allocate();
        rg.execute(cmdBuf);
        rg.reset();
      }

      beginDebugUIFrame();
      ImGui::Begin("Cascade Renderer Debug");
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::Text("Frame: %llu", frameCount++);
      ImGui::Separator();
      drawScenePanel(scene, modelPath);
      ImGui::End();
      renderDebugUI(renderer);

      renderer.endFrame();
    }

    rg.dumpJSON("graph.json");

    backend->destroyPipeline(cubePipeline);
    backend->destroyBuffer(vertexBuffer);
    backend->destroyBuffer(indexBuffer);

    shutdownDebugUI();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
