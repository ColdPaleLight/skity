// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include "test/bench/common/target_skity.hpp"

namespace skity {
namespace bench {

TargetSkity::TargetSkity(skity::GPUContext *context,
                         std::unique_ptr<skity::GPUSurface> surface,
                         Options options)
    : Target(options), context_(context), surface_(std::move(surface)) {}

Canvas *TargetSkity::LockCanvas() {
  backend_canvas_ = surface_->LockCanvas();
  return backend_canvas_;
}

void TargetSkity::Flush() {
  backend_canvas_->Flush();
  surface_->Flush();
}

}  // namespace bench
}  // namespace skity
