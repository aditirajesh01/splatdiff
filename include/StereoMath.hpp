#pragma once
// SplatDiff stereo cameras (no OpenGL, so it can be unit tested).

#include <glm/glm.hpp>

enum class StereoMode {
  Off = 0,
  AnaglyphColour = 1,  // left R, right GB: keeps some colour
  AnaglyphGrey = 2,    // left luminance in R: less retinal rivalry on red/green content
  SideBySide = 3,      // cross-eye / 3D TV
  Cardboard = 4        // side-by-side with barrel lens pre-distortion
};

struct StereoSettings {
  StereoMode mode = StereoMode::Off;
  float eyeSeparation = 0.15f;  // inter-ocular distance in scene units
  float convergence = 7.0f;     // zero-parallax distance: objects here appear at screen depth
  bool toeIn = false;           // rotate both eyes towards the convergence point (wrong, for comparison)
  bool swapEyes = false;        // side-by-side: right image on the left, for cross-eye viewing
  float k1 = 0.22f;             // lens distortion coefficients
  float k2 = 0.24f;
};

// View and projection for one eye (eye 0 = left, 1 = right) from a mono camera.
//   Off-axis: both eyes look straight ahead (parallel axes, no vertical parallax); each frustum is shifted
//   sideways by (e/2) * near / convergence so the two frusta coincide at the convergence distance.
//   Toe-in: symmetric frusta rotated to look at the convergence point; introduces vertical parallax.
void stereoEyeView(const StereoSettings& s, int eye, const glm::vec3& position, const glm::vec3& forward,
                   const glm::vec3& up, float fovYDeg, float aspect, float nearZ, float farZ, glm::mat4& view,
                   glm::mat4& proj);
