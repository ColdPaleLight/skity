
// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#ifndef TEST_BENCHMARK_COMMON_COLOR_UTILS_HPP
#define TEST_BENCHMARK_COMMON_COLOR_UTILS_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace skity {
namespace bench {

inline void UnpremultiplyAlpha(uint8_t *data, size_t pixelCount) {
  for (size_t i = 0; i < pixelCount; ++i) {
    uint8_t *px = data + i * 4;

    uint8_t A = px[3];
    if (A == 0 || A == 255) {
      continue;
    }

    px[0] =
        static_cast<uint8_t>(std::min(255, (px[0] * 255 + A / 2) / A));  // R
    px[1] =
        static_cast<uint8_t>(std::min(255, (px[1] * 255 + A / 2) / A));  // G
    px[2] =
        static_cast<uint8_t>(std::min(255, (px[2] * 255 + A / 2) / A));  // B
  }
}
}  // namespace bench
}  // namespace skity

#endif  // TEST_BENCHMARK_COMMON_COLOR_UTILS_HPP
