// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include "test/bench/common/context_gl_skity.hpp"

#include <skity/codec/codec.hpp>
#include <skity/gpu/gpu_context.hpp>
#include <skity/gpu/gpu_context_gl.hpp>
#include <skity/graphic/alpha_type.hpp>
#include <skity/graphic/image.hpp>

#include "test/bench/common/color_utils.hpp"
#include "test/bench/common/context_skity.hpp"
#include "test/bench/common/target_gl_skity.hpp"

namespace skity {
namespace bench {

namespace {
void FlipY(uint8_t *addr, int width, int height) {
  auto row_bytes = width * 4;
  auto buffer = new uint8_t[row_bytes];
  for (int y = 0; y < height / 2; ++y) {
    auto top = addr + y * row_bytes;
    auto bottom = addr + (height - y - 1) * row_bytes;
    memcpy(buffer, top, row_bytes);
    memcpy(top, bottom, row_bytes);
    memcpy(bottom, buffer, row_bytes);
  }
}
}  // namespace

class ContextGLSkity : public ContextSkity {
 public:
  ContextGLSkity(std::unique_ptr<skity::GPUContext> gpu_context)
      : ContextSkity(std::move(gpu_context)) {}

  std::shared_ptr<Target> CreateTarget(Target::Options options) override;

  bool WriteToFile(std::shared_ptr<Target> target, std::string path) override;
};

std::shared_ptr<Target> ContextGLSkity::CreateTarget(Target::Options options) {
  return TargetGLSkity::Create(gpu_context_.get(), options);
}
bool ContextGLSkity::WriteToFile(std::shared_ptr<Target> target,
                                 std::string path) {
  auto target_gl_skity = static_cast<TargetGLSkity *>(target.get());
  auto gl_texture = target_gl_skity->GetTexture();
  skity::GPUBackendTextureInfoGL backend_texture_info;
  backend_texture_info.tex_id = gl_texture;
  backend_texture_info.backend = skity::GPUBackendType::kOpenGL;
  backend_texture_info.width = target_gl_skity->GetWidth();
  backend_texture_info.height = target_gl_skity->GetHeight();
  auto skity_texture =
      gpu_context_->WrapTexture(&backend_texture_info, nullptr, nullptr);
  auto image = skity::Image::MakeHWImage(skity_texture);
  auto pixmap = image->ReadPixels(gpu_context_.get());
  uint8_t *addr = reinterpret_cast<uint8_t *>(pixmap->WritableAddr());
  UnpremultiplyAlpha(addr, pixmap->Width() * pixmap->Height());
  FlipY(addr, pixmap->Width(), pixmap->Height());

  auto codec = skity::Codec::MakePngCodec();

  auto encoded_data = codec->Encode(pixmap.get());

  char full_file_name[128];

  snprintf(full_file_name, sizeof(full_file_name), "%s.png", path.c_str());

  encoded_data->WriteToFile(full_file_name);
  return true;
}

std::shared_ptr<Context> CreateContextGLSkity(void *proc_loader) {
  auto gpu_context = skity::GLContextCreate(proc_loader);
  gpu_context->SetEnableContourAA(false);
  return std::make_shared<ContextGLSkity>(std::move(gpu_context));
}

}  // namespace bench
}  // namespace skity
