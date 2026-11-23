#pragma once
// SplatDiff stereo / VR output.
//
// Each eye is rendered into its own framebuffer (render-to-texture), then one full-screen pass combines them:
//   * anaglyph (red/cyan glasses): left eye -> red channel, right eye -> green + blue
//   * side-by-side: left | right halves, for cross-eye free viewing or a 3D TV
//   * side-by-side + lens pre-distortion: barrel distortion r' = r (1 + k1 r^2 + k2 r^4) per eye, which cancels
//     the pincushion distortion of a Cardboard-style headset's magnifying lenses.
//
// The eye cameras themselves (off-axis or toe-in) are built by stereoEyeView() in StereoMath.hpp.

#include <GL/glew.h>

#include <glm/glm.hpp>
#include <memory>
#include <string>

#include "ShaderClass.hpp"
#include "StereoMath.hpp"

class StereoRenderer {
 public:
  StereoRenderer();
  ~StereoRenderer();

  // Size of each eye's image for a mode (side-by-side modes use half the window width).
  static glm::ivec2 eyeSize(StereoMode mode, glm::ivec2 window);

  // Bind eye `eye`'s framebuffer (resized if needed) and clear it. Render the eye's scene after this.
  void beginEye(int eye, glm::ivec2 size);
  // Back to the window framebuffer and combine both eyes.
  void composite(const StereoSettings& s, glm::ivec2 window);

 private:
  std::unique_ptr<Shader> shader;
  GLuint vao = 0;
  GLuint fbo[2] = {0, 0};
  GLuint colour[2] = {0, 0};
  glm::ivec2 size{0};
};
