#pragma once
// Portable random numbers. std::mt19937 gives the same sequence everywhere, but std::uniform_*_distribution and
// std::normal_distribution are implementation-defined: libc++ (macOS), libstdc++ (Linux) and MSVC turn the same
// seed into different numbers. Everything seeded in SplatDiff (edits, experiment regions) goes through this class,
// so a seed means the same scene and the same results on every platform.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>

namespace splatdiff {

class PortableRng {
 public:
  explicit PortableRng(uint32_t seed) : gen(seed) {}

  // uniform in [0, 1), 24 random bits
  float uniform() { return float(gen() >> 8) * (1.0f / 16777216.0f); }
  float uniform(float lo, float hi) { return lo + (hi - lo) * uniform(); }

  // uniform integer in [0, n)
  size_t index(size_t n) { return n == 0 ? 0 : size_t((uint64_t(gen()) * uint64_t(n)) >> 32); }

  // standard normal (Box-Muller, both values used)
  float normal() {
    if (hasSpare) {
      hasSpare = false;
      return spare;
    }
    float u1 = 0.0f;
    while (u1 <= 1e-7f) u1 = uniform();
    const float u2 = uniform();
    const float r = std::sqrt(-2.0f * std::log(u1));
    const float t = 6.28318530718f * u2;
    spare = r * std::sin(t);
    hasSpare = true;
    return r * std::cos(t);
  }
  float normal(float mean, float sigma) { return mean + sigma * normal(); }

 private:
  std::mt19937 gen;
  float spare = 0.0f;
  bool hasSpare = false;
};

}  // namespace splatdiff
