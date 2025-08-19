// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include "test/bench/common/context.hpp"

#include "test/bench/common/context_gl_skity.hpp"
#include "test/bench/common/context_mtl_skity.h"
#include "test/bench/common/gl_context_mac.h"

namespace skity {
namespace bench {

std::shared_ptr<Context> Context::Create(RenderingType type) {
  if (RenderingType::kMetal == type) {
    return CreateContextMTLSkity();
  }
  if (RenderingType::kOpenGL == type) {
    auto gl_context = CreateGLContextMac();
    gl_context->MakeCurrent();
    return CreateContextGLSkity(GetGLProcLoader());
  }
  return nullptr;
}

}  // namespace bench
}  // namespace skity
