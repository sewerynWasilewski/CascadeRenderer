#include "doctest/doctest.h"
#include "renderer/model_loader.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>


namespace {

struct Loaded {
  Scene           scene;
  ModelLoadResult result;
};

Loaded load(const std::string& objText, const std::string& name = "test.obj") {
  Loaded l;
  l.result = loadOBJFromMemory(objText, name, l.scene);
  return l;
}

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
  return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(eps)));
}

int findVertex(const Scene& scene, const glm::vec3& position) {
  for (std::size_t i = 0; i < scene.vertices.size(); ++i) {
    if (near(scene.vertices[i].position, position)) return static_cast<int>(i);
  }
  return -1;
}

u32 countOutOfRange(const Scene& scene, const Mesh& mesh) {
  u32 bad = 0;
  for (u32 i = 0; i < mesh.indexCount; ++i) {
    if (scene.indices[mesh.firstIndex + i] >= scene.vertices.size()) ++bad;
  }
  return bad;
}

const char* kTriangle = R"OBJ(
v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 3
)OBJ";

} // namespace

// ─── Dedup and stream unification ────────────────────────────────────────────

TEST_CASE("a single triangle produces three vertices and three indices") {
  Loaded l = load(kTriangle);
  REQUIRE(l.result.ok);

  CHECK(l.scene.vertices.size() == 3);
  CHECK(l.scene.indices == std::vector<u32>{0, 1, 2});
  CHECK(l.scene.meshes.size() == 1);
  CHECK(l.scene.meshes[0].indexCount == 3);
  CHECK(l.scene.meshes[0].firstIndex == 0);
}

TEST_CASE("a cube dedups to 24 vertices, not 8") {
  Loaded l = load(R"OBJ(
v -1 -1 -1
v  1 -1 -1
v  1  1 -1
v -1  1 -1
v -1 -1  1
v  1 -1  1
v  1  1  1
v -1  1  1
vn  0  0 -1
vn  1  0  0
vn  0  0  1
vn -1  0  0
vn  0  1  0
vn  0 -1  0
f 1//1 4//1 3//1
f 1//1 3//1 2//1
f 2//2 3//2 7//2
f 2//2 7//2 6//2
f 6//3 7//3 8//3
f 6//3 8//3 5//3
f 5//4 8//4 4//4
f 5//4 4//4 1//4
f 4//5 8//5 7//5
f 4//5 7//5 3//5
f 1//6 2//6 6//6
f 1//6 6//6 5//6
)OBJ");
  REQUIRE(l.result.ok);

  CHECK(l.scene.vertices.size() == 24);
  CHECK(l.scene.indices.size() == 36);
  CHECK(l.scene.meshes[0].indexCount == 36);
}

TEST_CASE("the three index streams are read independently") {
  Loaded l = load(R"OBJ(
v 0 0 0
v 1 0 0
v 0 1 0
vn 0 0 1
vn 0 1 0
vn 1 0 0
f 1//2 2//3 3//1
)OBJ");
  REQUIRE(l.result.ok);

  const int a = findVertex(l.scene, {0.0f, 0.0f, 0.0f});
  const int b = findVertex(l.scene, {1.0f, 0.0f, 0.0f});
  const int c = findVertex(l.scene, {0.0f, 1.0f, 0.0f});
  REQUIRE(a >= 0);
  REQUIRE(b >= 0);
  REQUIRE(c >= 0);

  CHECK(near(l.scene.vertices[a].normal, {0.0f, 1.0f, 0.0f}));
  CHECK(near(l.scene.vertices[b].normal, {1.0f, 0.0f, 0.0f}));
  CHECK(near(l.scene.vertices[c].normal, {0.0f, 0.0f, 1.0f}));
}

// ─── Absent attributes ──────────────────────────────────────────────────────

TEST_CASE("missing texcoords default to zero") {
  Loaded l = load(kTriangle);
  REQUIRE(l.result.ok);

  u32 nonZero = 0;
  for (const Vertex& v : l.scene.vertices) {
    if (v.uv != glm::vec2(0.0f)) ++nonZero;
  }
  CHECK(nonZero == 0);
}

