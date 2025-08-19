// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.
#ifndef TEST_BENCHMARK_COMMON_CONTEXT_SKITY_HPP
#define TEST_BENCHMARK_COMMON_CONTEXT_SKITY_HPP

#include "test/bench/common/context.hpp"

namespace skity {
namespace bench {

class ContextSkity : public Context {
 public:
  ContextSkity(std::unique_ptr<skity::GPUContext> gpu_context)
      : gpu_context_(std::move(gpu_context)) {}

 protected:
  std::unique_ptr<skity::GPUContext> gpu_context_;
};
}  // namespace bench
}  // namespace skity

#endif  // TEST_BENCHMARK_COMMON_CONTEXT_SKITY_HPP
