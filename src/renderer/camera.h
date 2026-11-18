#pragma once
#include <glm/glm.hpp>

class PerspectiveCamera {
public:
  PerspectiveCamera() = default;
  PerspectiveCamera(float fovYDegrees, float aspect, float nearPlane, float farPlane);

  // --- position ---
  void                     setPosition(const glm::vec3& position) { mPosition = position; }
  const glm::vec3&         position() const { return mPosition; }

  // --- orientation (degrees) ---
  void  setRotation(float yawDegrees, float pitchDegrees);
  void  addRotation(float yawDeltaDegrees, float pitchDeltaDegrees);
  float yaw() const { return mYaw; }
  float pitch() const { return mPitch; }

  // --- projection ---
  void  setPerspective(float fovYDegrees, float nearPlane, float farPlane);
  void  setAspect(float aspect);
  float fovY() const { return mFovY; }
  float aspect() const { return mAspect; }
  float nearPlane() const { return mNear; }
  float farPlane() const { return mFar; }

  glm::vec3 forward() const;
  glm::vec3 right() const;
  glm::vec3 up() const;

  glm::mat4 view() const;
  glm::mat4 projection() const;
  glm::mat4 viewProjection() const { return projection() * view(); }

private:
  glm::vec3 mPosition{0.0f, 0.0f, 3.0f};
  float     mYaw   = -90.0f;
  float     mPitch = 0.0f;

  float mFovY   = 60.0f;
  float mAspect = 16.0f / 9.0f;
  float mNear   = 0.1f;
  float mFar    = 1000.0f;
};
