#include "SceneEdit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <stdexcept>

#include "happly.h"

namespace splatdiff {

namespace {

constexpr float kShC0 = 0.28209479177387814f;

// Running min/max over the file-space positions touched by an edit.
struct BoundsAccumulator {
  glm::vec3 mn{std::numeric_limits<float>::max()};
  glm::vec3 mx{std::numeric_limits<float>::lowest()};
  int count = 0;
  void add(const glm::vec3& p) {
    mn = glm::min(mn, p);
    mx = glm::max(mx, p);
    ++count;
  }
};

std::string fmt(const char* format, double a = 0, double b = 0, double c = 0, double d = 0) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), format, a, b, c, d);
  return buf;
}

}  // namespace

// ---------- Covariance helpers ----------

glm::mat3 covariance(const GaussianSphere& g) {
  return glm::mat3(g.covA.x, g.covA.y, g.covA.z,   // column 0
                   g.covA.y, g.covB.x, g.covB.y,   // column 1
                   g.covA.z, g.covB.y, g.covB.z);  // column 2
}

void setCovariance(GaussianSphere& g, const glm::mat3& m) {
  glm::mat3 s = 0.5f * (m + glm::transpose(m));  // keep it exactly symmetric
  g.covA = glm::vec3(s[0][0], s[0][1], s[0][2]);
  g.covB = glm::vec3(s[1][1], s[1][2], s[2][2]);
}

glm::mat3 covarianceFromScaleRotation(const glm::vec3& scale, const glm::quat& rotation) {
  glm::mat3 R = glm::mat3_cast(glm::normalize(rotation));
  glm::mat3 S(scale.x, 0, 0, 0, scale.y, 0, 0, 0, scale.z);
  glm::mat3 M = R * S;
  return M * glm::transpose(M);
}

void symmetricEigen(const glm::mat3& m, glm::vec3& values, glm::mat3& vectors) {
  // Cyclic Jacobi rotations (Numerical Recipes convention), in double precision.
  // a[row][col]; glm is column-major so m[col][row].
  double a[3][3], v[3][3];
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) {
      a[r][c] = 0.5 * (double(m[c][r]) + double(m[r][c]));
      v[r][c] = (r == c) ? 1.0 : 0.0;
    }

  for (int sweep = 0; sweep < 50; ++sweep) {
    double off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
    double diag = a[0][0] * a[0][0] + a[1][1] * a[1][1] + a[2][2] * a[2][2];
    if (off <= 1e-30 * (diag + 1e-300)) break;
    for (int p = 0; p < 2; ++p)
      for (int q = p + 1; q < 3; ++q) {
        if (std::fabs(a[p][q]) < 1e-300) continue;
        double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
        double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
        double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
        // A' = J^T A J and V' = V J, with J = I except J[p][p]=J[q][q]=c, J[p][q]=s, J[q][p]=-s.
        double J[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        J[p][p] = c;
        J[q][q] = c;
        J[p][q] = s;
        J[q][p] = -s;
        double AJ[3][3] = {}, nA[3][3] = {}, nV[3][3] = {};
        for (int i = 0; i < 3; ++i)
          for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) {
              AJ[i][j] += a[i][k] * J[k][j];
              nV[i][j] += v[i][k] * J[k][j];
            }
        for (int i = 0; i < 3; ++i)
          for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) nA[i][j] += J[k][i] * AJ[k][j];
        std::copy(&nA[0][0], &nA[0][0] + 9, &a[0][0]);
        std::copy(&nV[0][0], &nV[0][0] + 9, &v[0][0]);
      }
  }
  values = glm::vec3(a[0][0], a[1][1], a[2][2]);
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) vectors[c][r] = float(v[r][c]);  // column c = eigenvector c
}

void decomposeCovariance(const glm::mat3& cov, glm::vec3& scale, glm::quat& rotation) {
  glm::vec3 values;
  glm::mat3 R;
  symmetricEigen(cov, values, R);
  scale = glm::sqrt(glm::max(values, glm::vec3(1e-20f)));
  if (glm::determinant(R) < 0.0f) R[2] = -R[2];  // reflection -> proper rotation
  rotation = glm::normalize(glm::quat_cast(R));
}

