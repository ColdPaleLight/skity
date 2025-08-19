// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#ifndef TEST_BENCHMARK_COMMON_CONTEXT_GL_SKITY_HPP
#define TEST_BENCHMARK_COMMON_CONTEXT_GL_SKITY_HPP

#include "test/bench/common/context.hpp"

namespace skity {
namespace bench {

std::shared_ptr<Context> CreateContextGLSkity(void *proc_loader);

}  // namespace bench
}  // namespace skity
#endif  // TEST_BENCHMARK_COMMON_CONTEXT_GL_SKITY_HPP
