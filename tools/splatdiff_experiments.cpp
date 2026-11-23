// splatdiff_experiments: run the full SplatDiff experiment suite on one scan A.
//
//   splatdiff_experiments A.ply [--out DIR] [--seed N] [--res N] [--repeats N] [--quick]
//
// Writes DIR/cases.csv (pass/fail per case), DIR/tau_sweep.csv, DIR/resolution_sweep.csv, DIR/threads.csv.
// Plot them with: python3 tools/plot_results.py DIR
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "Experiments.hpp"

using namespace splatdiff;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: splatdiff_experiments A.ply [--out DIR] [--seed N] [--res N] [--repeats N] [--filter "
                 "clamp|dilate|mass|none] [--quick]\n";
    return 2;
  }
  std::string aPath = argv[1], outDir = "assets/experiments";
  uint32_t seed = 7;
  int repeats = 3;
  bool quick = false;
  DetectParams p;
  for (int i = 2; i < argc; ++i) {
    std::string o = argv[i];
    if (o == "--out" && i + 1 < argc)
      outDir = argv[++i];
    else if (o == "--seed" && i + 1 < argc)
      seed = uint32_t(std::atoi(argv[++i]));
    else if (o == "--res" && i + 1 < argc)
      p.resolution = std::atoi(argv[++i]);
    else if (o == "--repeats" && i + 1 < argc)
      repeats = std::atoi(argv[++i]);
    else if (o == "--filter" && i + 1 < argc) {
      std::string f = argv[++i];
      p.filter = f == "none"     ? FilterMode::None
                 : f == "dilate" ? FilterMode::Dilate
                 : f == "mass"   ? FilterMode::Mass
                                 : FilterMode::Clamp;
    } else if (o == "--quick")
      quick = true;
  }
  std::filesystem::create_directories(outDir);

  Gaussians a = loadPly(aPath);
  SceneScale sc = measureScene(a);
  std::printf("scan A: %zu gaussians, scene size %.3f, OpenMP threads %d\n\n", a.size(), sc.extent, maxThreads());

  // ---- cases ----
  std::vector<ExperimentCase> cases = standardCases(a, seed);
  std::vector<ExperimentRow> rows;
  Gaussians combinedB;
  std::vector<EditRecord> combinedLog;
  std::printf("%-11s %-14s %-7s %9s %8s %8s %8s %10s %6s %8s\n", "group", "case", "expect", "found", "regions", "false",
              "IoU", "false%", "ms", "result");
  int passed = 0;
  for (const auto& c : cases) {
    Gaussians b;
    std::vector<EditRecord> log;
    ExperimentRow r = runCase(a, c, p, &b, &log);
    if (c.name == "combined") combinedB = std::move(b), combinedLog = std::move(log);
    rows.push_back(r);
    passed += r.pass;
    std::printf("%-11s %-14s %-7s %4d / %-2d %8d %8d %8.2f %9.4f%% %6.0f %8s\n", r.group.c_str(), r.name.c_str(),
                r.expectChange ? "change" : "none", r.gtDetected, r.gtBoxes, r.regions, r.falseRegions, r.meanIoU,
                100.0f * r.falseChangeRate, r.detectMs, r.pass ? "PASS" : "FAIL");
  }
  std::printf("\n%d / %zu cases passed\n", passed, rows.size());
  writeCasesCsv(outDir + "/cases.csv", rows);

  // ---- sweeps on the combined case ----
  std::printf("\ntau sweep...\n");
  auto taus = sweepTau(a, combinedB, combinedLog, p,
                       {0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.35f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f});
  writeSweepCsv(outDir + "/tau_sweep.csv", taus);
  for (const auto& t : taus)
    std::printf("  %-12s tau %.2f: detected %.0f%%, false-change %.4f%%, false regions %d, IoU %.2f\n", t.mode.c_str(),
                t.x, 100 * t.detectionRate, 100 * t.falseChangeRate, t.falseRegions, t.meanIoU);

  std::printf("\nresolution sweep...\n");
  std::vector<int> resolutions =
      quick ? std::vector<int>{32, 64, 128} : std::vector<int>{32, 48, 64, 96, 128, 192, 256};
  auto res = sweepResolution(a, combinedB, combinedLog, p, resolutions);
  writeSweepCsv(outDir + "/resolution_sweep.csv", res);
  for (const auto& t : res)
    std::printf("  res %3.0f: detected %.0f%%, false regions %d, IoU %.2f, %.1f ms\n", t.x, 100 * t.detectionRate,
                t.falseRegions, t.meanIoU, t.totalMs);

  std::printf("\nthread scaling (best of %d)...\n", repeats);
  std::vector<int> threadCounts;
  for (int t = 1; t <= maxThreads(); t *= 2) threadCounts.push_back(t);
  if (threadCounts.back() != maxThreads()) threadCounts.push_back(maxThreads());
  auto thr = sweepThreads(a, combinedB, p, quick ? std::vector<int>{128} : std::vector<int>{64, 128, 256}, threadCounts,
                          quick ? 1 : repeats);
  writeSweepCsv(outDir + "/threads.csv", thr);
  double serial = 0;
  for (const auto& t : thr) {
    if (t.mode == "serial") serial = t.totalMs;
    std::printf("  res %3d %-24s %d threads: %8.1f ms  speedup %.2fx\n", t.resolution, t.mode.c_str(), t.threads,
                t.totalMs, serial / t.totalMs);
  }
  std::printf("\nCSV files in %s. Plot: python3 tools/plot_results.py %s\n", outDir.c_str(), outDir.c_str());
  return passed == int(rows.size()) ? 0 : 1;
}
