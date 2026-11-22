// splatdiff_detect: detect what changed between two Gaussian scans.
//
//   splatdiff_detect A.ply B.ply [--res N] [--tau T] [--tol R] [--min V] [--density] [--no-aa]
//                    [--mode serial|atomic|private] [--gt B.edits.json] [--bench] [--csv results.csv]
//
// --gt     evaluate against a ground-truth edit log (default: B.edits.json next to B.ply, if present)
// --bench  run serial, OpenMP-atomic and OpenMP-private, report speedups and check outputs agree
// --csv    append one line per run (for plotting speedup / accuracy in Python)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "ChangeDetect.hpp"

using namespace splatdiff;

static void usage() {
  std::cerr << "usage: splatdiff_detect A.ply B.ply [--res N] [--tau T] [--tol R] [--min V] [--density] [--no-aa]\n"
               "                        [--mode serial|atomic|private] [--gt edits.json] [--bench] [--csv file]\n";
  std::exit(2);
}

static void printResult(const DetectResult& r) {
  const auto& g = r.grid;
  std::printf("grid %d x %d x %d (voxel %.4f), %zu occupied, %zu changed voxels, %zu regions\n", g.dims.x, g.dims.y,
              g.dims.z, g.voxel, r.occupiedVoxels, r.changedVoxels, r.regions.size());
  for (size_t i = 0; i < r.regions.size(); ++i) {
    const auto& c = r.regions[i];
    std::printf("  [%zu] %-13s %6d voxels (+%d / -%d)  centre (%.2f, %.2f, %.2f)  size (%.2f, %.2f, %.2f)", i,
                c.type.c_str(), c.voxels, c.addedVoxels, c.removedVoxels, c.centroid.x, c.centroid.y, c.centroid.z,
                c.max.x - c.min.x, c.max.y - c.min.y, c.max.z - c.min.z);
    if (c.pairedWith >= 0) std::printf("  <-> [%d] (match cost %.2f)", c.pairedWith, c.matchCost);
    std::printf("\n");
  }
}

static void printTimings(const DetectResult& r) {
  const auto& t = r.timings;
  std::printf(
      "%-24s threads %d | splat A %8.1f ms | splat B %8.1f ms | compare %6.1f ms | regions %6.1f ms | "
      "total %8.1f ms\n",
      execModeName(r.params.mode), t.threads, t.splatA, t.splatB, t.compare, t.regions, t.total);
}

