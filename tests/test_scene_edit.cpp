// Unit tests for the SplatDiff scene-editing maths. Run: ./build/splatdiff_tests
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <random>

#include "ChangeDetect.hpp"
#include "SceneEdit.hpp"

using namespace splatdiff;

static int failures = 0;
#define CHECK(cond, ...)                               \
  do {                                                 \
    if (!(cond)) {                                     \
      ++failures;                                      \
      std::printf("FAIL %s:%d  ", __FILE__, __LINE__); \
      std::printf(__VA_ARGS__);                        \
      std::printf("\n");                               \
    }                                                  \
  } while (0)

static float maxAbs(const glm::mat3& a, const glm::mat3& b) {
  float m = 0;
  for (int c = 0; c < 3; ++c)
    for (int r = 0; r < 3; ++r) m = std::max(m, std::fabs(a[c][r] - b[c][r]));
  return m;
}

static GaussianSphere randomGaussian(std::mt19937& rng) {
  std::uniform_real_distribution<float> u(-1, 1), s(0.01f, 0.5f);
  GaussianSphere g{};
  g.position = glm::vec3(u(rng), u(rng), u(rng)) * 3.0f;
  g.color = glm::vec3(0.2f, 0.5f, 0.8f);
  g.opacity = 0.7f;
  glm::quat q = glm::normalize(glm::quat(u(rng), u(rng), u(rng), u(rng)));
  setCovariance(g, covarianceFromScaleRotation(glm::vec3(s(rng), s(rng), s(rng)), q));
  return g;
}

static void testDecomposeRoundTrip() {
  std::mt19937 rng(1);
  for (int t = 0; t < 1000; ++t) {
    GaussianSphere g = randomGaussian(rng);
    glm::mat3 cov = covariance(g);
    glm::vec3 s;
    glm::quat q;
    decomposeCovariance(cov, s, q);
    float err = maxAbs(cov, covarianceFromScaleRotation(s, q));
    CHECK(err < 1e-5f, "decompose round trip error %g", err);
  }
}

static void testRotationPreservesShape() {
  // Sigma' = R Sigma R^T keeps the eigenvalues (the ellipsoid's axis lengths).
  std::mt19937 rng(2);
  for (int t = 0; t < 200; ++t) {
    GaussianSphere g = randomGaussian(rng);
    glm::vec3 before, after;
    glm::mat3 V;
    symmetricEigen(covariance(g), before, V);
    glm::mat4 R = glm::rotate(glm::mat4(1.0f), 0.7f + t * 0.01f, glm::normalize(glm::vec3(1, 2, 3)));
    transformGaussian(g, glm::translate(glm::mat4(1.0f), glm::vec3(1, 2, 3)) * R);
    symmetricEigen(covariance(g), after, V);
    auto sorted = [](glm::vec3 v) {
      float a[3] = {v.x, v.y, v.z};
      std::sort(a, a + 3);
      return glm::vec3(a[0], a[1], a[2]);
    };
    float err = glm::length(sorted(before) - sorted(after));
    CHECK(err < 1e-5f, "eigenvalues changed under rotation by %g", err);
  }
}

static void testMoveOnlyTouchesRegion() {
  std::mt19937 rng(3);
  Gaussians gs;
  for (int i = 0; i < 2000; ++i) gs.push_back(randomGaussian(rng));
  Gaussians before = gs;
  Region box;
  box.center = glm::vec3(0.5f, 0, 0);
  box.halfSize = glm::vec3(1.0f);
  EditRecord r = moveRegion(gs, box, glm::vec3(10, 0, 0), 0.0f);
  int inside = 0;
  for (size_t i = 0; i < gs.size(); ++i) {
    bool was = box.contains(before[i].position);
    inside += was;
    glm::vec3 expected = before[i].position + (was ? glm::vec3(10, 0, 0) : glm::vec3(0));
    CHECK(glm::length(gs[i].position - expected) < 1e-5f, "gaussian %zu moved wrongly", i);
    CHECK(maxAbs(covariance(gs[i]), covariance(before[i])) < 1e-6f, "pure translation changed covariance");
  }
  CHECK(r.affected == inside && inside > 0, "affected=%d inside=%d", r.affected, inside);
}

