#pragma once
// SplatDiff scene editing: everything needed to build "scan B" from "scan A".
//
// These functions only touch the CPU copy (std::vector<GaussianSphere>), so they are shared by the
// interactive viewer and the command-line tool `splatdiff_edit`, and can be unit tested without OpenGL.
// Every edit returns an EditRecord describing what changed; the list of records is the ground truth
// that the change detector (milestone 2) is evaluated against.

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

#include "Gaussian.hpp"

namespace splatdiff {

using Gaussians = std::vector<GaussianSphere>;

// ---------- Covariance helpers ----------

// Full symmetric 3x3 covariance from the packed (covA, covB) form.
glm::mat3 covariance(const GaussianSphere& g);
void setCovariance(GaussianSphere& g, const glm::mat3& cov);

// Sigma = R S S^T R^T, with S = diag(scale) and R from a unit quaternion.
glm::mat3 covarianceFromScaleRotation(const glm::vec3& scale, const glm::quat& rotation);

// Inverse of the above: eigen-decomposition of Sigma (Jacobi). scale = sqrt(eigenvalues),
// rotation = eigenvectors (forced to a proper rotation, det = +1).
void decomposeCovariance(const glm::mat3& cov, glm::vec3& scale, glm::quat& rotation);

// Symmetric 3x3 eigen-decomposition: cov = V diag(values) V^T, columns of V are eigenvectors.
void symmetricEigen(const glm::mat3& m, glm::vec3& values, glm::mat3& vectors);

// Apply an affine transform to one Gaussian: mu' = A mu + t, Sigma' = L Sigma L^T (L = linear part of A).
void transformGaussian(GaussianSphere& g, const glm::mat4& affine);

// ---------- Regions ----------

// An axis-aligned box in WORLD space. Gaussians are stored in model (file) space and drawn with
// `modelMatrix`, so membership is tested on modelMatrix * position.
struct Region {
  glm::vec3 center{0.0f};
  glm::vec3 halfSize{0.5f};
  glm::mat4 modelMatrix{1.0f};

  bool contains(const glm::vec3& modelPos) const;
};

// ---------- Edit log (ground truth) ----------

struct EditRecord {
  std::string type;                      // move, delete, duplicate, jitter, split, prune, colour
  std::string params;                    // human-readable parameters
  int affected = 0;                      // number of Gaussians touched / removed / created
  bool geometric = true;                 // false for colour-only edits (should NOT be detected as change)
  bool hasBounds = false;                // true when the edit is local (has a ground-truth region)
  glm::vec3 srcMin{0.0f}, srcMax{0.0f};  // file-space bounds of the touched Gaussians before the edit
  bool hasDst = false;
  glm::vec3 dstMin{0.0f}, dstMax{0.0f};  // ... and after the edit (move / duplicate)
};

// ---------- Edits ----------

// Rigidly move the Gaussians inside `region`: rotate by `yawDeg` about the world-up axis through the
// region centre, then translate by `translation` (world units).
EditRecord moveRegion(Gaussians& gs, const Region& region, const glm::vec3& translation, float yawDeg);

// Remove every Gaussian inside `region`.
EditRecord deleteRegion(Gaussians& gs, const Region& region);

// Copy the Gaussians inside `region` and place the copy at `offset` (world units).
EditRecord duplicateRegion(Gaussians& gs, const Region& region, const glm::vec3& offset);

// Re-scan noise on every Gaussian: position noise N(0, posSigma) and multiplicative scale noise
// exp(N(0, scaleSigma)) per axis. The physical scene is unchanged, so this should NOT be detected.
EditRecord jitterAll(Gaussians& gs, float posSigma, float scaleSigma, uint32_t seed);

// Split a random `fraction` of Gaussians into two children along their major axis (as the 3DGS
// densification step does). Children sit at mu +/- 0.5*sqrt(lambda)*v, shrink along v, and take
// opacity 1 - sqrt(1 - alpha) each so the pair is about as opaque as the parent. Same geometry, so
// this should NOT be detected as change.
EditRecord splitRandom(Gaussians& gs, float fraction, uint32_t seed);

// Remove a random `fraction` of the faint Gaussians (opacity < maxOpacity). Mimics a re-scan that
// kept fewer near-transparent splats; should NOT be detected.
EditRecord pruneFaint(Gaussians& gs, float fraction, float maxOpacity, uint32_t seed);

// Lighting change: color' = gain * color + offset. If `region` is null, applies to every Gaussian.
// Appearance only, so this should NOT be detected.
EditRecord colourShift(Gaussians& gs, const glm::vec3& gain, const glm::vec3& offset, const Region* region);

// ---------- I/O ----------

// Load a standard 3DGS .ply (x, y, z, f_dc_*, opacity, scale_*, rot_*). Throws std::runtime_error.
Gaussians loadPly(const std::string& path);

// Write a standard 3DGS .ply (binary). Scale/rotation are recovered from the covariance, colours are
// written as the SH DC term only (f_rest is not kept).
void savePly(const std::string& path, const Gaussians& gs);

// Write the edit log as JSON next to a saved scan (ground truth for the change detector).
void saveEditLog(const std::string& path, const std::string& sourcePly, const std::vector<EditRecord>& log);

// File-space bounding box of a set of Gaussians.
void bounds(const Gaussians& gs, glm::vec3& mn, glm::vec3& mx);

}  // namespace splatdiff
