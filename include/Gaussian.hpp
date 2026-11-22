#pragma once

#include <glm/glm.hpp>

// One 3D Gaussian as uploaded to the GPU (interleaved vertex attributes, see GaussianSplat).
// The 3x3 covariance is symmetric, so only 6 values are stored:
//   covA = (S00, S01, S02), covB = (S11, S12, S22)
struct GaussianSphere {
  glm::vec3 position;
  glm::vec3 color;  // linear RGB from the SH DC term: 0.5 + C0 * f_dc
  float opacity;    // after sigmoid, in (0, 1)
  glm::vec3 covA;
  glm::vec3 covB;
  int index;  // row in the source .ply, or -1 for Gaussians created by an edit
};
