#include "doctest/doctest.h"
#include "renderer/frame_constants.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace {

bool near(float a, float b, float eps = 1e-5f) {
  return std::fabs(a - b) <= eps;
}

void checkMat4(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f) {
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      CHECK(near(a[c][r], b[c][r], eps));
}

} // namespace

TEST_CASE("buildMeshDraws applies the mesh transform on top of view-projection") {
  Scene scene;
  scene.camera.setPosition(glm::vec3(1.0f, 2.0f, 3.0f));

  Mesh mesh;
  mesh.transform  = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, -1.0f));
  mesh.firstIndex = 7;
  mesh.indexCount = 21;
  scene.meshes.push_back(mesh);

  const auto draws = buildMeshDraws(scene);
  REQUIRE(draws.size() == 1);
  CHECK(draws[0].firstIndex == 7);
  CHECK(draws[0].indexCount == 21);
  checkMat4(draws[0].model, mesh.transform);
  checkMat4(draws[0].mvp, scene.camera.viewProjection() * mesh.transform);
}

TEST_CASE("makeLightingConstants packs camera, light and ambient") {
  Scene scene;
  scene.camera.setPosition(glm::vec3(3.0f, 4.0f, 5.0f));
  scene.sun.direction = glm::vec3(0.0f, -1.0f, 0.0f);
  scene.sun.color     = glm::vec3(0.2f, 0.3f, 0.4f);
  scene.sun.intensity = 2.5f;
  scene.ambient       = glm::vec3(0.05f, 0.06f, 0.07f);

  const LightingPC pc = makeLightingConstants(scene);

  CHECK(near(pc.lightDirToLight.x, 0.0f));
  CHECK(near(pc.lightDirToLight.y, 1.0f)); // directionToLight = -normalize(direction)
  CHECK(near(pc.lightDirToLight.z, 0.0f));
  CHECK(near(pc.lightColorIntensity.x, 0.2f));
  CHECK(near(pc.lightColorIntensity.w, 2.5f));
  CHECK(near(pc.cameraPosition.x, 3.0f));
  CHECK(near(pc.cameraPosition.w, 1.0f));
  CHECK(near(pc.ambient.z, 0.07f));
  CHECK(near(pc.ambient.w, 0.0f));

  checkMat4(pc.invViewProj * scene.camera.viewProjection(), glm::mat4(1.0f), 1e-4f);
}

TEST_CASE("makePresentConstants forwards exposure") {
  CHECK(near(makePresentConstants(0.75f).exposure, 0.75f));
  CHECK(near(makePresentConstants().exposure, 1.0f));
}
