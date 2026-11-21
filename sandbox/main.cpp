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

    PerspectiveCamera camera(60.0f, static_cast<float>(fbWidth) / static_cast<float>(fbHeight), 0.1f, 100.0f);
    camera.setPosition({0.0f, 0.0f, 3.0f});
    camera.setRotation(-90.0f, 0.0f);

    initDebugUI(renderer, window);

    uint64_t frameCount = 0;

    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      renderer.beginFrame();

      {
        void* cmdBuf = renderer.getCurrentCommandBuffer();
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), static_cast<float>(glfwGetTime()),
                                       glm::vec3(0.3f, 1.0f, 0.0f));
        glm::mat4 mvp = camera.viewProjection() * model;

        backend->beginPass(cmdBuf, RHI_PASS_RASTER, backend->getCurrentSwapchainImage());
        backend->bindPipeline(cmdBuf, cubePipeline);
        backend->bindVertexBuffer(cmdBuf, vertexBuffer, 0);
        backend->bindIndexBuffer(cmdBuf, indexBuffer, 0);
        backend->pushConstants(cmdBuf, cubePipeline, RHI_SHADER_STAGE_VERTEX, 0, sizeof(mvp), &mvp);
        backend->drawIndexed(cmdBuf, static_cast<u32>(indices.size()), 1, 0, 0, 0);
        backend->endPass(cmdBuf);
      }

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

    backend->destroyPipeline(cubePipeline);
    backend->destroyBuffer(vertexBuffer);
    backend->destroyBuffer(indexBuffer);

    shutdownDebugUI();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
