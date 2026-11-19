#pragma once
#include <cstddef>
#include <type_traits>

#include <glm/glm.hpp>

struct GbufferPC {
  glm::mat4 mvp;
  glm::mat4 model;
};

struct LightingPC {
  glm::mat4 invViewProj;
  glm::vec4 lightDirToLight;
  glm::vec4 lightColorIntensity;
  glm::vec4 cameraPosition;
  glm::vec4 ambient;
};

struct PresentPC {
  float exposure;
};

static_assert(sizeof(glm::mat4) == 64, "glm::mat4 must be 16 tightly packed floats");
static_assert(sizeof(glm::vec4) == 16, "glm::vec4 must be 4 tightly packed floats");

static_assert(std::is_standard_layout_v<GbufferPC>);
static_assert(sizeof(GbufferPC) == 128);
static_assert(offsetof(GbufferPC, mvp) == 0);
static_assert(offsetof(GbufferPC, model) == 64);

static_assert(std::is_standard_layout_v<LightingPC>);
static_assert(sizeof(LightingPC) == 128);
static_assert(offsetof(LightingPC, invViewProj) == 0);
static_assert(offsetof(LightingPC, lightDirToLight) == 64);
static_assert(offsetof(LightingPC, lightColorIntensity) == 80);
static_assert(offsetof(LightingPC, cameraPosition) == 96);
static_assert(offsetof(LightingPC, ambient) == 112);

static_assert(std::is_standard_layout_v<PresentPC>);
static_assert(sizeof(PresentPC) == 4);
