// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#ifndef TEST_BENCHMARK_COMMON_TARGET_HPP
#define TEST_BENCHMARK_COMMON_TARGET_HPP

#include <skity/skity.hpp>

namespace skity {
namespace bench {

class Target {
 public:
  struct Options {
    uint32_t width;
    uint32_t height;
    bool msaa = false;
    bool force_disable_aa = false;
  };

  Target(Options options) : width_(options.width), height_(options.height) {}

  virtual ~Target() = default;

  virtual Canvas *LockCanvas() = 0;

  virtual void Flush() = 0;

  int32_t GetWidth() const { return width_; }
  int32_t GetHeight() const { return height_; }

 private:
  uint32_t width_;
  uint32_t height_;
};

}  // namespace bench
}  // namespace skity

#endif  // TEST_BENCHMARK_COMMON_TARGET_HPP