TEST_CASE("missing normals are generated with unit length and correct winding") {
  Loaded l = load(kTriangle);
  REQUIRE(l.result.ok);

  u32 badLength = 0;
  u32 wrongSide = 0;
  for (const Vertex& v : l.scene.vertices) {
    if (std::abs(glm::length(v.normal) - 1.0f) > 1e-4f) ++badLength;
    if (!near(v.normal, {0.0f, 0.0f, 1.0f})) ++wrongSide;
  }
  CHECK(badLength == 0);
  CHECK(wrongSide == 0);
}

TEST_CASE("generated normals are area weighted") {
  Loaded l = load(R"OBJ(
v 0 0 0
v 10 0 0
v 0 10 0
v 0.1 0 0
v 0 0 -0.1
f 1 2 3
f 1 4 5
)OBJ");
  REQUIRE(l.result.ok);

  const int shared = findVertex(l.scene, {0.0f, 0.0f, 0.0f});
  REQUIRE(shared >= 0);
  CHECK(l.scene.vertices.size() == 5);

  const glm::vec3 n = l.scene.vertices[shared].normal;
  CHECK(glm::dot(n, {0.0f, 0.0f, 1.0f}) > glm::dot(n, {0.0f, 1.0f, 0.0f}));
}

TEST_CASE("a degenerate face does not produce NaN normals") {
  Loaded l = load(R"OBJ(
v 0 0 0
v 1 0 0
v 2 0 0
f 1 2 3
)OBJ");
  REQUIRE(l.result.ok);

  u32 bad = 0;
  for (const Vertex& v : l.scene.vertices) {
    const bool finite = std::isfinite(v.normal.x) && std::isfinite(v.normal.y) && std::isfinite(v.normal.z);
    if (!finite || glm::length(v.normal) <= 0.0f) ++bad;
  }
  CHECK(bad == 0);
}

// ─── Triangulation ──────────────────────────────────────────────────────────

TEST_CASE("a quad triangulates into two triangles") {
  Loaded l = load(R"OBJ(
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
f 1 2 3 4
)OBJ");
  REQUIRE(l.result.ok);

  CHECK(l.scene.vertices.size() == 4);
  CHECK(l.scene.indices.size() == 6);
  CHECK(l.scene.indices.size() % 3 == 0);
}

TEST_CASE("a pentagon triangulates without defeating dedup") {
  Loaded l = load(R"OBJ(
v 0 0 0
v 1 0 0
v 2 1 0
v 1 2 0
v 0 2 0
f 1 2 3 4 5
)OBJ");
  REQUIRE(l.result.ok);

  CHECK(l.scene.indices.size() == 9);
  CHECK(l.scene.vertices.size() == 5);
}

// ─── Index forms ────────────────────────────────────────────────────────────

TEST_CASE("negative relative indices resolve like absolute ones") {
  Loaded relative = load(R"OBJ(
v 0 0 0
v 1 0 0
v 0 1 0
f -3 -2 -1
)OBJ");
  Loaded absolute = load(kTriangle);
  REQUIRE(relative.result.ok);
  REQUIRE(absolute.result.ok);

  CHECK(relative.scene.indices == absolute.scene.indices);
  CHECK(relative.scene.vertices.size() == absolute.scene.vertices.size());
}

TEST_CASE("v/t face format extracts uv and generates normals") {
  Loaded l = load(R"OBJ(
v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
vt 1 0
vt 0 1
f 1/1 2/2 3/3
)OBJ");
  REQUIRE(l.result.ok);

  REQUIRE(l.scene.vertices.size() == 3);
  const int right = findVertex(l.scene, {1.0f, 0.0f, 0.0f});
  REQUIRE(right >= 0);
  CHECK(l.scene.vertices[right].uv == glm::vec2(1.0f, 0.0f));

  u32 wrongNormal = 0;
  for (const Vertex& v : l.scene.vertices) {
    if (!near(v.normal, {0.0f, 0.0f, 1.0f})) ++wrongNormal;
  }
  CHECK(wrongNormal == 0);
}

