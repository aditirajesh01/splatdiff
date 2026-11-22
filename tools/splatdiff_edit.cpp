// splatdiff_edit: make "scan B" from "scan A" on the command line, with a ground-truth edit log.
//
//   splatdiff_edit A.ply B.ply [--seed N] [edits...]
//
// Edits are applied in order. Boxes are given as centre (cx cy cz) and half-size (hx hy hz) in file
// coordinates.
//   --move      cx cy cz hx hy hz  tx ty tz yawDeg
//   --delete    cx cy cz hx hy hz
//   --duplicate cx cy cz hx hy hz  ox oy oz
//   --jitter    posSigma scaleSigma
//   --split     fraction
//   --prune     fraction maxOpacity
//   --colour    gainR gainG gainB offset          (whole scene)
//
// Writes B.ply and B.edits.json (ground truth).
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "SceneEdit.hpp"

using namespace splatdiff;

static void usage() {
  std::cerr << "usage: splatdiff_edit A.ply B.ply [--seed N] [--move cx cy cz hx hy hz tx ty tz yaw]\n"
               "       [--delete cx cy cz hx hy hz] [--duplicate cx cy cz hx hy hz ox oy oz]\n"
               "       [--jitter posSigma scaleSigma] [--split fraction] [--prune fraction maxOpacity]\n"
               "       [--colour gainR gainG gainB offset]\n";
  std::exit(2);
}

int main(int argc, char** argv) {
  if (argc < 3) usage();
  std::string inPath = argv[1], outPath = argv[2];
  int i = 3;
  auto need = [&](int n) {
    if (i + n > argc) usage();
    std::vector<float> v;
    for (int k = 0; k < n; ++k) v.push_back(std::strtof(argv[i++], nullptr));
    return v;
  };
  auto box = [](const std::vector<float>& v) {
    Region r;
    r.center = glm::vec3(v[0], v[1], v[2]);
    r.halfSize = glm::vec3(v[3], v[4], v[5]);
    return r;
  };

  Gaussians gs = loadPly(inPath);
  std::cout << "loaded " << gs.size() << " gaussians from " << inPath << "\n";
  std::vector<EditRecord> log;
  uint32_t seed = 0;

  while (i < argc) {
    std::string op = argv[i++];
    if (op == "--seed") {
      seed = uint32_t(need(1)[0]);
    } else if (op == "--move") {
      auto v = need(10);
      log.push_back(moveRegion(gs, box(v), glm::vec3(v[6], v[7], v[8]), v[9]));
    } else if (op == "--delete") {
      log.push_back(deleteRegion(gs, box(need(6))));
    } else if (op == "--duplicate") {
      auto v = need(9);
      log.push_back(duplicateRegion(gs, box(v), glm::vec3(v[6], v[7], v[8])));
    } else if (op == "--jitter") {
      auto v = need(2);
      log.push_back(jitterAll(gs, v[0], v[1], seed++));
    } else if (op == "--split") {
      log.push_back(splitRandom(gs, need(1)[0], seed++));
    } else if (op == "--prune") {
      auto v = need(2);
      log.push_back(pruneFaint(gs, v[0], v[1], seed++));
    } else if (op == "--colour" || op == "--color") {
      auto v = need(4);
      log.push_back(colourShift(gs, glm::vec3(v[0], v[1], v[2]), glm::vec3(v[3]), nullptr));
    } else {
      std::cerr << "unknown option " << op << "\n";
      usage();
    }
    if (op != "--seed")
      std::cout << "  " << log.back().type << ": " << log.back().affected << " gaussians  " << log.back().params
                << "\n";
  }

  savePly(outPath, gs);
  std::string logPath = outPath.substr(0, outPath.rfind('.')) + ".edits.json";
  saveEditLog(logPath, inPath, log);
  std::cout << "wrote " << gs.size() << " gaussians to " << outPath << " and ground truth to " << logPath << "\n";
  return 0;
}
