#pragma once
// SplatDiff experiment suite: the EL experiments, runnable on any scan A.
//
//   stability   re-scan effects on an unchanged scene (jitter, split, prune)      -> expect no change
//   appearance  lighting / colour changes (global and local)                       -> expect no change
//   real        delete, move, move+rotate, duplicate, small move, all combined     -> expect exactly those
//   sweeps      threshold tau, grid resolution, thread count (strong scaling)
//
// Edit regions are chosen automatically and sized relative to the scene, so the same suite runs on the
// synthetic room and on real 3DGS captures.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ChangeDetect.hpp"
#include "SceneEdit.hpp"

namespace splatdiff {

struct ExperimentCase {
  std::string group;  // stability | appearance | real
  std::string name;
  std::string description;
  bool expectChange = false;
  // builds scan B from A (deterministic for a given seed) and appends the ground-truth edit records
  std::function<void(Gaussians& b, std::vector<EditRecord>& log)> make;
};

struct ExperimentRow {
  std::string group, name;
  bool expectChange = false;
  size_t gaussiansB = 0;
  int gtBoxes = 0, gtDetected = 0, regions = 0, falseRegions = 0;
  size_t changedVoxels = 0;
  float falseChangeRate = 0, meanIoU = 0;
  double detectMs = 0;
  bool pass = false;
};

// Scale information used to size edits (robust bounds, longest extent).
struct SceneScale {
  glm::vec3 lo{0.0f}, hi{1.0f};
  float extent = 1.0f;
  float opacity30 = 0.5f;  // 30th percentile of opacity
};
SceneScale measureScene(const Gaussians& a);

// The standard cases for scan A. Region centres are drawn with `seed`.
std::vector<ExperimentCase> standardCases(const Gaussians& a, uint32_t seed);

// Build scan B for one case and score the detector on it.
ExperimentRow runCase(const Gaussians& a, const ExperimentCase& c, const DetectParams& p, Gaussians* outB = nullptr,
                      std::vector<EditRecord>* outLog = nullptr);

struct SweepPoint {
  std::string sweep;  // tau | resolution | threads
  float x = 0;        // the swept value
  std::string mode;   // execution mode (threads sweep)
  int threads = 0;
  int resolution = 0;
  float detectionRate = 0, falseChangeRate = 0, meanIoU = 0;
  int falseRegions = 0;
  double totalMs = 0, setupMs = 0, splatMs = 0, compareMs = 0, regionsMs = 0;
};

// Sweeps run on the "combined" real-change case (real edits plus re-scan noise and lighting).
std::vector<SweepPoint> sweepTau(const Gaussians& a, const Gaussians& b, const std::vector<EditRecord>& log,
                                 DetectParams p, const std::vector<float>& taus);
std::vector<SweepPoint> sweepResolution(const Gaussians& a, const Gaussians& b, const std::vector<EditRecord>& log,
                                        DetectParams p, const std::vector<int>& resolutions);
// Serial once, then each parallel mode at each thread count; `repeats` runs each, best time kept.
std::vector<SweepPoint> sweepThreads(const Gaussians& a, const Gaussians& b, DetectParams p,
                                     const std::vector<int>& resolutions, const std::vector<int>& threads, int repeats);

// CSV writers
void writeCasesCsv(const std::string& path, const std::vector<ExperimentRow>& rows);
void writeSweepCsv(const std::string& path, const std::vector<SweepPoint>& points);

}  // namespace splatdiff