void transformGaussian(GaussianSphere& g, const glm::mat4& affine) {
  g.position = glm::vec3(affine * glm::vec4(g.position, 1.0f));
  glm::mat3 L(affine);
  setCovariance(g, L * covariance(g) * glm::transpose(L));
}

// ---------- Regions ----------

bool Region::contains(const glm::vec3& modelPos) const {
  glm::vec3 w = glm::vec3(modelMatrix * glm::vec4(modelPos, 1.0f));
  glm::vec3 d = glm::abs(w - center);
  return d.x <= halfSize.x && d.y <= halfSize.y && d.z <= halfSize.z;
}

// ---------- Edits ----------

EditRecord moveRegion(Gaussians& gs, const Region& region, const glm::vec3& translation, float yawDeg) {
  // World-space rigid motion about the region centre, conjugated into model space:
  //   E = M^-1 * T(c + t) * Ry(yaw) * T(-c) * M
  glm::mat4 world = glm::translate(glm::mat4(1.0f), region.center + translation) *
                    glm::rotate(glm::mat4(1.0f), glm::radians(yawDeg), glm::vec3(0, 1, 0)) *
                    glm::translate(glm::mat4(1.0f), -region.center);
  glm::mat4 E = glm::inverse(region.modelMatrix) * world * region.modelMatrix;

  BoundsAccumulator src, dst;
  for (auto& g : gs) {
    if (!region.contains(g.position)) continue;
    src.add(g.position);
    transformGaussian(g, E);
    dst.add(g.position);
  }
  EditRecord r;
  r.type = "move";
  r.params = fmt("t=(%.3f, %.3f, %.3f) yaw=%.1f deg", translation.x, translation.y, translation.z, yawDeg);
  r.affected = src.count;
  r.hasBounds = r.hasDst = src.count > 0;
  r.srcMin = src.mn, r.srcMax = src.mx, r.dstMin = dst.mn, r.dstMax = dst.mx;
  return r;
}

EditRecord deleteRegion(Gaussians& gs, const Region& region) {
  BoundsAccumulator src;
  auto it = std::remove_if(gs.begin(), gs.end(), [&](const GaussianSphere& g) {
    if (!region.contains(g.position)) return false;
    src.add(g.position);
    return true;
  });
  gs.erase(it, gs.end());
  EditRecord r;
  r.type = "delete";
  r.affected = src.count;
  r.hasBounds = src.count > 0;
  r.srcMin = src.mn, r.srcMax = src.mx;
  return r;
}

EditRecord duplicateRegion(Gaussians& gs, const Region& region, const glm::vec3& offset) {
  glm::mat4 E = glm::inverse(region.modelMatrix) * glm::translate(glm::mat4(1.0f), offset) * region.modelMatrix;
  BoundsAccumulator src, dst;
  size_t n = gs.size();
  for (size_t i = 0; i < n; ++i) {
    if (!region.contains(gs[i].position)) continue;
    GaussianSphere copy = gs[i];
    src.add(copy.position);
    transformGaussian(copy, E);
    copy.index = -1;
    dst.add(copy.position);
    gs.push_back(copy);
  }
  EditRecord r;
  r.type = "duplicate";
  r.params = fmt("offset=(%.3f, %.3f, %.3f)", offset.x, offset.y, offset.z);
  r.affected = src.count;
  r.hasBounds = r.hasDst = src.count > 0;
  r.srcMin = src.mn, r.srcMax = src.mx, r.dstMin = dst.mn, r.dstMax = dst.mx;
  return r;
}

