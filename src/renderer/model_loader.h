#pragma once
#include <string>

#include "scene.h"

struct ModelLoadResult {
  bool        ok = false;
  std::string error;

  u32 meshCount   = 0;
  u32 vertexCount = 0;
  u32 indexCount  = 0;
};

ModelLoadResult loadOBJ(const std::string& path, Scene& scene);

ModelLoadResult loadOBJFromMemory(const std::string& objText,
                                  const std::string& sourceName,
                                  Scene& scene);
