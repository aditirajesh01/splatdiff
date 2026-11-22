#pragma once
// SplatDiff change detection: compare two Gaussian scans through a colour-free voxel occupancy field.
//
//   Kernel 1  splat    Phi(v) = sum_i a_i exp(-1/2 (v - mu_i)^T Sigma_i^-1 (v - mu_i)),  O(v) = 1 - exp(-Phi(v))
//   Kernel 2  compare  added   if O_B(v) - max_{N(v)} O_A > tau
//                      removed if O_A(v) - max_{N(v)} O_B > tau
//   Kernel 3  regions  26-connected components of changed voxels -> boxes, typed added / removed / moved
//
// Only position, shape and opacity are used; colour never enters, so lighting changes cannot be detected.

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "SceneEdit.hpp"

namespace splatdiff {

enum class ExecMode {
  Serial = 0,        // plain loops
  OpenMPAtomic = 1,  // one thread per Gaussian, atomic adds into the shared grid
  OpenMPPrivate = 2  // per-thread private grids, then a parallel reduction (no atomics, more memory)
};
const char* execModeName(ExecMode m);
int maxThreads();  // OpenMP threads available (1 if built without OpenMP)

struct GridSpec {
  glm::vec3 origin{0.0f};  // corner of voxel (0,0,0), file/model space
  float voxel = 1.0f;      // voxel edge length
  glm::ivec3 dims{1};

  size_t count() const { return size_t(dims.x) * dims.y * dims.z; }
  size_t index(int x, int y, int z) const { return size_t(x) + size_t(dims.x) * (size_t(y) + size_t(dims.y) * z); }
  glm::vec3 centre(int x, int y, int z) const { return origin + (glm::vec3(x, y, z) + 0.5f) * voxel; }
};

struct DetectParams {
  int resolution = 128;           // voxels along the longest axis of the scene
  float tau = 0.35f;              // occupancy-difference threshold
  int tolerance = 1;              // neighbourhood radius (voxels) for the max filter; 0 = plain |D| > tau
  int minVoxels = 8;              // smaller connected components are dropped as noise
  float boundsPercentile = 0.5f;  // crop the grid to [p, 100-p] percentile of positions (ignores far floaters)
  bool occupancy = true;          // compare O = 1 - exp(-Phi) (true) or the raw density Phi (false)
  bool antiAlias = true;          // add a voxel-sized low-pass filter to every Gaussian (mass preserving)
  float cutoffSigma = 3.0f;       // evaluate each Gaussian out to this many standard deviations
  ExecMode mode = ExecMode::OpenMPAtomic;
};

struct ChangeRegion {
  std::string type;  // "added", "removed", "moved" (paired), or "changed" (mixed signs in one blob)
  int voxels = 0, addedVoxels = 0, removedVoxels = 0;
  glm::vec3 min{0.0f}, max{0.0f};  // file/model space bounds (voxel faces)
  glm::vec3 centroid{0.0f};
  float magnitude = 0.0f;    // sum of |D| over the region
  int pairedWith = -1;       // for "moved": index of the matching added/removed region
  float matchCost = 0.0f;    // size + shape difference of that pair (lower = more alike)
  std::vector<float> shape;  // D1 shape distribution: histogram of voxel distances from the centroid
};

struct DetectTimings {
  double splatA = 0, splatB = 0, compare = 0, regions = 0, total = 0;  // milliseconds
  int threads = 1;
};

struct DetectResult {
  GridSpec grid;
  std::vector<float> fieldA, fieldB;  // O (or Phi) for each scan, size grid.count()
  std::vector<int8_t> label;          // +1 added, -1 removed, 0 unchanged
  std::vector<ChangeRegion> regions;
  size_t changedVoxels = 0, occupiedVoxels = 0;
  DetectTimings timings;
  DetectParams params;
};

// Grid covering both scans (robust percentile bounds, padded), `resolution` voxels on the longest axis.
GridSpec makeGrid(const Gaussians& a, const Gaussians& b, int resolution, float boundsPercentile);

// Kernel 1: accumulate the density Phi of `gs` into `field` (resized to grid.count()), then optionally map it
// to occupancy. Returns elapsed milliseconds.
double splatField(const Gaussians& gs, const GridSpec& grid, const DetectParams& p, std::vector<float>& field);

// Full pipeline: grid, splat A and B, compare, regions.
DetectResult detectChanges(const Gaussians& a, const Gaussians& b, const DetectParams& p);

// ---------- Evaluation against ground truth ----------

struct GroundTruthBox {
  std::string type;  // edit type + "/src" or "/dst"
  glm::vec3 min, max;
};

// Boxes of the geometric edits (move src+dst, delete src, duplicate dst) from an edit log.
std::vector<GroundTruthBox> groundTruthBoxes(const std::vector<EditRecord>& log);
std::vector<EditRecord> loadEditLog(const std::string& path);  // reads a .edits.json

struct Evaluation {
  int gtBoxes = 0, gtDetected = 0;    // recall: GT boxes touched by a detected region
  int regions = 0, falseRegions = 0;  // detected regions that touch no GT box
  size_t falseVoxels = 0;             // changed voxels outside every (padded) GT box
  float falseChangeRate = 0;          // falseVoxels / occupied voxels
  float meanIoU = 0;                  // mean over GT boxes of best box IoU with a detected region
};
Evaluation evaluate(const DetectResult& r, const std::vector<GroundTruthBox>& gt);

}  // namespace splatdiff
