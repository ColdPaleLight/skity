// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#ifndef TEST_BENCHMARK_COMMON_TARGET_MTL_SKITY_H
#define TEST_BENCHMARK_COMMON_TARGET_MTL_SKITY_H

#import <Metal/Metal.h>

#include <cassert>
#include <memory>
#include <skity/gpu/gpu_backend_type.hpp>
#include <skity/gpu/gpu_context.hpp>

#include "test/bench/common/target_skity.hpp"

namespace skity {
namespace bench {

class TargetMTLSkity : public TargetSkity {
 public:
  static std::shared_ptr<Target> Create(skity::GPUContext *context,
                                        Options options);

  TargetMTLSkity(skity::GPUContext *context,
                 std::unique_ptr<skity::GPUSurface> surface, Options options,
                 id<MTLTexture> texture);

  id<MTLTexture> GetTexture() const { return texture_; }

 private:
  id<MTLTexture> texture_;
};
}  // namespace bench
}  // namespace skity

#endif  // TEST_BENCHMARK_COMMON_TARGET_MTL_SKITY_H