TEST_CASE("v/t/n face format reads all three streams independently") {
  Loaded l = load(R"OBJ(
v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
vt 1 0
vt 0 1
vn 0 0 1
vn 0 1 0
vn 1 0 0
f 1/1/2 2/2/3 3/3/1
)OBJ");
  REQUIRE(l.result.ok);

  REQUIRE(l.scene.vertices.size() == 3);

  const int a = findVertex(l.scene, {0.0f, 0.0f, 0.0f});
  const int b = findVertex(l.scene, {1.0f, 0.0f, 0.0f});
  const int c = findVertex(l.scene, {0.0f, 1.0f, 0.0f});
  REQUIRE(a >= 0);
  REQUIRE(b >= 0);
  REQUIRE(c >= 0);

  CHECK(l.scene.vertices[a].uv == glm::vec2(0.0f, 0.0f));
  CHECK(l.scene.vertices[b].uv == glm::vec2(1.0f, 0.0f));
  CHECK(l.scene.vertices[c].uv == glm::vec2(0.0f, 1.0f));

  CHECK(near(l.scene.vertices[a].normal, {0.0f, 1.0f, 0.0f}));
  CHECK(near(l.scene.vertices[b].normal, {1.0f, 0.0f, 0.0f}));
  CHECK(near(l.scene.vertices[c].normal, {0.0f, 0.0f, 1.0f}));
}

TEST_CASE("CRLF line endings parse the same as LF") {
  const std::string crlf =
    "v 0 0 0\r\n"
    "v 1 0 0\r\n"
    "v 0 1 0\r\n"
    "f 1 2 3\r\n";

  Loaded actual = load(crlf);
  Loaded lf     = load(kTriangle);
  REQUIRE(actual.result.ok);
  REQUIRE(lf.result.ok);

  CHECK(actual.scene.vertices.size() == lf.scene.vertices.size());
  CHECK(actual.scene.indices == lf.scene.indices);
}

TEST_CASE("comments and an mtllib directive do not disturb parsing") {
  Loaded l = load(R"OBJ(
# a comment
mtllib missing_material_file.mtl
usemtl some_material
v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 3
)OBJ");
  REQUIRE(l.result.ok);

  CHECK(l.scene.vertices.size() == 3);
  CHECK(l.scene.indices.size() == 3);
}

// ─── Multi-shape bookkeeping ────────────────────────────────────────────────

TEST_CASE("each shape becomes one Mesh over its own index range") {
  Loaded l = load(R"OBJ(
o A
v 0 0 0
v 1 0 0
v 0 1 0
vn 0 0 1
f 1//1 2//1 3//1
o B
v 10 0 0
v 11 0 0
v 10 1 0
vn 0 0 1
f 4//2 5//2 6//2
)OBJ");
  REQUIRE(l.result.ok);

  REQUIRE(l.scene.meshes.size() == 2);
  CHECK(l.scene.meshes[0].name == "A");
  CHECK(l.scene.meshes[1].name == "B");

  CHECK(l.scene.meshes[0].firstIndex == 0);
  CHECK(l.scene.meshes[0].indexCount == 3);
  CHECK(l.scene.meshes[1].firstIndex == 3);
  CHECK(l.scene.meshes[1].indexCount == 3);

  CHECK(countOutOfRange(l.scene, l.scene.meshes[0]) == 0);
  CHECK(countOutOfRange(l.scene, l.scene.meshes[1]) == 0);

  u32 foreign = 0;
  for (u32 i = 0; i < l.scene.meshes[1].indexCount; ++i) {
    if (l.scene.indices[l.scene.meshes[1].firstIndex + i] < 3) ++foreign;
  }
  CHECK(foreign == 0);
}

TEST_CASE("a shape with no faces produces no Mesh") {
  Loaded l = load(R"OBJ(
o empty
o real
v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 3
)OBJ");
  REQUIRE(l.result.ok);

  CHECK(l.scene.meshes.size() == 1);
  CHECK(l.scene.meshes[0].name == "real");
}

TEST_CASE("loaded meshes get an identity transform") {
  Loaded l = load(kTriangle);
  REQUIRE(l.result.ok);

  CHECK(l.scene.meshes[0].transform == glm::mat4(1.0f));
  CHECK(l.scene.meshes[0].valid());
  CHECK(l.scene.meshes[0].sourcePath == "test.obj");
}

