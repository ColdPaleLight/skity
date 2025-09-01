// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "src/render/hw/draw/hw_dynamic_oval_draw.hpp"

#include <vector>

#include "src/effect/pixmap_shader.hpp"
#include "src/gpu/gpu_context_impl.hpp"
#include "src/render/hw/draw/fragment/wgsl_gradient_fragment.hpp"
#include "src/render/hw/draw/fragment/wgsl_oval_fragment.hpp"
#include "src/render/hw/draw/fragment/wgsl_solid_color.hpp"
#include "src/render/hw/draw/fragment/wgsl_stencil_fragment.hpp"
#include "src/render/hw/draw/fragment/wgsl_texture_fragment.hpp"
#include "src/render/hw/draw/geometry/wgsl_gradient_path.hpp"
#include "src/render/hw/draw/geometry/wgsl_oval_geometry.hpp"
#include "src/render/hw/draw/geometry/wgsl_path_geometry.hpp"
#include "src/render/hw/draw/geometry/wgsl_texture_path.hpp"
#include "src/render/hw/draw/step/color_step.hpp"
#include "src/render/hw/draw/step/stencil_step.hpp"
#include "src/render/hw/draw/wgx_filter.hpp"

namespace skity {

HWDynamicOvalDraw::HWDynamicOvalDraw(Matrix transform, float radius,
                                     Vec2 center, Paint paint)
    : HWDynamicDraw(transform, paint.GetBlendMode()),
      radius_(radius),
      center_(center),
      paint_(std::move(paint)) {}

void HWDynamicOvalDraw::OnGenerateDrawStep(ArrayList<HWDrawStep *, 2> &steps,
                                           HWDrawContext *context) {
  std::vector<WGSLOvalGeometry::Oval> ovals;

  ovals.push_back(WGSLOvalGeometry::Oval{center_, radius_});

  auto geom = context->arena_allocator->Make<WGSLOvalGeometry>(ovals, paint_);

  auto frag =
      context->arena_allocator->Make<WGSLOvalFragment>(paint_.GetColor4f());
  steps.emplace_back(context->arena_allocator->Make<ColorStep>(
      std::move(geom), std::move(frag), CoverageType::kNone));
}

// HWWGSLGeometry *HWDynamicOvalDraw::GenGeometry(HWDrawContext *context,
//                                                bool aa) const {
//   auto arena_allocator = context->arena_allocator;
//   if (paint_.GetShader()) {
//     // gradient or image
//     auto type = paint_.GetShader()->AsGradient(nullptr);

//     if (type == Shader::GradientType::kNone) {
//       auto pixmap_shader =
//           std::static_pointer_cast<PixmapShader>(paint_.GetShader());

//       const std::shared_ptr<Image> &image = *(pixmap_shader->AsImage());

//       Matrix inv_local_matrix{};

//       pixmap_shader->GetLocalMatrix().Invert(&inv_local_matrix);

//       return arena_allocator->Make<WGSLTexturePath>(
//           path_, paint_, is_stroke_, aa, inv_local_matrix,
//           static_cast<float>(image->Width()),
//           static_cast<float>(image->Height()));
//     } else {
//       return arena_allocator->Make<WGSLGradientPath>(
//           path_, paint_, is_stroke_, aa,
//           paint_.GetShader()->GetLocalMatrix());
//     }

//   } else {
//     return arena_allocator->Make<WGSLPathGeometry>(path_, paint_, is_stroke_,
//                                                    aa);
//   }
// }

// HWWGSLFragment *HWDynamicOvalDraw::GenFragment(HWDrawContext *context) const
// {
//   auto arena_allocator = context->arena_allocator;
//   if (paint_.GetShader()) {
//     auto type = paint_.GetShader()->AsGradient(nullptr);

//     if (type == Shader::GradientType::kNone) {
//       // handle image rendering in the future
//       auto pixmap_shader =
//           std::static_pointer_cast<PixmapShader>(paint_.GetShader());

//       const std::shared_ptr<Image> &image = *(pixmap_shader->AsImage());

//       std::shared_ptr<GPUTexture> texture;
//       if (image->GetTexture()) {
//         const auto &texture_image = *(image->GetTexture());
//         texture = texture_image->GetGPUTexture();
//       } else if (image->GetPixmap()) {
//         const auto &pixmap_image = *(image->GetPixmap());
//         auto texture_handler =
//             context->gpuContext->GetTextureManager()->FindOrCreateTexture(
//                 Texture::FormatFromColorType(pixmap_image->GetColorType()),
//                 pixmap_image->Width(), pixmap_image->Height(),
//                 pixmap_image->GetAlphaType(), pixmap_image);
//         texture_handler->UploadImage(pixmap_image);
//         texture = texture_handler->GetGPUTexture();
//       } else {
//         auto texture_handler =
//         image->GetTextureByContext(context->gpuContext);

//         if (texture_handler) {
//           texture = texture_handler->GetGPUTexture();
//         }
//       }

//       if (texture != nullptr) {
//         GPUSamplerDescriptor descriptor;
//         descriptor.mag_filter =
//             ToGPUFilterMode(pixmap_shader->GetSamplingOptions()->filter);
//         descriptor.min_filter =
//             ToGPUFilterMode(pixmap_shader->GetSamplingOptions()->filter);
//         descriptor.mipmap_filter =
//             ToGPUMipmapMode(pixmap_shader->GetSamplingOptions()->mipmap);
//         auto sampler =
//             context->gpuContext->GetGPUDevice()->CreateSampler(descriptor);

//         return arena_allocator->Make<WGSLTextureFragment>(
//             pixmap_shader, texture, sampler, paint_.GetAlphaF());
//       } else {
//         return arena_allocator->Make<WGSLSolidColor>(Colors::kRed);
//       }

//     } else {
//       Shader::GradientInfo info{};

//       paint_.GetShader()->AsGradient(&info);

//       return arena_allocator->Make<WGSLGradientFragment>(info, type,
//                                                          paint_.GetAlphaF());
//     }

//   } else {
//     if (is_stroke_) {
//       return arena_allocator->Make<WGSLSolidColor>(paint_.GetStrokeColor());
//     } else {
//       return arena_allocator->Make<WGSLSolidColor>(paint_.GetFillColor());
//     }
//   }
// }

}  // namespace skity
