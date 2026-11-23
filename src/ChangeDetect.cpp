#include "ChangeDetect.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <queue>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace splatdiff {

namespace {

using Clock = std::chrono::high_resolution_clock;
double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

// Precomputed per-Gaussian data for the splat kernel.
struct SplatShape {
  glm::mat3 inv;  // (Sigma + s^2 I)^-1
  float amp;      // alpha * sqrt(det Sigma / det(Sigma + s^2 I))  (mass-preserving filter)
  glm::vec3 radius;
};

bool prepare(const GaussianSphere& g, const DetectParams& p, float voxel, SplatShape& out) {
  glm::mat3 cov = covariance(g);
  float amp = g.opacity;
  const float s = 0.5f * voxel;  // filter std-dev: half a voxel
  switch (p.filter) {
    case FilterMode::None:
      break;
    case FilterMode::Clamp:
      cov = clampCovariance(cov, s * s);
      break;
    case FilterMode::Dilate:
      cov += glm::mat3(s * s);
      break;
    case FilterMode::Mass: {
      glm::mat3 filtered = cov + glm::mat3(s * s);
      amp *= std::sqrt(std::max(glm::determinant(cov), 0.0f) / glm::determinant(filtered));
      cov = filtered;
      break;
    }
  }
  float det = glm::determinant(cov);
  if (!(det > 1e-30f) || !(amp > 1e-6f)) return false;
  out.inv = glm::inverse(cov);
  out.amp = amp;
  // the k-sigma ellipsoid fits in a box of half-size k * sqrt(Sigma_ii) per axis
  out.radius = p.cutoffSigma * glm::sqrt(glm::vec3(cov[0][0], cov[1][1], cov[2][2]));
  return true;
}

// Splat one Gaussian into `field`. With `atomic`, concurrent writers to the same voxel are safe.
inline void splatOne(const GaussianSphere& g, const GridSpec& grid, const DetectParams& p, float* field, bool atomic) {
  SplatShape s;
  if (!prepare(g, p, grid.voxel, s)) return;
  glm::vec3 lo = (g.position - s.radius - grid.origin) / grid.voxel;
  glm::vec3 hi = (g.position + s.radius - grid.origin) / grid.voxel;
  int x0 = std::max(0, int(std::floor(lo.x))), x1 = std::min(grid.dims.x - 1, int(std::floor(hi.x)));
  int y0 = std::max(0, int(std::floor(lo.y))), y1 = std::min(grid.dims.y - 1, int(std::floor(hi.y)));
  int z0 = std::max(0, int(std::floor(lo.z))), z1 = std::min(grid.dims.z - 1, int(std::floor(hi.z)));
  const float cutoff2 = p.cutoffSigma * p.cutoffSigma;
  for (int z = z0; z <= z1; ++z)
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        glm::vec3 d = grid.centre(x, y, z) - g.position;
        float e = glm::dot(d, s.inv * d);  // squared Mahalanobis distance
        if (e > cutoff2) continue;
        float v = s.amp * std::exp(-0.5f * e);
        float& cell = field[grid.index(x, y, z)];
        if (atomic) {
#pragma omp atomic
          cell += v;
        } else {
          cell += v;
        }
      }
}

// Separable max filter with radius r (box neighbourhood), used for the 1-voxel tolerance.
std::vector<float> maxFilter(const std::vector<float>& in, const GridSpec& g, int r) {
  if (r <= 0) return in;
  std::vector<float> a = in, b(in.size());
  const glm::ivec3 D = g.dims;
  for (int axis = 0; axis < 3; ++axis) {
// dynamic: macOS gives the 4 efficiency cores equal static shares, so everyone waits for them
#pragma omp parallel for collapse(2) schedule(dynamic, 16)
    for (int z = 0; z < D.z; ++z)
      for (int y = 0; y < D.y; ++y)
        for (int x = 0; x < D.x; ++x) {
          glm::ivec3 c(x, y, z);
          float m = 0.0f;
          for (int k = -r; k <= r; ++k) {
            glm::ivec3 q = c;
            q[axis] += k;
            if (q[axis] < 0 || q[axis] >= D[axis]) continue;
            m = std::max(m, a[g.index(q.x, q.y, q.z)]);
          }
          b[g.index(x, y, z)] = m;
        }
    std::swap(a, b);
  }
  return a;
}

