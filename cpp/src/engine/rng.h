// Seeded PRNG -- mulberry32, the same generator the JS engine uses, so a seed
// means the same thing across the browser build and this one where the two
// make the same sequence of draws.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace sl {

class SeededRandom {
 public:
  explicit SeededRandom(uint32_t seed) : state_(seed) {}

  double random() {
    state_ += 0x6d2b79f5u;
    uint32_t t = state_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }
  double uniform(double a, double b) { return a + (b - a) * random(); }
  int randint(int a, int b) { return a + static_cast<int>(random() * (b - a + 1)); }
  size_t index(size_t n) { return static_cast<size_t>(random() * static_cast<double>(n)); }

  template <typename T>
  void shuffle(std::vector<T>& v) {
    for (size_t i = v.size(); i-- > 1;) {
      size_t j = index(i + 1);
      std::swap(v[i], v[j]);
    }
  }

 private:
  uint32_t state_;
};

}  // namespace sl
