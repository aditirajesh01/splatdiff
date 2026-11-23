#include "VolumeView.hpp"

#include <OPPCH.h>

#include <algorithm>

VolumeView::VolumeView() {
  shader = std::make_unique<Shader>("./shaders/volume_vert.glsl", "./shaders/volume_frag.glsl");
  glGenVertexArrays(1, &vao);  // core profile needs a bound VAO even without vertex buffers
  glGenTextures(1, &volumeTex);
  glGenTextures(1, &sliceTex);

  glBindTexture(GL_TEXTURE_3D, volumeTex);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_3D, 0);

  glBindTexture(GL_TEXTURE_2D, sliceTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);
}

VolumeView::~VolumeView() {
  glDeleteTextures(1, &volumeTex);
  glDeleteTextures(1, &sliceTex);
  glDeleteVertexArrays(1, &vao);
}

void VolumeView::upload(const splatdiff::DetectResult& r) {
  grid = r.grid;
  const size_t n = grid.count();
  std::vector<float> rg(2 * n);
  for (size_t v = 0; v < n; ++v) {
    rg[2 * v] = r.fieldA[v];
    rg[2 * v + 1] = r.fieldB[v];
  }
  glBindTexture(GL_TEXTURE_3D, volumeTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  // x fastest, then y, then z: the same layout as GridSpec::index
  glTexImage3D(GL_TEXTURE_3D, 0, GL_RG16F, grid.dims.x, grid.dims.y, grid.dims.z, 0, GL_RG, GL_FLOAT, rg.data());
  glBindTexture(GL_TEXTURE_3D, 0);
  uploaded = true;
}

void VolumeView::draw(const glm::mat4& proj, const glm::mat4& view, const glm::mat4& model,
                      const glm::vec2& resolution) {
  if (!uploaded) return;
  shader->use();
  GLuint id = shader->ID;
  glm::vec3 boxMin = grid.origin;
  glm::vec3 boxMax = grid.origin + glm::vec3(grid.dims) * grid.voxel;
  glUniformMatrix4fv(glGetUniformLocation(id, "InvViewProj"), 1, GL_FALSE, glm::value_ptr(glm::inverse(proj * view)));
  glUniformMatrix4fv(glGetUniformLocation(id, "InvModel"), 1, GL_FALSE, glm::value_ptr(glm::inverse(model)));
  glUniform3fv(glGetUniformLocation(id, "BoxMin"), 1, glm::value_ptr(boxMin));
  glUniform3fv(glGetUniformLocation(id, "BoxMax"), 1, glm::value_ptr(boxMax));
  glUniform2fv(glGetUniformLocation(id, "Resolution"), 1, glm::value_ptr(resolution));
  glUniform1f(glGetUniformLocation(id, "StepSize"), 0.5f * grid.voxel);
  glUniform1f(glGetUniformLocation(id, "Voxel"), grid.voxel);
  glUniform1f(glGetUniformLocation(id, "Opacity"), opacity);
  glUniform1f(glGetUniformLocation(id, "Threshold"), threshold);
  glUniform1i(glGetUniformLocation(id, "ShowContext"), showContext ? 1 : 0);
  glUniform1f(glGetUniformLocation(id, "ContextOpacity"), contextOpacity);
  glUniform1i(glGetUniformLocation(id, "Occupancy"), 0);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, volumeTex);
  glEnable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // premultiplied "over"
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_3D, 0);
}

void VolumeView::updateSlice(const splatdiff::DetectResult& r, int axis, float position) {
  const glm::ivec3 D = r.grid.dims;
  // image axes (u, v) for each slicing axis: y-slices are a top-down map (x right, z down)
  int ua = axis == 0 ? 2 : 0;
  int va = axis == 1 ? 2 : 1;
  int k = std::clamp(int(position * (D[axis] - 1) + 0.5f), 0, D[axis] - 1);
  sliceDims = glm::ivec2(D[ua], D[va]);
  std::vector<unsigned char> rgba(4 * size_t(sliceDims.x) * sliceDims.y);
  for (int v = 0; v < sliceDims.y; ++v)
    for (int u = 0; u < sliceDims.x; ++u) {
      // image row 0 is drawn at the top: flip v for side views so "up" stays up
      int vv = axis == 1 ? v : sliceDims.y - 1 - v;
      glm::ivec3 c(0);
      c[axis] = k;
      c[ua] = u;
      c[va] = vv;
      size_t i = r.grid.index(c.x, c.y, c.z);
      float oa = r.fieldA[i], ob = r.fieldB[i];
      glm::vec3 col(0.06f + 0.4f * std::max(oa, ob));  // occupied space in grey
      if (r.label[i] > 0) col = glm::vec3(0.2f, 1.0f, 0.3f);
      if (r.label[i] < 0) col = glm::vec3(1.0f, 0.2f, 0.2f);
      unsigned char* px = &rgba[4 * (size_t(v) * sliceDims.x + u)];
      px[0] = (unsigned char)(255 * glm::clamp(col.r, 0.0f, 1.0f));
      px[1] = (unsigned char)(255 * glm::clamp(col.g, 0.0f, 1.0f));
      px[2] = (unsigned char)(255 * glm::clamp(col.b, 0.0f, 1.0f));
      px[3] = 255;
    }
  glBindTexture(GL_TEXTURE_2D, sliceTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sliceDims.x, sliceDims.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glBindTexture(GL_TEXTURE_2D, 0);
}
