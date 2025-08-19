// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include "test/bench/common/target_mtl_skity.h"

#include <Metal/Metal.h>
#include <skity/gpu/gpu_context_mtl.h>
#include <cassert>
#include <memory>
#include <skity/gpu/gpu_backend_type.hpp>
#include <skity/gpu/texture.hpp>

namespace skity {
namespace bench {

std::shared_ptr<Target> TargetMTLSkity::Create(skity::GPUContext *context, Options options) {
  skity::GPUSurfaceDescriptorMTL desc;
  desc.backend = skity::GPUBackendType::kMetal;
  desc.width = options.width;
  desc.height = options.height;
  desc.sample_count = options.msaa ? 4 : 1;
  desc.content_scale = 1.f;

  desc.surface_type = skity::MTLSurfaceType::kTexture;
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  MTLTextureDescriptor *texture_desc = [MTLTextureDescriptor new];
  texture_desc.width = options.width;
  texture_desc.height = options.height;
  texture_desc.pixelFormat = MTLPixelFormatBGRA8Unorm;
  texture_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
  texture_desc.textureType = MTLTextureType2D;
  texture_desc.sampleCount = 1;
  texture_desc.storageMode = MTLStorageModePrivate;
  desc.texture = [device newTextureWithDescriptor:texture_desc];
  auto surface = context->CreateSurface(&desc);

  assert(surface.get() != nullptr);
  return std::make_shared<TargetMTLSkity>(context, std::move(surface), options, desc.texture);
}

TargetMTLSkity::TargetMTLSkity(skity::GPUContext *context,
                               std::unique_ptr<skity::GPUSurface> surface, Options options,
                               id<MTLTexture> texture)
    : TargetSkity(context, std::move(surface), options), texture_(texture) {}

}  // namespace bench
}  // namespace skity
