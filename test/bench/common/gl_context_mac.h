// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#ifndef TEST_BENCHMARK_COMMON_GL_CONTEXT_MAC_HPP
#define TEST_BENCHMARK_COMMON_GL_CONTEXT_MAC_HPP

#include <memory>

#include "test/bench/common/gl_context.hpp"

namespace skity {
namespace bench {

std::shared_ptr<GLContext> CreateGLContextMac();

void *GetGLProcLoader();

}  // namespace bench
}  // namespace skity

#endif  // TEST_BENCHMARK_COMMON_GL_CONTEXT_MAC_HPP