static void testModelMatrixRegion() {
  // A region in world space with a rotated model: membership must use M * position.
  GaussianSphere g{};
  g.position = glm::vec3(1, 0, 0);
  setCovariance(g, glm::mat3(0.01f));
  Region box;
  box.modelMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));  // x -> -z
  box.center = glm::vec3(0, 0, -1);
  box.halfSize = glm::vec3(0.1f);
  CHECK(box.contains(g.position), "rotated model point should be inside the world box");
  Gaussians gs{g};
  moveRegion(gs, box, glm::vec3(0, 2, 0), 0.0f);  // world +y is model +y here
  CHECK(glm::length(gs[0].position - glm::vec3(1, 2, 0)) < 1e-5f, "move in rotated model space wrong");
}

static void testSplitKeepsCentreAndOpacity() {
  std::mt19937 rng(4);
  Gaussians gs;
  for (int i = 0; i < 500; ++i) gs.push_back(randomGaussian(rng));
  glm::vec3 c0(0);
  for (auto& g : gs) c0 += g.position;
  EditRecord r = splitRandom(gs, 1.0f, 7);
  CHECK(r.affected == 500 && gs.size() == 1000, "split count %d size %zu", r.affected, gs.size());
  glm::vec3 c1(0);
  for (auto& g : gs) c1 += g.position;
  CHECK(glm::length(c0 - 0.5f * c1) < 1e-3f, "split moved the centroid");
  // two children of opacity a' composite to 1 - (1 - a')^2 = a
  float a = gs[0].opacity;
  CHECK(std::fabs(1.0f - (1.0f - a) * (1.0f - a) - 0.7f) < 1e-5f, "split opacity %g", a);
}

static void testDeleteAndDuplicate() {
  std::mt19937 rng(5);
  Gaussians gs;
  for (int i = 0; i < 1000; ++i) gs.push_back(randomGaussian(rng));
  Region box;
  box.halfSize = glm::vec3(1.0f);
  Gaussians dup = gs;
  EditRecord d = duplicateRegion(dup, box, glm::vec3(5, 0, 0));
  CHECK(dup.size() == gs.size() + size_t(d.affected) && d.affected > 0, "duplicate size");
  EditRecord del = deleteRegion(gs, box);
  CHECK(del.affected == d.affected, "delete %d vs duplicate %d", del.affected, d.affected);
  for (auto& g : gs) CHECK(!box.contains(g.position), "gaussian left inside deleted box");
}

static void testPlyRoundTrip() {
  std::mt19937 rng(6);
  Gaussians gs;
  for (int i = 0; i < 300; ++i) gs.push_back(randomGaussian(rng));
  const char* path = "/tmp/splatdiff_test_roundtrip.ply";
  savePly(path, gs);
  Gaussians back = loadPly(path);
  CHECK(back.size() == gs.size(), "size %zu vs %zu", back.size(), gs.size());
  for (size_t i = 0; i < std::min(back.size(), gs.size()); ++i) {
    CHECK(glm::length(back[i].position - gs[i].position) < 1e-6f, "position %zu", i);
    CHECK(glm::length(back[i].color - gs[i].color) < 1e-5f, "colour %zu", i);
    CHECK(std::fabs(back[i].opacity - gs[i].opacity) < 1e-5f, "opacity %zu", i);
    CHECK(maxAbs(covariance(back[i]), covariance(gs[i])) < 1e-5f, "covariance %zu", i);
  }
  std::remove(path);
}

// A small room: a floor sheet plus two solid blobs, for detector tests.
static Gaussians makeRoom() {
  std::mt19937 rng(11);
  std::uniform_real_distribution<float> u(-1, 1);
  Gaussians gs;
  auto blob = [&](glm::vec3 c, float r, int n) {
    for (int i = 0; i < n; ++i) {
      GaussianSphere g{};
      glm::vec3 d = glm::normalize(glm::vec3(u(rng), u(rng), u(rng)));
      g.position = c + d * r;
      g.color = glm::vec3(0.8f, 0.2f, 0.2f);
      g.opacity = 0.9f;
      setCovariance(g, glm::mat3(0.03f * 0.03f));
      gs.push_back(g);
    }
  };
  for (int i = 0; i < 8000; ++i) {
    GaussianSphere g{};
    g.position = glm::vec3(u(rng) * 3, 0, u(rng) * 3);
    g.color = glm::vec3(0.5f);
    g.opacity = 0.9f;
    setCovariance(g, covarianceFromScaleRotation(glm::vec3(0.05f, 0.003f, 0.05f), glm::quat(1, 0, 0, 0)));
    gs.push_back(g);
  }
  blob(glm::vec3(-1.5f, 0.5f, 0), 0.4f, 4000);
  blob(glm::vec3(1.5f, 0.5f, 0), 0.4f, 4000);
  return gs;
}