TEST_CASE("instancing shares geometry by copying a Mesh") {
  Loaded l = load(kTriangle);
  REQUIRE(l.result.ok);

  Mesh copy = l.scene.meshes[0];
  copy.transform = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
  l.scene.meshes.push_back(copy);

  CHECK(l.scene.meshes.size() == 2);
  CHECK(l.scene.meshes[0].firstIndex == l.scene.meshes[1].firstIndex);
  CHECK(l.scene.meshes[0].indexCount == l.scene.meshes[1].indexCount);
  CHECK(l.scene.vertices.size() == 3); // no duplicate geometry
}

// ─── Append semantics ───────────────────────────────────────────────────────

TEST_CASE("loading twice appends and leaves the first mesh's range intact") {
  Scene scene;
  REQUIRE(loadOBJFromMemory(kTriangle, "test.obj", scene).ok);
  REQUIRE(loadOBJFromMemory(kTriangle, "test.obj", scene).ok);

  REQUIRE(scene.meshes.size() == 2);
  CHECK(scene.vertices.size() == 6);
  CHECK(scene.indices.size() == 6);

  CHECK(scene.meshes[0].firstIndex == 0);
  CHECK(scene.meshes[1].firstIndex == 3);

  u32 fromFirstLoad = 0;
  for (u32 i = 0; i < scene.meshes[1].indexCount; ++i) {
    if (scene.indices[scene.meshes[1].firstIndex + i] < 3) ++fromFirstLoad;
  }
  CHECK(fromFirstLoad == 0);

  CHECK(countOutOfRange(scene, scene.meshes[0]) == 0);
  CHECK(countOutOfRange(scene, scene.meshes[1]) == 0);
}

// ─── Failure modes ──────────────────────────────────────────────────────────

TEST_CASE("a missing file fails without touching the Scene") {
  Scene scene;
  const ModelLoadResult r = loadOBJ("no_such_file_xyz.obj", scene);

  CHECK_FALSE(r.ok);
  CHECK_FALSE(r.error.empty());
  CHECK(scene.vertices.empty());
  CHECK(scene.indices.empty());
  CHECK(scene.meshes.empty());
}

TEST_CASE("empty source text fails") {
  Loaded l = load("");
  CHECK_FALSE(l.result.ok);
  CHECK_FALSE(l.result.error.empty());
}

TEST_CASE("vertices with no faces fail") {
  Loaded l = load(R"OBJ(
v 0 0 0
v 1 0 0
v 0 1 0
)OBJ");
  CHECK_FALSE(l.result.ok);
  CHECK(l.scene.meshes.empty());
}

TEST_CASE("an out-of-range vertex index fails instead of reading out of bounds") {
  Loaded l = load(R"OBJ(
v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 99
)OBJ");

  CHECK_FALSE(l.result.ok);
  CHECK_FALSE(l.result.error.empty());
  CHECK(l.scene.vertices.empty());
  CHECK(l.scene.indices.empty());
}

TEST_CASE("a failed load leaves a populated Scene untouched") {
  Scene scene;
  REQUIRE(loadOBJFromMemory(kTriangle, "test.obj", scene).ok);

  const std::size_t vertices = scene.vertices.size();
  const std::size_t indices  = scene.indices.size();
  const std::size_t meshes   = scene.meshes.size();
  const u32         first    = scene.meshes[0].firstIndex;
  const u32         count    = scene.meshes[0].indexCount;

  const ModelLoadResult r = loadOBJFromMemory("f 1 2 99", "bad.obj", scene);
  CHECK_FALSE(r.ok);

  CHECK(scene.vertices.size() == vertices);
  CHECK(scene.indices.size() == indices);
  CHECK(scene.meshes.size() == meshes);
  CHECK(scene.meshes[0].firstIndex == first);
  CHECK(scene.meshes[0].indexCount == count);
}

// ─── File round trip ────────────────────────────────────────────────────────

TEST_CASE("loadOBJ reads a real file from disk") {
  const std::filesystem::path path =
    std::filesystem::temp_directory_path() / "cascade_loader_roundtrip.obj";
  {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out << kTriangle;
  }

  Scene scene;
  const ModelLoadResult r = loadOBJ(path.string(), scene);

  std::error_code ec;
  std::filesystem::remove(path, ec);

  REQUIRE(r.ok);
  CHECK(scene.vertices.size() == 3);
  CHECK(scene.meshes.size() == 1);
  CHECK(scene.meshes[0].sourcePath == path.string());
}
