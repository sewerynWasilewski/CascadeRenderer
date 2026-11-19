#pragma once
#include <vector>

#include <glm/glm.hpp>

#include "push_constants.h"
#include "scene.h"

struct MeshDraw {
  glm::mat4 mvp;
  glm::mat4 model;
  u32       firstIndex;
  u32       indexCount;
};

std::vector<MeshDraw> buildMeshDraws(const Scene& scene);
LightingPC            makeLightingConstants(const Scene& scene);
PresentPC             makePresentConstants(float exposure = 1.0f);
