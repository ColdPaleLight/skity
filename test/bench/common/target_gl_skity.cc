// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include "test/bench/common/target_gl_skity.hpp"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <cassert>
#include <skity/gpu/gpu_context_gl.hpp>

namespace skity {
namespace bench {

std::shared_ptr<Target> TargetGLSkity::Create(skity::GPUContext *context,
                                              Options options) {
  GLuint texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, options.width, options.height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

  skity::GPUSurfaceDescriptorGL desc;
  desc.backend = skity::GPUBackendType::kOpenGL;
  desc.width = options.width;
  desc.height = options.height;
  desc.gl_id = texture;
  desc.surface_type = skity::GLSurfaceType::kTexture;
  desc.has_stencil_attachment = false;
  desc.sample_count = options.msaa ? 4 : 1;
  auto surface = context->CreateSurface(&desc);
  assert(surface.get() != nullptr);
  return std::make_shared<TargetGLSkity>(context, std::move(surface), options,
                                         texture);
}

TargetGLSkity::TargetGLSkity(skity::GPUContext *context,
                             std::unique_ptr<skity::GPUSurface> surface,
                             Options options, uint32_t texture)
    : TargetSkity(context, std::move(surface), options), texture_(texture) {}

TargetGLSkity::~TargetGLSkity() {
  if (texture_) {
    glDeleteTextures(1, &texture_);
  }
  texture_ = 0;
}

}  // namespace bench
}  // namespace skity
