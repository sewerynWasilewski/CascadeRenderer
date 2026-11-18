#include "shader_loader.h"

#include <fstream>

namespace {
// SPIR-V modules start with this magic word in little-endian byte order.
constexpr u32 kSpirvMagic = 0x07230203u;
}

ShaderBlob loadShaderFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return ShaderBlob{ {}, path + ": cannot open" };

  const std::streamoff bytes = file.tellg();
  if (bytes <= 0) return ShaderBlob{ {}, path + ": empty file" };
  if (bytes % 4 != 0) return ShaderBlob{ {}, path + ": size is not a multiple of 4" };

  ShaderBlob blob;
  blob.code.resize(static_cast<std::size_t>(bytes / 4));
  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char*>(blob.code.data()), static_cast<std::streamsize>(bytes)))
    return ShaderBlob{ {}, path + ": read failed" };

  if (blob.code.front() != kSpirvMagic)
    return ShaderBlob{ {}, path + ": not a SPIR-V module" };

  return blob;
}
