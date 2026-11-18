#pragma once
#include <string>
#include <vector>

#include "core/types.h"

struct ShaderBlob {
  std::vector<u32> code;
  std::string      error;

  bool ok() const { return error.empty() && !code.empty(); }
};

// Loads a compiled SPIR-V module from disk. The caller supplies the full path
// (e.g. SHADER_DIR "/gbuffer.vert.spv"); no RHI dependency.
ShaderBlob loadShaderFile(const std::string& path);