float boxIoU(const glm::vec3& amin, const glm::vec3& amax, const glm::vec3& bmin, const glm::vec3& bmax) {
  glm::vec3 lo = glm::max(amin, bmin), hi = glm::min(amax, bmax);
  glm::vec3 e = glm::max(hi - lo, glm::vec3(0.0f));
  float inter = e.x * e.y * e.z;
  auto vol = [](const glm::vec3& mn, const glm::vec3& mx) {
    glm::vec3 s = glm::max(mx - mn, glm::vec3(0.0f));
    return s.x * s.y * s.z;
  };
  float uni = vol(amin, amax) + vol(bmin, bmax) - inter;
  return uni > 0 ? inter / uni : 0.0f;
}

bool boxesTouch(const glm::vec3& amin, const glm::vec3& amax, const glm::vec3& bmin, const glm::vec3& bmax) {
  return amin.x <= bmax.x && bmin.x <= amax.x && amin.y <= bmax.y && bmin.y <= amax.y && amin.z <= bmax.z &&
         bmin.z <= amax.z;
}

}  // namespace

const char* filterModeName(FilterMode f) {
  switch (f) {
    case FilterMode::None:
      return "none";
    case FilterMode::Clamp:
      return "clamp";
    case FilterMode::Dilate:
      return "dilate";
    case FilterMode::Mass:
      return "mass-preserving";
  }
  return "?";
}

const char* execModeName(ExecMode m) {
  switch (m) {
    case ExecMode::Serial:
      return "serial";
    case ExecMode::OpenMPAtomic:
      return "OpenMP (atomic)";
    case ExecMode::OpenMPPrivate:
      return "OpenMP (private grids)";
  }
  return "?";
}

int maxThreads() {
#ifdef _OPENMP
  return omp_get_max_threads();
#else
  return 1;
#endif
}

GridSpec makeGrid(const Gaussians& a, const Gaussians& b, int resolution, float boundsPercentile) {
  glm::vec3 lo(0.0f), hi(1.0f);
  const size_t total = a.size() + b.size();
  const size_t kMaxSamples = size_t(1) << 15;
  const size_t stride = std::max<size_t>(1, (total + kMaxSamples - 1) / kMaxSamples);
  if (total > 0) {
    // one axis per thread (at most 3; respects serial mode's single thread); each keeps its own samples
#pragma omp parallel for schedule(static, 1)
    for (int axis = 0; axis < 3; ++axis) {
      std::vector<float> coord;
      coord.reserve(total / stride + 2);
      for (size_t i = 0; i < a.size(); i += stride) coord.push_back(a[i].position[axis]);
      for (size_t i = 0; i < b.size(); i += stride) coord.push_back(b[i].position[axis]);
      size_t n = coord.size();
      size_t iLo = size_t(double(n - 1) * boundsPercentile / 100.0);
      size_t iHi = size_t(double(n - 1) * (1.0 - boundsPercentile / 100.0));
      std::nth_element(coord.begin(), coord.begin() + iLo, coord.end());
      lo[axis] = coord[iLo];
      std::nth_element(coord.begin() + iLo, coord.begin() + iHi, coord.end());  // iHi >= iLo
      hi[axis] = coord[iHi];
    }
  }
  glm::vec3 extent = glm::max(hi - lo, glm::vec3(1e-3f));
  float pad = 0.05f * std::max(extent.x, std::max(extent.y, extent.z));
  lo -= pad;
  hi += pad;
  extent = hi - lo;
  GridSpec g;
  g.voxel = std::max(extent.x, std::max(extent.y, extent.z)) / float(std::max(resolution, 4));
  g.origin = lo;
  g.dims = glm::max(glm::ivec3(glm::ceil(extent / g.voxel)), glm::ivec3(1));
  return g;
}

