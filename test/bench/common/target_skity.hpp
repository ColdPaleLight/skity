// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#pragma once

#include <cassert>
#include <memory>
#include <skity/gpu/gpu_backend_type.hpp>
#include <skity/gpu/gpu_context.hpp>

#include "test/bench/common/target.hpp"

namespace skity {
namespace bench {
class TargetSkity : public Target {
 public:
  TargetSkity(skity::GPUContext *context,
              std::unique_ptr<skity::GPUSurface> surface, Options options);

  Canvas *LockCanvas() override;

  void Flush() override;

 private:
  skity::GPUContext *context_;
  std::unique_ptr<skity::GPUSurface> surface_;
  Canvas *backend_canvas_;
};
}  // namespace bench
}  // namespace skity