EditRecord jitterAll(Gaussians& gs, float posSigma, float scaleSigma, uint32_t seed) {
  std::mt19937 rng(seed);
  std::normal_distribution<float> posNoise(0.0f, std::max(posSigma, 0.0f));
  std::normal_distribution<float> scaleNoise(0.0f, std::max(scaleSigma, 0.0f));
  for (auto& g : gs) {
    if (posSigma > 0) g.position += glm::vec3(posNoise(rng), posNoise(rng), posNoise(rng));
    if (scaleSigma > 0) {
      glm::vec3 s;
      glm::quat q;
      decomposeCovariance(covariance(g), s, q);
      s *= glm::exp(glm::vec3(scaleNoise(rng), scaleNoise(rng), scaleNoise(rng)));
      setCovariance(g, covarianceFromScaleRotation(s, q));
    }
  }
  EditRecord r;
  r.type = "jitter";
  r.params = fmt("posSigma=%.4f scaleSigma=%.3f seed=%.0f", posSigma, scaleSigma, seed);
  r.affected = int(gs.size());
  r.geometric = false;  // no physical change
  return r;
}

EditRecord splitRandom(Gaussians& gs, float fraction, uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> uni(0.0f, 1.0f);
  const float shrink = 0.6f;  // child std-dev along the split axis, relative to the parent
  size_t n = gs.size();
  int splits = 0;
  gs.reserve(n + size_t(n * fraction * 1.2f) + 16);
  for (size_t i = 0; i < n; ++i) {
    if (uni(rng) >= fraction) continue;
    GaussianSphere& g = gs[i];
    glm::vec3 values;
    glm::mat3 V;
    glm::mat3 cov = covariance(g);
    symmetricEigen(cov, values, V);
    int k = (values.x >= values.y && values.x >= values.z) ? 0 : (values.y >= values.z ? 1 : 2);
    float lambda = std::max(values[k], 0.0f);
    glm::vec3 v = V[k];
    glm::vec3 delta = 0.5f * std::sqrt(lambda) * v;
    // Sigma' = Sigma - (1 - shrink^2) * lambda * v v^T  (shrinks only along the major axis)
    glm::mat3 childCov = cov - (1.0f - shrink * shrink) * lambda * glm::outerProduct(v, v);
    float childOpacity = 1.0f - std::sqrt(std::max(1.0f - g.opacity, 0.0f));

    GaussianSphere child = g;
    child.position = g.position - delta;
    child.opacity = childOpacity;
    child.index = -1;
    setCovariance(child, childCov);

    g.position += delta;
    g.opacity = childOpacity;
    setCovariance(g, childCov);
    gs.push_back(child);  // may reallocate: `g` is not used after this
    ++splits;
  }
  EditRecord r;
  r.type = "split";
  r.params = fmt("fraction=%.3f seed=%.0f", fraction, seed);
  r.affected = splits;
  r.geometric = false;
  return r;
}

EditRecord pruneFaint(Gaussians& gs, float fraction, float maxOpacity, uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> uni(0.0f, 1.0f);
  size_t before = gs.size();
  auto it = std::remove_if(gs.begin(), gs.end(),
                           [&](const GaussianSphere& g) { return g.opacity < maxOpacity && uni(rng) < fraction; });
  gs.erase(it, gs.end());
  EditRecord r;
  r.type = "prune";
  r.params = fmt("fraction=%.3f maxOpacity=%.3f seed=%.0f", fraction, maxOpacity, seed);
  r.affected = int(before - gs.size());
  r.geometric = false;
  return r;
}

EditRecord colourShift(Gaussians& gs, const glm::vec3& gain, const glm::vec3& offset, const Region* region) {
  BoundsAccumulator src;
  for (auto& g : gs) {
    if (region && !region->contains(g.position)) continue;
    g.color = gain * g.color + offset;
    src.add(g.position);
  }
  EditRecord r;
  r.type = "colour";
  r.params = fmt("gain=(%.2f, %.2f, %.2f) offset=%.2f", gain.x, gain.y, gain.z, offset.x);
  r.affected = src.count;
  r.geometric = false;
  r.hasBounds = region != nullptr && src.count > 0;
  r.srcMin = src.mn, r.srcMax = src.mx;
  return r;
}

// ---------- I/O ----------

