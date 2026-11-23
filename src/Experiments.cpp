#include "Experiments.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

#include "Random.hpp"

namespace splatdiff {

namespace {

float percentile(std::vector<float> v, float pct) {
  if (v.empty()) return 0.0f;
  size_t k = std::min(v.size() - 1, size_t(double(v.size() - 1) * pct / 100.0));
  std::nth_element(v.begin(), v.begin() + k, v.end());
  return v[k];
}

glm::vec3 randomDirection(PortableRng& rng) {
  glm::vec3 d(rng.normal(), rng.normal(), rng.normal());
  return glm::length(d) > 1e-6f ? glm::normalize(d) : glm::vec3(1, 0, 0);
}

// Pick a cube region around a random Gaussian that holds a reasonable chunk of the scene and keeps away from
// regions picked before (so edits do not interfere with each other's ground truth).
Region pickRegion(const Gaussians& a, const SceneScale& sc, PortableRng& rng, float halfFraction,
                  const std::vector<Region>& avoid) {
  Region best;
  best.halfSize = glm::vec3(halfFraction * sc.extent);
  int bestCount = -1;
  const int minCount = 200, maxCount = int(0.15 * a.size());
  for (int attempt = 0; attempt < 300 && !a.empty(); ++attempt) {
    Region r;
    r.halfSize = best.halfSize;
    r.center = a[rng.index(a.size())].position;
    if (glm::any(glm::lessThan(r.center, sc.lo)) || glm::any(glm::greaterThan(r.center, sc.hi))) continue;
    bool clash = false;
    for (const auto& o : avoid) clash |= glm::length(o.center - r.center) < 3.5f * r.halfSize.x;
    if (clash) continue;
    int count = 0;
    for (const auto& g : a) count += r.contains(g.position);
    if (count >= minCount && count <= maxCount) return r;
    if (count > bestCount && count <= maxCount) bestCount = count, best = r;
  }
  return best;
}

ExperimentRow score(const ExperimentCase& c, const Gaussians& b, const DetectResult& r,
                    const std::vector<EditRecord>& log) {
  ExperimentRow row;
  row.group = c.group;
  row.name = c.name;
  row.expectChange = c.expectChange;
  row.gaussiansB = b.size();
  Evaluation ev = evaluate(r, groundTruthBoxes(log));
  row.gtBoxes = ev.gtBoxes;
  row.gtDetected = ev.gtDetected;
  row.regions = ev.regions;
  row.falseRegions = ev.falseRegions;
  row.changedVoxels = r.changedVoxels;
  row.falseChangeRate = ev.falseChangeRate;
  row.meanIoU = ev.meanIoU;
  row.detectMs = r.timings.total;
  row.pass = c.expectChange ? (ev.gtDetected == ev.gtBoxes && ev.falseRegions == 0) : (ev.regions == 0);
  return row;
}

}  // namespace

SceneScale measureScene(const Gaussians& a) {
  SceneScale sc;
  std::vector<float> v;
  v.reserve(a.size());
  for (int axis = 0; axis < 3; ++axis) {
    v.clear();
    for (const auto& g : a) v.push_back(g.position[axis]);
    sc.lo[axis] = percentile(v, 0.5f);
    sc.hi[axis] = percentile(v, 99.5f);
  }
  glm::vec3 e = sc.hi - sc.lo;
  sc.extent = std::max(1e-3f, std::max(e.x, std::max(e.y, e.z)));
  v.clear();
  for (const auto& g : a) v.push_back(g.opacity);
  sc.opacity30 = percentile(v, 30.0f);
  return sc;
}

std::vector<ExperimentCase> standardCases(const Gaussians& a, uint32_t seed) {
  const SceneScale sc = measureScene(a);
  const float E = sc.extent;
  const float h = 0.06f * E;  // half-size of edited regions: about an object in a room-sized scene
  PortableRng rng(seed);
  // Move/copy directions stay parallel to the "ground": scenes have no labelled up axis, but the thinnest
  // extent of a scan is almost always vertical (rooms, tabletops, streets), so that component is zeroed.
  const glm::vec3 ext = sc.hi - sc.lo;
  const int upAxis = (ext.x <= ext.y && ext.x <= ext.z) ? 0 : (ext.y <= ext.z ? 1 : 2);
  auto groundDirection = [&]() {
    glm::vec3 d = randomDirection(rng);
    d[upAxis] = 0.0f;
    return glm::length(d) > 1e-4f ? glm::normalize(d) : glm::vec3(upAxis == 0 ? 0 : 1, 0, upAxis == 0 ? 1 : 0);
  };
  auto countIn = [&](const glm::vec3& centre) {
    Region q;
    q.center = centre;
    q.halfSize = glm::vec3(h);
    int n = 0;
    for (const auto& g : a) n += q.contains(g.position);
    return n;
  };
  // A move or copy is only a real change if it lands somewhere empty: copying a patch of floor onto more floor
  // changes nothing. Pick the region and a ground direction together so the destination holds as little of the
  // scene as possible relative to the source, stays inside the scene and keeps clear of the other edits.
  std::vector<Region> chosen;
  auto pickMovable = [&](float distance, glm::vec3& dirOut) {
    Region best = pickRegion(a, sc, rng, 0.06f, chosen);
    glm::vec3 bestDir = groundDirection();
    float bestRatio = std::numeric_limits<float>::max();
    for (int attempt = 0; attempt < 24 && bestRatio > 0.25f; ++attempt) {
      Region r = attempt == 0 ? best : pickRegion(a, sc, rng, 0.06f, chosen);
      const int source = countIn(r.center);
      if (source == 0) continue;
      for (int k = 0; k < 12; ++k) {
        const glm::vec3 d = groundDirection();
        const glm::vec3 dst = r.center + d * distance;
        bool ok = !glm::any(glm::lessThan(dst, sc.lo)) && !glm::any(glm::greaterThan(dst, sc.hi));
        for (const auto& o : chosen) ok &= glm::length(o.center - dst) >= 3.5f * h;
        if (!ok) continue;
        const float ratio = float(countIn(dst)) / float(source);
        if (ratio < bestRatio) bestRatio = ratio, best = r, bestDir = d;
      }
    }
    dirOut = bestDir;
    return best;
  };
  Region r1 = pickRegion(a, sc, rng, 0.06f, chosen);
  chosen.push_back(r1);
  glm::vec3 d1, d2;
  Region r2 = pickMovable(2.5f * h, d1);
  chosen.push_back(r2);
  chosen.push_back(Region{r2.center + d1 * (2.5f * h), r2.halfSize});
  Region r3 = pickMovable(3.0f * h, d2);
  const float op30 = sc.opacity30;
  const uint32_t s = seed;

  std::vector<ExperimentCase> cs;
  auto add = [&](std::string group, std::string name, std::string desc, bool expect,
                 std::function<void(Gaussians&, std::vector<EditRecord>&)> f) {
    cs.push_back({std::move(group), std::move(name), std::move(desc), expect, std::move(f)});
  };

  // ---- stability: the same physical scene, re-scanned ----
  add("stability", "jitter", "position noise 0.1% of scene size, scale noise 10%", false,
      [=](Gaussians& b, std::vector<EditRecord>& l) { l.push_back(jitterAll(b, 0.001f * E, 0.1f, s + 1)); });
  add("stability", "jitter_strong", "position noise 0.3% of scene size, scale noise 20%", false,
      [=](Gaussians& b, std::vector<EditRecord>& l) { l.push_back(jitterAll(b, 0.003f * E, 0.2f, s + 2)); });
  add("stability", "split", "50% of Gaussians split in two", false,
      [=](Gaussians& b, std::vector<EditRecord>& l) { l.push_back(splitRandom(b, 0.5f, s + 3)); });
  add("stability", "prune", "half of the faintest 30% removed", false,
      [=](Gaussians& b, std::vector<EditRecord>& l) { l.push_back(pruneFaint(b, 0.5f, op30, s + 4)); });
  add("stability", "rescan_all", "split + prune + jitter together", false,
      [=](Gaussians& b, std::vector<EditRecord>& l) {
        l.push_back(splitRandom(b, 0.3f, s + 5));
        l.push_back(pruneFaint(b, 0.3f, op30, s + 6));
        l.push_back(jitterAll(b, 0.001f * E, 0.1f, s + 7));
      });

  // ---- appearance: lighting changes, geometry untouched ----
  add("appearance", "colour_global", "whole scene re-lit (gain 0.6/0.8/1.3, +0.1)", false,
      [=](Gaussians& b, std::vector<EditRecord>& l) {
        l.push_back(colourShift(b, glm::vec3(0.6f, 0.8f, 1.3f), glm::vec3(0.1f), nullptr));
      });
  add("appearance", "colour_local", "one object repainted red", false, [=](Gaussians& b, std::vector<EditRecord>& l) {
    l.push_back(colourShift(b, glm::vec3(2.0f, 0.3f, 0.3f), glm::vec3(0.0f), &r1));
  });

  // ---- real changes ----
  add("real", "delete", "one object removed", true,
      [=](Gaussians& b, std::vector<EditRecord>& l) { l.push_back(deleteRegion(b, r1)); });
  add("real", "move", "one object moved 2.5 half-sizes", true,
      [=](Gaussians& b, std::vector<EditRecord>& l) { l.push_back(moveRegion(b, r2, d1 * (2.5f * h), 0.0f)); });
  add("real", "move_rotate", "moved and turned 45 degrees", true,
      [=](Gaussians& b, std::vector<EditRecord>& l) { l.push_back(moveRegion(b, r2, d1 * (2.5f * h), 45.0f)); });
  add("real", "duplicate", "one object copied elsewhere", true,
      [=](Gaussians& b, std::vector<EditRecord>& l) { l.push_back(duplicateRegion(b, r3, d2 * (3.0f * h))); });
  add("real", "small_move", "nudged by half its half-size (overlaps itself)", true,
      [=](Gaussians& b, std::vector<EditRecord>& l) { l.push_back(moveRegion(b, r2, d1 * (0.5f * h), 0.0f)); });
  add("real", "combined", "delete + move + duplicate, re-lit and re-scanned", true,
      [=](Gaussians& b, std::vector<EditRecord>& l) {
        l.push_back(deleteRegion(b, r1));
        l.push_back(moveRegion(b, r2, d1 * (2.5f * h), 30.0f));
        l.push_back(duplicateRegion(b, r3, d2 * (3.0f * h)));
        l.push_back(colourShift(b, glm::vec3(1.2f, 1.0f, 0.8f), glm::vec3(0.05f), nullptr));
        l.push_back(splitRandom(b, 0.2f, s + 8));
        l.push_back(jitterAll(b, 0.001f * E, 0.1f, s + 9));
      });
  return cs;
}

ExperimentRow runCase(const Gaussians& a, const ExperimentCase& c, const DetectParams& p, Gaussians* outB,
                      std::vector<EditRecord>* outLog) {
  Gaussians b = a;
  std::vector<EditRecord> log;
  c.make(b, log);
  DetectResult r = detectChanges(a, b, p);
  ExperimentRow row = score(c, b, r, log);
  if (outB) *outB = std::move(b);
  if (outLog) *outLog = std::move(log);
  return row;
}

static SweepPoint pointFrom(const std::string& sweep, float x, const DetectResult& r,
                            const std::vector<GroundTruthBox>& gt) {
  SweepPoint pt;
  pt.sweep = sweep;
  pt.x = x;
  pt.mode = execModeName(r.params.mode);
  pt.threads = r.timings.threads;
  pt.resolution = r.params.resolution;
  if (!gt.empty()) {
    Evaluation ev = evaluate(r, gt);
    pt.detectionRate = float(ev.gtDetected) / ev.gtBoxes;
    pt.falseChangeRate = ev.falseChangeRate;
    pt.meanIoU = ev.meanIoU;
    pt.falseRegions = ev.falseRegions;
  }
  pt.totalMs = r.timings.total;
  pt.setupMs = r.timings.setup;
  pt.splatMs = r.timings.splatA + r.timings.splatB;
  pt.compareMs = r.timings.compare;
  pt.regionsMs = r.timings.regions;
  return pt;
}

std::vector<SweepPoint> sweepTau(const Gaussians& a, const Gaussians& b, const std::vector<EditRecord>& log,
                                 DetectParams p, const std::vector<float>& taus) {
  std::vector<SweepPoint> out;
  auto gt = groundTruthBoxes(log);
  // two series: the full detector, and the same without the neighbourhood tolerance (ablation)
  for (int tol : {p.tolerance, 0}) {
    DetectParams q = p;
    q.tolerance = tol;
    for (float t : taus) {
      q.tau = t;
      SweepPoint pt = pointFrom("tau", t, detectChanges(a, b, q), gt);
      pt.mode = "tolerance " + std::to_string(tol);
      out.push_back(pt);
    }
    if (p.tolerance == 0) break;
  }
  return out;
}

std::vector<SweepPoint> sweepResolution(const Gaussians& a, const Gaussians& b, const std::vector<EditRecord>& log,
                                        DetectParams p, const std::vector<int>& resolutions) {
  std::vector<SweepPoint> out;
  auto gt = groundTruthBoxes(log);
  for (int res : resolutions) {
    p.resolution = res;
    out.push_back(pointFrom("resolution", float(res), detectChanges(a, b, p), gt));
  }
  return out;
}

std::vector<SweepPoint> sweepThreads(const Gaussians& a, const Gaussians& b, DetectParams p,
                                     const std::vector<int>& resolutions, const std::vector<int>& threads,
                                     int repeats) {
  std::vector<SweepPoint> out;
  auto best = [&](DetectParams q) {
    DetectResult bestRun = detectChanges(a, b, q);
    for (int k = 1; k < repeats; ++k) {
      DetectResult r = detectChanges(a, b, q);
      if (r.timings.total < bestRun.timings.total) bestRun = std::move(r);
    }
    return bestRun;
  };
  for (int res : resolutions) {
    p.resolution = res;
    DetectParams q = p;
    q.mode = ExecMode::Serial;
    q.threads = 0;
    out.push_back(pointFrom("threads", 1.0f, best(q), {}));
    for (ExecMode m : {ExecMode::OpenMPAtomic, ExecMode::OpenMPPrivate})
      for (int t : threads) {
        q.mode = m;
        q.threads = t;
        out.push_back(pointFrom("threads", float(t), best(q), {}));
      }
  }
  return out;
}

void writeCasesCsv(const std::string& path, const std::vector<ExperimentRow>& rows) {
  std::ofstream f(path);
  f << "group,name,expect_change,gaussians_b,gt_boxes,gt_detected,regions,false_regions,changed_voxels,"
       "false_change_rate,mean_iou,detect_ms,pass\n";
  for (const auto& r : rows)
    f << r.group << ',' << r.name << ',' << r.expectChange << ',' << r.gaussiansB << ',' << r.gtBoxes << ','
      << r.gtDetected << ',' << r.regions << ',' << r.falseRegions << ',' << r.changedVoxels << ',' << r.falseChangeRate
      << ',' << r.meanIoU << ',' << r.detectMs << ',' << r.pass << '\n';
}

void writeSweepCsv(const std::string& path, const std::vector<SweepPoint>& points) {
  std::ofstream f(path);
  f << "sweep,x,mode,threads,resolution,detection_rate,false_change_rate,mean_iou,false_regions,total_ms,setup_ms,"
       "splat_ms,compare_ms,regions_ms\n";
  for (const auto& p : points)
    f << p.sweep << ',' << p.x << ",\"" << p.mode << "\"," << p.threads << ',' << p.resolution << ',' << p.detectionRate
      << ',' << p.falseChangeRate << ',' << p.meanIoU << ',' << p.falseRegions << ',' << p.totalMs << ',' << p.setupMs
      << ',' << p.splatMs << ',' << p.compareMs << ',' << p.regionsMs << '\n';
}

}  // namespace splatdiff