double splatField(const Gaussians& gs, const GridSpec& grid, const DetectParams& p, std::vector<float>& field) {
  auto t0 = Clock::now();
  const size_t n = grid.count();
  field.assign(n, 0.0f);
  const long count = long(gs.size());

  switch (p.mode) {
    case ExecMode::Serial:
      for (long i = 0; i < count; ++i) splatOne(gs[i], grid, p, field.data(), false);
      break;

    case ExecMode::OpenMPAtomic:
#pragma omp parallel for schedule(dynamic, 256)
      for (long i = 0; i < count; ++i) splatOne(gs[i], grid, p, field.data(), true);
      break;

    case ExecMode::OpenMPPrivate: {
      std::vector<std::vector<float>> local(maxThreads());
#pragma omp parallel
      {
#ifdef _OPENMP
        int t = omp_get_thread_num();
#else
        int t = 0;
#endif
        local[t].assign(n, 0.0f);
#pragma omp for schedule(dynamic, 256)
        for (long i = 0; i < count; ++i) splatOne(gs[i], grid, p, local[t].data(), false);
      }
      // parallel reduction over voxels
#pragma omp parallel for schedule(dynamic, 32768)
      for (long v = 0; v < long(n); ++v) {
        float sum = 0.0f;
        for (const auto& l : local)
          if (!l.empty()) sum += l[v];
        field[v] = sum;
      }
      break;
    }
  }

  if (p.occupancy) {
#pragma omp parallel for schedule(dynamic, 32768)
    for (long v = 0; v < long(n); ++v) field[v] = 1.0f - std::exp(-field[v]);
  }
  return msSince(t0);
}

