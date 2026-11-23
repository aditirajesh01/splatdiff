#include "StereoMath.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

void stereoEyeView(const StereoSettings& s, int eye, const glm::vec3& position, const glm::vec3& forward,
                   const glm::vec3& up, float fovYDeg, float aspect, float nearZ, float farZ, glm::mat4& view,
                   glm::mat4& proj) {
  const glm::vec3 f = glm::normalize(forward);
  const glm::vec3 right = glm::normalize(glm::cross(f, up));
  const float side = eye == 0 ? -1.0f : 1.0f;  // left eye sits at -right
  const float half = 0.5f * s.eyeSeparation;
  const glm::vec3 eyePos = position + right * (side * half);
  const float convergence = std::max(s.convergence, nearZ * 1.01f);

  if (s.toeIn) {
    // both eyes rotate to look at the same point: symmetric frusta, but their image planes are no longer
    // parallel, which causes vertical parallax (keystone) towards the image corners
    view = glm::lookAt(eyePos, position + f * convergence, up);
    proj = glm::perspective(glm::radians(fovYDeg), aspect, nearZ, farZ);
    return;
  }
  // off-axis: parallel view directions, frustum shifted so both frusta share one rectangle at `convergence`
  view = glm::lookAt(eyePos, eyePos + f, up);
  const float top = nearZ * std::tan(glm::radians(fovYDeg) * 0.5f);
  const float halfWidth = top * aspect;
  const float shift = -side * half * nearZ / convergence;
  proj = glm::frustum(-halfWidth + shift, halfWidth + shift, -top, top, nearZ, farZ);
}
