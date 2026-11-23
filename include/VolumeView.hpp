#pragma once
// SplatDiff volume views of the detector's voxel grid.
//
//  * Ray-marched 3D view: (O_A, O_B) is uploaded as an RG 3D texture. A full-screen pass casts one ray per
//    pixel through the grid's bounding box and composites D = O_B - O_A front to back (green = added,
//    red = removed), optionally with the occupied space as a faint grey context.
//  * Slice view: one axis-aligned cross-section of the grid as a 2D heatmap image for the ImGui panel.

#include <GL/glew.h>

#include <glm/glm.hpp>
#include <memory>

#include "ChangeDetect.hpp"
#include "ShaderClass.hpp"

class VolumeView {
 public:
  VolumeView();
  ~VolumeView();

  // Upload the occupancy fields of a detection result (call after every detection).
  void upload(const splatdiff::DetectResult& result);
  bool hasData() const { return uploaded; }

  // Draw the ray-marched difference volume over the current frame (premultiplied alpha).
  void draw(const glm::mat4& proj, const glm::mat4& view, const glm::mat4& model, const glm::vec2& resolution);

  // Rebuild the slice image: axis 0/1/2 = x/y/z, position in [0, 1] along that axis.
  void updateSlice(const splatdiff::DetectResult& result, int axis, float position);
  GLuint sliceTexture() const { return sliceTex; }
  glm::ivec2 sliceSize() const { return sliceDims; }

  // transfer-function settings
  float opacity = 2.0f;     // opacity per voxel of |D| = 1
  float threshold = 0.2f;   // |D| below this is transparent
  bool showContext = true;  // faint grey for occupied, unchanged space
  float contextOpacity = 0.04f;

 private:
  std::unique_ptr<Shader> shader;
  GLuint vao = 0;
  GLuint volumeTex = 0;
  GLuint sliceTex = 0;
  bool uploaded = false;
  splatdiff::GridSpec grid;
  glm::ivec2 sliceDims{0};
};
