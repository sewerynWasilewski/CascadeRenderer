#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "camera.h"
#include "gpu_types.h"

struct DirectionalLight {
  glm::vec3 direction{0.0f, -1.0f, 0.0f}; // direction the light travels
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float     intensity = 1.0f;

  glm::vec3 directionToLight() const { return -glm::normalize(direction); }
};

struct Mesh {
  std::string name;
  std::string sourcePath; // file the loader will read
  glm::mat4   transform{1.0f};

  u32 firstIndex = 0;
  u32 indexCount = 0;

  bool valid() const { return indexCount > 0; }
};

struct Scene {
  PerspectiveCamera          camera;
  DirectionalLight           sun;
  std::vector<Mesh>          meshes;
  glm::vec3                  ambient{0.03f, 0.03f, 0.04f};
};