static void testDetectorNoChange() {
  Gaussians a = makeRoom(), b = a;
  colourShift(b, glm::vec3(0.5f, 1.2f, 1.5f), glm::vec3(0.1f), nullptr);
  jitterAll(b, 0.01f, 0.1f, 3);
  splitRandom(b, 0.3f, 4);
  DetectParams p;
  p.resolution = 64;
  DetectResult r = detectChanges(a, b, p);
  CHECK(r.regions.empty(), "unchanged scene produced %zu regions (%zu voxels)", r.regions.size(), r.changedVoxels);
}

static void testDetectorFindsDeletion() {
  Gaussians a = makeRoom(), b = a;
  Region box;
  box.center = glm::vec3(1.5f, 0.5f, 0);
  box.halfSize = glm::vec3(0.5f, 0.45f, 0.5f);
  std::vector<EditRecord> log{deleteRegion(b, box)};
  DetectParams p;
  p.resolution = 64;
  DetectResult r = detectChanges(a, b, p);
  CHECK(r.regions.size() == 1, "expected 1 region, got %zu", r.regions.size());
  if (!r.regions.empty()) {
    CHECK(r.regions[0].type == "removed", "type %s", r.regions[0].type.c_str());
    CHECK(glm::length(r.regions[0].centroid - box.center) < 0.15f, "region centre off by %g",
          glm::length(r.regions[0].centroid - box.center));
  }
  Evaluation ev = evaluate(r, groundTruthBoxes(log));
  CHECK(ev.gtDetected == 1 && ev.falseRegions == 0 && ev.falseVoxels == 0, "eval %d/%d false %d", ev.gtDetected,
        ev.gtBoxes, ev.falseRegions);
}

static void testDetectorPairsMove() {
  Gaussians a = makeRoom(), b = a;
  Region box;
  box.center = glm::vec3(-1.5f, 0.5f, 0);
  box.halfSize = glm::vec3(0.5f, 0.45f, 0.5f);
  moveRegion(b, box, glm::vec3(0, 0, 1.8f), 0.0f);
  DetectParams p;
  p.resolution = 64;
  DetectResult r = detectChanges(a, b, p);
  int moved = 0;
  for (auto& c : r.regions) moved += c.type.rfind("moved", 0) == 0;
  CHECK(r.regions.size() == 2 && moved == 2, "expected a moved pair, got %zu regions (%d moved)", r.regions.size(),
        moved);
}

static void testParallelMatchesSerial() {
  Gaussians a = makeRoom(), b = a;
  Region box;
  box.center = glm::vec3(1.5f, 0.5f, 0);
  box.halfSize = glm::vec3(0.5f);
  deleteRegion(b, box);
  DetectParams p;
  p.resolution = 64;
  p.mode = ExecMode::Serial;
  DetectResult s = detectChanges(a, b, p);
  for (ExecMode m : {ExecMode::OpenMPAtomic, ExecMode::OpenMPPrivate}) {
    p.mode = m;
    DetectResult r = detectChanges(a, b, p);
    float err = 0;
    size_t diff = 0;
    for (size_t v = 0; v < s.fieldA.size(); ++v) {
      err = std::max(err, std::fabs(s.fieldA[v] - r.fieldA[v]));
      err = std::max(err, std::fabs(s.fieldB[v] - r.fieldB[v]));
      diff += s.label[v] != r.label[v];
    }
    CHECK(err < 1e-4f && diff == 0, "%s differs from serial: max err %g, %zu labels", execModeName(m), err, diff);
  }
}

int main() {
  testDecomposeRoundTrip();
  testRotationPreservesShape();
  testMoveOnlyTouchesRegion();
  testModelMatrixRegion();
  testSplitKeepsCentreAndOpacity();
  testDeleteAndDuplicate();
  testPlyRoundTrip();
  testDetectorNoChange();
  testDetectorFindsDeletion();
  testDetectorPairsMove();
  testParallelMatchesSerial();
  if (failures == 0) std::printf("All SplatDiff tests passed.\n");
  return failures == 0 ? 0 : 1;
}