int main(int argc, char** argv) {
  if (argc < 3) usage();
  std::string aPath = argv[1], bPath = argv[2], gtPath, csvPath;
  DetectParams p;
  bool bench = false;
  for (int i = 3; i < argc; ++i) {
    std::string o = argv[i];
    auto next = [&]() -> const char* {
      if (i + 1 >= argc) usage();
      return argv[++i];
    };
    if (o == "--res")
      p.resolution = std::atoi(next());
    else if (o == "--tau")
      p.tau = std::strtof(next(), nullptr);
    else if (o == "--tol")
      p.tolerance = std::atoi(next());
    else if (o == "--min")
      p.minVoxels = std::atoi(next());
    else if (o == "--density")
      p.occupancy = false;
    else if (o == "--no-aa")
      p.antiAlias = false;
    else if (o == "--gt")
      gtPath = next();
    else if (o == "--csv")
      csvPath = next();
    else if (o == "--bench")
      bench = true;
    else if (o == "--mode") {
      std::string m = next();
      p.mode = m == "serial" ? ExecMode::Serial : m == "private" ? ExecMode::OpenMPPrivate : ExecMode::OpenMPAtomic;
    } else
      usage();
  }
  if (gtPath.empty()) {
    std::string guess = bPath.substr(0, bPath.rfind('.')) + ".edits.json";
    if (std::ifstream(guess)) gtPath = guess;
  }

  Gaussians a = loadPly(aPath), b = loadPly(bPath);
  std::printf("A: %zu gaussians (%s)\nB: %zu gaussians (%s)\n", a.size(), aPath.c_str(), b.size(), bPath.c_str());
  std::printf("params: res %d, tau %.2f, tolerance %d, min %d voxels, %s, anti-alias %s, OpenMP threads %d\n",
              p.resolution, p.tau, p.tolerance, p.minVoxels, p.occupancy ? "occupancy" : "density",
              p.antiAlias ? "on" : "off", maxThreads());

  std::vector<DetectResult> runs;
  if (bench) {
    for (ExecMode m : {ExecMode::Serial, ExecMode::OpenMPAtomic, ExecMode::OpenMPPrivate}) {
      DetectParams q = p;
      q.mode = m;
      runs.push_back(detectChanges(a, b, q));
    }
  } else {
    runs.push_back(detectChanges(a, b, p));
  }

  printResult(runs.back());
  std::printf("\n");
  for (const auto& r : runs) printTimings(r);

  if (bench) {
    const auto& s = runs[0];
    for (size_t k = 1; k < runs.size(); ++k) {
      const auto& r = runs[k];
      double maxErr = 0;
      size_t labelDiff = 0;
      for (size_t v = 0; v < s.fieldA.size(); ++v) {
        maxErr = std::max(maxErr, double(std::fabs(s.fieldA[v] - r.fieldA[v])));
        maxErr = std::max(maxErr, double(std::fabs(s.fieldB[v] - r.fieldB[v])));
        labelDiff += s.label[v] != r.label[v];
      }
      std::printf("%-24s speedup %.2fx (splat %.2fx) | max |field - serial| = %.2e | labels differing: %zu\n",
                  execModeName(r.params.mode), s.timings.total / r.timings.total,
                  (s.timings.splatA + s.timings.splatB) / (r.timings.splatA + r.timings.splatB), maxErr, labelDiff);
    }
  }

  Evaluation ev;
  bool haveGt = !gtPath.empty();
  if (haveGt) {
    auto gt = groundTruthBoxes(loadEditLog(gtPath));
    ev = evaluate(runs.back(), gt);
    std::printf("\nground truth (%s): %d geometric change boxes\n", gtPath.c_str(), ev.gtBoxes);
    std::printf("  detected %d / %d | false regions %d / %d | false-change rate %.4f%% (%zu voxels) | mean IoU %.2f\n",
                ev.gtDetected, ev.gtBoxes, ev.falseRegions, ev.regions, 100.0f * ev.falseChangeRate, ev.falseVoxels,
                ev.meanIoU);
  }

  if (!csvPath.empty()) {
    bool fresh = !std::ifstream(csvPath).good();
    std::ofstream csv(csvPath, std::ios::app);
    if (fresh)
      csv << "scanA,scanB,gaussiansA,gaussiansB,res,dimx,dimy,dimz,tau,tol,mode,threads,splatA_ms,splatB_ms,"
             "compare_ms,regions_ms,total_ms,changed_voxels,regions,gt_boxes,gt_detected,false_regions,"
             "false_change_rate,mean_iou\n";
    for (const auto& r : runs) {
      const auto& t = r.timings;
      csv << aPath << ',' << bPath << ',' << a.size() << ',' << b.size() << ',' << p.resolution << ',' << r.grid.dims.x
          << ',' << r.grid.dims.y << ',' << r.grid.dims.z << ',' << p.tau << ',' << p.tolerance << ','
          << execModeName(r.params.mode) << ',' << t.threads << ',' << t.splatA << ',' << t.splatB << ',' << t.compare
          << ',' << t.regions << ',' << t.total << ',' << r.changedVoxels << ',' << r.regions.size() << ','
          << (haveGt ? ev.gtBoxes : -1) << ',' << (haveGt ? ev.gtDetected : -1) << ','
          << (haveGt ? ev.falseRegions : -1) << ',' << (haveGt ? ev.falseChangeRate : -1) << ','
          << (haveGt ? ev.meanIoU : -1) << '\n';
    }
  }
  return 0;
}
