// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#ifndef TEST_BENCHMARK_COMMON_CONTEXT_MTL_SKITY_H
#define TEST_BENCHMARK_COMMON_CONTEXT_MTL_SKITY_H

#include <memory>

#include "test/bench/common/context.hpp"

namespace skity {
namespace bench {

std::shared_ptr<Context> CreateContextMTLSkity();

}  // namespace bench
}  // namespace skity

#endif  // TEST_BENCHMARK_COMMON_CONTEXT_MTL_SKITY_H
