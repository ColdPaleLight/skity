// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#ifndef TEST_BENCHMARK_COMMON_GL_CONTEXT_HPP
#define TEST_BENCHMARK_COMMON_GL_CONTEXT_HPP

namespace skity {
namespace bench {
class GLContext {
 public:
  GLContext() = default;
  virtual ~GLContext() = default;
  virtual bool MakeCurrent() = 0;
  virtual bool ClearCurrent() = 0;
  virtual bool IsCurrent() = 0;
};
}  // namespace bench
}  // namespace skity

#endif  // TEST_BENCHMARK_COMMON_GL_CONTEXT_HPP
