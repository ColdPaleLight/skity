

// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#pragma once

#include <cassert>
#include <memory>

#include "skity/gpu/gpu_backend_type.hpp"
#include "skity/gpu/gpu_context.hpp"
#include "test/bench/common/target.hpp"
#include "test/bench/common/target_skity.hpp"

namespace skity {
namespace bench {

class TargetGLSkity : public TargetSkity {
 public:
  static std::shared_ptr<Target> Create(skity::GPUContext *context,
                                        Options options);

  TargetGLSkity(skity::GPUContext *context,
                std::unique_ptr<skity::GPUSurface> surface, Options options,
                uint32_t texture);

  ~TargetGLSkity() override;

  uint32_t GetTexture() const { return texture_; }

 private:
  uint32_t texture_;
};

}  // namespace bench
}  // namespace skity