DetectResult detectChanges(const Gaussians& a, const Gaussians& b, const DetectParams& p) {
  auto tStart = Clock::now();
  DetectResult r;
  r.params = p;
#ifdef _OPENMP
  const int previousThreads = omp_get_max_threads();
  // serial mode must be serial end to end (compare and region kernels too), or the baseline is not a baseline
  if (p.mode == ExecMode::Serial)
    omp_set_num_threads(1);
  else if (p.threads > 0)
    omp_set_num_threads(p.threads);
#endif
  r.timings.threads = (p.mode == ExecMode::Serial) ? 1 : maxThreads();
  auto tSetup = Clock::now();
  r.grid = makeGrid(a, b, p.resolution, p.boundsPercentile);
  r.timings.setup = msSince(tSetup);
  const GridSpec& g = r.grid;
  const size_t n = g.count();

  // ---- Kernel 1: splat both scans ----
  r.timings.splatA = splatField(a, g, p, r.fieldA);
  r.timings.splatB = splatField(b, g, p, r.fieldB);

  // ---- Kernel 2: compare with a neighbourhood tolerance ----
  auto t0 = Clock::now();
  std::vector<float> maxA = maxFilter(r.fieldA, g, p.tolerance);
  std::vector<float> maxB = maxFilter(r.fieldB, g, p.tolerance);
  r.label.assign(n, 0);
  long changed = 0, occupied = 0;
  const float occupiedLevel = p.occupancy ? 0.5f : 0.7f;
#pragma omp parallel for schedule(dynamic, 32768) reduction(+ : changed, occupied)
  for (long v = 0; v < long(n); ++v) {
    float oa = r.fieldA[v], ob = r.fieldB[v];
    int8_t l = 0;
    if (ob - maxA[v] > p.tau)
      l = 1;  // material in B with nothing near it in A
    else if (oa - maxB[v] > p.tau)
      l = -1;  // material in A with nothing near it in B
    r.label[v] = l;
    changed += (l != 0);
    occupied += (oa > occupiedLevel || ob > occupiedLevel);
  }
  r.changedVoxels = size_t(changed);
  r.occupiedVoxels = size_t(occupied);
  r.timings.compare = msSince(t0);

  // ---- Kernel 3: connected components (26-neighbourhood) ----
  t0 = Clock::now();
  std::vector<int> comp(n, -1);
  std::vector<int> queue;
  std::vector<std::vector<int>> members;  // voxel indices of each component (for the shape descriptor)
  for (size_t seed = 0; seed < n; ++seed) {
    if (r.label[seed] == 0 || comp[seed] >= 0) continue;
    ChangeRegion reg;
    glm::ivec3 mn(std::numeric_limits<int>::max()), mx(std::numeric_limits<int>::min());
    glm::vec3 sum(0.0f);
    int id = int(r.regions.size());
    queue.clear();
    queue.push_back(int(seed));
    comp[seed] = id;
    for (size_t qi = 0; qi < queue.size(); ++qi) {
      int v = queue[qi];
      int x = v % g.dims.x, y = (v / g.dims.x) % g.dims.y, z = v / (g.dims.x * g.dims.y);
      glm::ivec3 c(x, y, z);
      mn = glm::min(mn, c);
      mx = glm::max(mx, c);
      sum += glm::vec3(c);
      reg.voxels++;
      (r.label[v] > 0 ? reg.addedVoxels : reg.removedVoxels)++;
      reg.magnitude += std::fabs(r.fieldB[v] - r.fieldA[v]);
      for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            int nx = x + dx, ny = y + dy, nz = z + dz;
            if (nx < 0 || ny < 0 || nz < 0 || nx >= g.dims.x || ny >= g.dims.y || nz >= g.dims.z) continue;
            size_t ni = g.index(nx, ny, nz);
            if (r.label[ni] == 0 || comp[ni] >= 0) continue;
            comp[ni] = id;
            queue.push_back(int(ni));
          }
    }
    reg.min = g.origin + glm::vec3(mn) * g.voxel;
    reg.max = g.origin + glm::vec3(mx + 1) * g.voxel;
    reg.centroid = g.origin + (sum / float(reg.voxels) + 0.5f) * g.voxel;
    float addFrac = float(reg.addedVoxels) / reg.voxels;
    reg.type = addFrac > 0.9f ? "added" : (addFrac < 0.1f ? "removed" : "changed");
    reg.componentId = id;
    r.regions.push_back(reg);
    members.push_back(queue);
  }

  // Rotation-invariant shape descriptor (D1 shape distribution): normalised histogram of voxel distances
  // from the region centroid, in voxel-sized bins. A moved object keeps its histogram; a sphere and a cube
  // of the same volume do not share one.
  const int kBins = 32;
  std::vector<std::vector<float>> shape(r.regions.size(), std::vector<float>(kBins, 0.0f));
  for (size_t i = 0; i < r.regions.size(); ++i) {
    glm::vec3 c = (r.regions[i].centroid - g.origin) / g.voxel - 0.5f;  // centroid in voxel coordinates
    for (int v : members[i]) {
      glm::vec3 q(v % g.dims.x, (v / g.dims.x) % g.dims.y, v / (g.dims.x * g.dims.y));
      int bin = std::min(kBins - 1, int(glm::length(q - c)));
      shape[i][bin] += 1.0f / members[i].size();
    }
  }
  for (size_t i = 0; i < r.regions.size(); ++i) r.regions[i].shape = shape[i];
  // drop noise, largest first
  r.regions.erase(
      std::remove_if(r.regions.begin(), r.regions.end(), [&](const ChangeRegion& c) { return c.voxels < p.minVoxels; }),
      r.regions.end());
  std::sort(r.regions.begin(), r.regions.end(),
            [](const ChangeRegion& x, const ChangeRegion& y) { return x.voxels > y.voxels; });

  // Pair removed + added regions that look like the same object: cost = relative size difference +
  // L1 distance between shape histograms (0 = identical, 2 = disjoint). Greedy, cheapest pair first.
  struct Candidate {
    float cost;
    int removed, added;
  };
  std::vector<Candidate> cands;
  for (size_t i = 0; i < r.regions.size(); ++i) {
    if (r.regions[i].type != "removed") continue;
    for (size_t j = 0; j < r.regions.size(); ++j) {
      if (r.regions[j].type != "added") continue;
      float a = float(r.regions[i].voxels), b = float(r.regions[j].voxels);
      float sizeDiff = std::fabs(a - b) / std::max(a, b);
      float shapeDiff = 0.0f;
      for (size_t k = 0; k < r.regions[i].shape.size(); ++k)
        shapeDiff += std::fabs(r.regions[i].shape[k] - r.regions[j].shape[k]);
      if (sizeDiff < 0.35f && shapeDiff < 0.8f) cands.push_back({sizeDiff + shapeDiff, int(i), int(j)});
    }
  }
  std::sort(cands.begin(), cands.end(), [](const Candidate& x, const Candidate& y) { return x.cost < y.cost; });
  for (const auto& c : cands) {
    auto& rem = r.regions[c.removed];
    auto& add = r.regions[c.added];
    if (rem.pairedWith >= 0 || add.pairedWith >= 0) continue;
    rem.pairedWith = c.added;
    add.pairedWith = c.removed;
    rem.matchCost = add.matchCost = c.cost;
    rem.type = "moved (from)";
    add.type = "moved (to)";
  }
  // per-voxel region index (after filtering and sorting), used to tag Gaussians in the viewer
  std::vector<int> newIndex(members.size(), -1);
  for (size_t i = 0; i < r.regions.size(); ++i) newIndex[r.regions[i].componentId] = int(i);
  r.regionId.assign(n, -1);
