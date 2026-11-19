#pragma once
#include <string>
#include <vector>

#include "core/types.h"

struct ShaderBlob {
  std::vector<u32> code;
  std::string      error;

  bool ok() const { return error.empty() && !code.empty(); }
};

ShaderBlob loadShaderFile(const std::string& path);
