// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#ifndef TEST_BENCHMARK_COMMON_CONTEXT_HPP
#define TEST_BENCHMARK_COMMON_CONTEXT_HPP

#include <memory>

#include "test/bench/common/target.hpp"

namespace skity {
namespace bench {

enum class RenderingType {
  kOpenGL,
  kMetal,
};

class Context {
 public:
  static std::shared_ptr<Context> Create(RenderingType type);

  virtual ~Context() = default;

  virtual std::shared_ptr<Target> CreateTarget(Target::Options options) = 0;

  virtual bool WriteToFile(std::shared_ptr<Target> target,
                           std::string path) = 0;
};

}  // namespace bench
}  // namespace skity

#endif  // TEST_BENCHMARK_COMMON_CONTEXT_HPP
