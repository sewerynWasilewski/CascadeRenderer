#include "frame_constants.h"

std::vector<MeshDraw> buildMeshDraws(const Scene& scene) {
  const glm::mat4 viewProjection = scene.camera.viewProjection();

  std::vector<MeshDraw> draws;
  draws.reserve(scene.meshes.size());
  for (const Mesh& mesh : scene.meshes) {
    draws.push_back({ viewProjection * mesh.transform, mesh.transform,
                      mesh.firstIndex, mesh.indexCount });
  }
  return draws;
}

LightingPC makeLightingConstants(const Scene& scene) {
  LightingPC pc{};
  pc.invViewProj         = glm::inverse(scene.camera.viewProjection());
  pc.lightDirToLight     = glm::vec4(scene.sun.directionToLight(), 0.0f);
  pc.lightColorIntensity = glm::vec4(scene.sun.color, scene.sun.intensity);
  pc.cameraPosition      = glm::vec4(scene.camera.position(), 1.0f);
  pc.ambient             = glm::vec4(scene.ambient, 0.0f);
  return pc;
}

PresentPC makePresentConstants(float exposure) {
  return PresentPC{ exposure };
}
