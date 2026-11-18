#include "camera.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr float kMaxPitch = 89.0f;
const glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
}

PerspectiveCamera::PerspectiveCamera(float fovYDegrees, float aspect, float nearPlane, float farPlane)
  : mFovY(fovYDegrees), mAspect(aspect), mNear(nearPlane), mFar(farPlane) {}

void PerspectiveCamera::setRotation(float yawDegrees, float pitchDegrees) {
  mYaw   = yawDegrees;
  mPitch = std::clamp(pitchDegrees, -kMaxPitch, kMaxPitch);
}

void PerspectiveCamera::addRotation(float yawDeltaDegrees, float pitchDeltaDegrees) {
  setRotation(mYaw + yawDeltaDegrees, mPitch + pitchDeltaDegrees);
}

void PerspectiveCamera::setPerspective(float fovYDegrees, float nearPlane, float farPlane) {
  mFovY = fovYDegrees;
  mNear = std::max(nearPlane, 1e-4f);
  mFar  = std::max(farPlane, mNear + 1e-4f);
}

void PerspectiveCamera::setAspect(float aspect) {
  mAspect = std::max(aspect, 1e-4f);
}

glm::vec3 PerspectiveCamera::forward() const {
  const float yawRad   = glm::radians(mYaw);
  const float pitchRad = glm::radians(mPitch);
  const float cosPitch = std::cos(pitchRad);

  return glm::normalize(glm::vec3{std::cos(yawRad) * cosPitch,
                                  std::sin(pitchRad),
                                  std::sin(yawRad) * cosPitch});
}

glm::vec3 PerspectiveCamera::right() const {
  return glm::normalize(glm::cross(forward(), kWorldUp));
}

glm::vec3 PerspectiveCamera::up() const {
  return glm::normalize(glm::cross(right(), forward()));
}

glm::mat4 PerspectiveCamera::view() const {
  return glm::lookAt(mPosition, mPosition + forward(), up());
}

glm::mat4 PerspectiveCamera::projection() const {
  glm::mat4 proj = glm::perspective(glm::radians(mFovY), mAspect, mNear, mFar);

  proj[1][1] *= -1.0f;

  return proj;
}
