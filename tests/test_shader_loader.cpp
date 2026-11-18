#include "doctest/doctest.h"
#include "renderer/shader_loader.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string writeTemp(const std::string& name, const std::vector<unsigned char>& bytes) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path.string();
}

} // namespace

TEST_CASE("loadShaderFile accepts a SPIR-V module") {
  const std::string path = writeTemp("cascade_valid.spv",
                                     { 0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x00, 0x00 });
  const ShaderBlob blob = loadShaderFile(path);
  REQUIRE(blob.ok());
  REQUIRE(blob.code.size() == 2);
  CHECK(blob.code[0] == 0x07230203u);
  std::filesystem::remove(path);
}

TEST_CASE("loadShaderFile rejects non-SPIR-V data") {
  const std::string path = writeTemp("cascade_bad_magic.spv", { 0x00, 0x00, 0x00, 0x00 });
  const ShaderBlob blob = loadShaderFile(path);
  CHECK_FALSE(blob.ok());
  CHECK(blob.code.empty());
  std::filesystem::remove(path);
}

TEST_CASE("loadShaderFile rejects a size that is not a multiple of 4") {
  const std::string path = writeTemp("cascade_bad_size.spv", { 0x03, 0x02, 0x23 });
  CHECK_FALSE(loadShaderFile(path).ok());
  std::filesystem::remove(path);
}

TEST_CASE("loadShaderFile reports a missing file") {
  CHECK_FALSE(loadShaderFile("cascade_missing.spv").ok());
}