#pragma omp parallel for schedule(dynamic, 32768)
  for (long v = 0; v < long(n); ++v)
    if (comp[v] >= 0) r.regionId[v] = newIndex[comp[v]];

  r.timings.regions = msSince(t0);
  r.timings.total = msSince(tStart);
#ifdef _OPENMP
  omp_set_num_threads(previousThreads);
#endif
  return r;
}

// ---------- Evaluation ----------

std::vector<GroundTruthBox> groundTruthBoxes(const std::vector<EditRecord>& log) {
  std::vector<GroundTruthBox> out;
  for (const auto& e : log) {
    if (!e.geometric || !e.hasBounds) continue;
    if (e.type != "duplicate") out.push_back({e.type + "/src", e.srcMin, e.srcMax});  // the copy source is unchanged
    if (e.hasDst) out.push_back({e.type + "/dst", e.dstMin, e.dstMax});
  }
  return out;
}

std::vector<EditRecord> loadEditLog(const std::string& path) {
  std::vector<EditRecord> log;
  std::ifstream f(path);
  if (!f) return log;
  nlohmann::json j = nlohmann::json::parse(f);
  auto vec = [](const nlohmann::json& a) { return glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>()); };
  for (const auto& e : j["edits"]) {
    EditRecord r;
    r.type = e.value("type", "");
    r.params = e.value("params", "");
    r.affected = e.value("affected", 0);
    r.geometric = e.value("geometric", true);
    if (e.contains("src_bounds")) {
      r.hasBounds = true;
      r.srcMin = vec(e["src_bounds"]["min"]);
      r.srcMax = vec(e["src_bounds"]["max"]);
    }
    if (e.contains("dst_bounds")) {
      r.hasDst = true;
      r.dstMin = vec(e["dst_bounds"]["min"]);
      r.dstMax = vec(e["dst_bounds"]["max"]);
    }
    log.push_back(r);
  }
  return log;
}

Evaluation evaluate(const DetectResult& r, const std::vector<GroundTruthBox>& gt) {
  Evaluation ev;
  ev.gtBoxes = int(gt.size());
  ev.regions = int(r.regions.size());
  const GridSpec& g = r.grid;
  // GT boxes are bounds of Gaussian centres; the splats and the filter spread a little beyond them.
  const float pad = (2.0f + r.params.tolerance) * g.voxel;

  for (const auto& reg : r.regions) {
    bool hit = false;
    for (const auto& b : gt) hit |= boxesTouch(reg.min, reg.max, b.min - pad, b.max + pad);
    ev.falseRegions += !hit;
  }
  float iouSum = 0;
  for (const auto& b : gt) {
    float best = 0;
    bool detected = false;
    for (const auto& reg : r.regions) {
      if (boxesTouch(reg.min, reg.max, b.min - pad, b.max + pad)) detected = true;
      best = std::max(best, boxIoU(reg.min, reg.max, b.min, b.max));
    }
    ev.gtDetected += detected;
    iouSum += best;
  }
  ev.meanIoU = gt.empty() ? 0.0f : iouSum / gt.size();

  for (int z = 0; z < g.dims.z; ++z)
    for (int y = 0; y < g.dims.y; ++y)
      for (int x = 0; x < g.dims.x; ++x) {
        if (r.label[g.index(x, y, z)] == 0) continue;
        glm::vec3 c = g.centre(x, y, z);
        bool inside = false;
        for (const auto& b : gt)
          inside |= glm::all(glm::greaterThanEqual(c, b.min - pad)) && glm::all(glm::lessThanEqual(c, b.max + pad));
        ev.falseVoxels += !inside;
      }
  ev.falseChangeRate = r.occupiedVoxels ? float(ev.falseVoxels) / float(r.occupiedVoxels) : 0.0f;
  return ev;
}

}  // namespace splatdiff