Gaussians loadPly(const std::string& path) {
  happly::PLYData ply(path);
  auto& v = ply.getElement("vertex");
  auto x = v.getProperty<float>("x"), y = v.getProperty<float>("y"), z = v.getProperty<float>("z");
  auto r = v.getProperty<float>("f_dc_0"), gr = v.getProperty<float>("f_dc_1"), b = v.getProperty<float>("f_dc_2");
  auto s0 = v.getProperty<float>("scale_0"), s1 = v.getProperty<float>("scale_1"), s2 = v.getProperty<float>("scale_2");
  auto q0 = v.getProperty<float>("rot_0"), q1 = v.getProperty<float>("rot_1"), q2 = v.getProperty<float>("rot_2"),
       q3 = v.getProperty<float>("rot_3");
  auto op = v.getProperty<float>("opacity");

  Gaussians gs(x.size());
  for (size_t i = 0; i < x.size(); ++i) {
    GaussianSphere& g = gs[i];
    g.position = glm::vec3(x[i], y[i], z[i]);
    g.color = glm::vec3(0.5f + kShC0 * r[i], 0.5f + kShC0 * gr[i], 0.5f + kShC0 * b[i]);
    glm::vec3 scale(std::exp(s0[i]), std::exp(s1[i]), std::exp(s2[i]));
    setCovariance(g, covarianceFromScaleRotation(scale, glm::quat(q0[i], q1[i], q2[i], q3[i])));  // (w, x, y, z)
    g.opacity = 1.0f / (1.0f + std::exp(-op[i]));
    g.index = int(i);
  }
  return gs;
}

void savePly(const std::string& path, const Gaussians& gs) {
  static const char* props[] = {"x",       "y",      "z",      "nx",      "ny",      "nz",
                                "f_dc_0",  "f_dc_1", "f_dc_2", "opacity", "scale_0", "scale_1",
                                "scale_2", "rot_0",  "rot_1",  "rot_2",   "rot_3"};
  const int P = 17;
  std::ofstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot write " + path);
  f << "ply\nformat binary_little_endian 1.0\n";
  f << "comment written by SplatDiff (scale/rotation recovered from covariance, SH DC only)\n";
  f << "element vertex " << gs.size() << "\n";
  for (const char* p : props) f << "property float " << p << "\n";
  f << "end_header\n";

  std::vector<float> row(P);
  for (const auto& g : gs) {
    glm::vec3 scale;
    glm::quat q;
    decomposeCovariance(covariance(g), scale, q);
    float a = std::clamp(g.opacity, 1e-6f, 1.0f - 1e-6f);
    float vals[P] = {g.position.x,
                     g.position.y,
                     g.position.z,
                     0,
                     0,
                     0,
                     (g.color.r - 0.5f) / kShC0,
                     (g.color.g - 0.5f) / kShC0,
                     (g.color.b - 0.5f) / kShC0,
                     std::log(a / (1.0f - a)),
                     std::log(scale.x),
                     std::log(scale.y),
                     std::log(scale.z),
                     q.w,
                     q.x,
                     q.y,
                     q.z};
    f.write(reinterpret_cast<const char*>(vals), sizeof(vals));  // arm64 / x86 are little-endian
  }
}

void saveEditLog(const std::string& path, const std::string& sourcePly, const std::vector<EditRecord>& log) {
  using nlohmann::json;
  auto vec = [](const glm::vec3& v) { return json::array({v.x, v.y, v.z}); };
  json j;
  j["source"] = sourcePly;
  j["note"] =
      "Ground truth for SplatDiff. Bounds are in file (model) space. Edits with geometric=false "
      "(jitter, split, prune, colour) must NOT be detected as change.";
  j["edits"] = json::array();
  for (const auto& r : log) {
    json e = {{"type", r.type}, {"params", r.params}, {"affected", r.affected}, {"geometric", r.geometric}};
    if (r.hasBounds) e["src_bounds"] = {{"min", vec(r.srcMin)}, {"max", vec(r.srcMax)}};
    if (r.hasDst) e["dst_bounds"] = {{"min", vec(r.dstMin)}, {"max", vec(r.dstMax)}};
    j["edits"].push_back(e);
  }
  std::ofstream(path) << j.dump(2) << "\n";
}

void bounds(const Gaussians& gs, glm::vec3& mn, glm::vec3& mx) {
  BoundsAccumulator b;
  for (const auto& g : gs) b.add(g.position);
  mn = b.mn;
  mx = b.mx;
}

}  // namespace splatdiff
