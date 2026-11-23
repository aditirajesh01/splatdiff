#include "Stereo.hpp"

#include <OPPCH.h>

StereoRenderer::StereoRenderer() {
  // the composite pass reuses the full-screen-triangle vertex shader of the volume view
  shader = std::make_unique<Shader>("./shaders/volume_vert.glsl", "./shaders/stereo_frag.glsl");
  glGenVertexArrays(1, &vao);
  glGenFramebuffers(2, fbo);
  glGenTextures(2, colour);
}

StereoRenderer::~StereoRenderer() {
  glDeleteFramebuffers(2, fbo);
  glDeleteTextures(2, colour);
  glDeleteVertexArrays(1, &vao);
}

glm::ivec2 StereoRenderer::eyeSize(StereoMode mode, glm::ivec2 window) {
  if (mode == StereoMode::SideBySide || mode == StereoMode::Cardboard) return glm::ivec2(window.x / 2, window.y);
  return window;
}

void StereoRenderer::beginEye(int eye, glm::ivec2 eyeSize) {
  if (eyeSize != size) {
    // (re)allocate both colour targets; splats blend without a depth buffer, so none is attached
    size = eyeSize;
    for (int i = 0; i < 2; ++i) {
      glBindTexture(GL_TEXTURE_2D, colour[i]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour[i], 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Stereo framebuffer " << i << " is incomplete" << std::endl;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, fbo[eye]);
  glViewport(0, 0, size.x, size.y);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
}

void StereoRenderer::composite(const StereoSettings& s, glm::ivec2 window) {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, window.x, window.y);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  shader->use();
  GLuint id = shader->ID;
  glUniform1i(glGetUniformLocation(id, "LeftEye"), 0);
  glUniform1i(glGetUniformLocation(id, "RightEye"), 1);
  glUniform1i(glGetUniformLocation(id, "Mode"), int(s.mode));
  glUniform1i(glGetUniformLocation(id, "SwapEyes"), s.swapEyes ? 1 : 0);
  glUniform2f(glGetUniformLocation(id, "Resolution"), float(window.x), float(window.y));
  glUniform2f(glGetUniformLocation(id, "Distortion"), s.k1, s.k2);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, colour[0]);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, colour[1]);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glEnable(GL_BLEND);
}
