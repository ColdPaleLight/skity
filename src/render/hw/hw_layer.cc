// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "src/render/hw/hw_layer.hpp"

#include <cassert>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <skity/effect/shader.hpp>

#include "skity/graphic/alpha_type.hpp"
#include "skity/graphic/image.hpp"
#include "src/geometry/glm_helper.hpp"
#include "src/gpu/gpu_context_impl.hpp"
#include "src/gpu/gpu_render_pass.hpp"
#include "src/gpu/gpu_sampler.hpp"
#include "src/gpu/gpu_texture.hpp"
#include "src/gpu/texture_impl.hpp"
#include "src/render/hw/draw/hw_dynamic_path_draw.hpp"
#include "src/render/hw/hw_draw.hpp"
#include "src/render/hw/hw_draw_pass.hpp"
#include "src/tracing.hpp"

namespace skity {

HWLayer::HWLayer(Matrix matrix, int32_t depth, Rect bounds, uint32_t width,
                 uint32_t height)
    : HWDraw(matrix),
      state_(depth),
      bounds_(bounds),
      width_(width),
      height_(height),
      world_matrix_(Matrix{}),
      bounds_to_physical_matrix_(
          Matrix::Scale(width_ / bounds_.Width(), height_ / bounds_.Height()) *
          Matrix::Translate(-bounds_.Left(), -bounds_.Top())) {
  state_.SaveClipBounds(Rect::MakeWH(width_, height_), true);
}

void HWLayer::Draw(GPURenderPass* render_pass, GPUCommandBuffer* cmd) {
  SKITY_TRACE_EVENT(HWLayer_Draw);

  bool force_load = false;
  for (auto pass : draw_passes_) {
    auto self_pass = OnBeginRenderPass(cmd, force_load);
    self_pass->SetArenaAllocator(arena_allocator_);
    for (auto draw : pass->draw_ops) {
      draw->Draw(self_pass.get(), cmd);
    }
    self_pass->EncodeCommands(GetViewport());

    /**
     * FIXME: avoid crash on VIVO Y77
     *
     * Didn't know why, but it seems that if delete framebuffer before draw to
     * scrren, will avoid crash on VIVO Y77
     */
    self_pass = nullptr;

    if (pass->needs_copy_to_dst) {
      OnCopyToDstTexture(cmd, pass->dst_texture, pass->copy_to_dst_region);
    }

    force_load = true;
  }

  OnPostDraw(render_pass, cmd);

  draw_passes_.clear();
}

HWLayerState* HWLayer::GetState() { return &state_; }

void HWLayer::AddDraw(HWDraw* draw) {
  FlushPendingClip();

  draw->SetColorFormat(GetColorFormat());

  const auto& clip_bounds = state_.CurrentClipBounds();
  draw->SetScissorBox(clip_bounds);

  draw->SetClipDraw(state_.LastClipDraw());

  Rect rect = draw->GetLayerSpaceBounds();
  if (!rect.Intersect(Rect::MakeWH(width_, height_))) {
    rect.SetEmpty();
  }
  draw->SetLayerSpaceBounds(rect);

  if (draw->GetDstReadStrategy() == DstReadStrategy::kTextureCopy) {
    auto draw_pass = draw_passes_.back();
    draw_pass->needs_copy_to_dst = true;
    Rect copy_to_dst =
        Rect::MakeXYWH(std::floor(rect.Left()), std::floor(rect.Top()),
                       std::ceil(rect.Width()), std::ceil(rect.Height()));
    draw_pass->copy_to_dst_region = GPURegion{
        .x = static_cast<uint32_t>(copy_to_dst.Left()),
        .y = static_cast<uint32_t>(copy_to_dst.Top()),
        .width = static_cast<uint32_t>(copy_to_dst.Width()),
        .height = static_cast<uint32_t>(copy_to_dst.Height()),
    };
    if (rt_origin_ == LayerRTOrigin::kTopLeft) {
      auto mapping =
          Matrix::Scale(1.f / copy_to_dst.Width(), 1.f / copy_to_dst.Height()) *
          Matrix::Translate(-copy_to_dst.Left(), -copy_to_dst.Top());
      draw_pass->dst_uv_mapping =
          Vec4{mapping.GetScaleX(), mapping.GetScaleY(),
               mapping.GetTranslateX(), mapping.GetTranslateY()};
    } else {
      auto mapping =
          Matrix::Scale(1.f / copy_to_dst.Width(), 1.f / copy_to_dst.Height()) *
          Matrix::Translate(-copy_to_dst.Left(), -(height_ - copy_to_dst.Top() -
                                                   copy_to_dst.Height()));
      draw_pass->dst_uv_mapping =
          Vec4{mapping.GetScaleX(), mapping.GetScaleY(),
               mapping.GetTranslateX(), mapping.GetTranslateY()};
    }
    auto new_draw_pass = arena_allocator_->Make<HWDrawPass>();
    draw_passes_.push_back(new_draw_pass);
    if (GetSampleCount() > 1) {
      // 如果draw_pass不支持load，需要在里面插入一个Draw来代表LoadDraw
      // TODO 检查GL是不是需要这个逻辑
      // TODO 思考现在这个API是否合理
      HWDraw* load_draw = CreateEmulatedLoadDraw(new_draw_pass);
      load_draw->SetClipDepth(state_.GetNextDrawDepth());
      load_draw->SetScissorBox(clip_bounds);
      load_draw->SetLayerSpaceBounds(Rect::MakeSize(Vec2{width_, height_}));
      new_draw_pass->draw_ops.emplace_back(load_draw);
    }

  } else {
    if (enable_merging_draw_call_) {
      bool merged = TryMerge(draw);
      if (merged) {
        return;
      }
    }
  }

  draw->SetClipDepth(state_.GetNextDrawDepth());
  draw_passes_.back()->draw_ops.emplace_back(draw);
}

bool HWLayer::TryMerge(HWDraw* draw) {
  auto& draw_ops = draw_passes_.back()->draw_ops;
  size_t max_count = std::min(draw_ops.size(), size_t(5));

  for (auto it = draw_ops.rbegin(); it != draw_ops.rbegin() + max_count; it++) {
    auto cadidate = *it;
    bool merged = cadidate->MergeIfPossible(draw);
    if (merged) {
      return true;
    }

    if (Rect::Intersect(cadidate->GetLayerSpaceBounds(),
                        draw->GetLayerSpaceBounds())) {
      break;
    }
  }

  return false;
}

void HWLayer::AddClip(HWDraw* draw) {
  const auto& clip_bounds = state_.CurrentClipBounds();

  draw->SetScissorBox(clip_bounds);
  draw->SetColorFormat(GetColorFormat());
  pending_clip_.emplace_back(draw);
  state_.SaveClipOp(draw);
}

void HWLayer::AddRectClip(const skity::Rect& local_rect, const Matrix& matrix) {
  Rect transformed_rect;
  GetLayerPhysicalMatrix(matrix).MapRect(&transformed_rect, local_rect);
  state_.SaveClipBounds(transformed_rect);
}

void HWLayer::Restore() { state_.Restore(); }

void HWLayer::RestoreToCount(int32_t count) { state_.RestoreToCount(count); }

void HWLayer::FlushPendingClip() {
  draw_passes_.back()->draw_ops.insert(draw_passes_.back()->draw_ops.end(),
                                       pending_clip_.begin(),
                                       pending_clip_.end());

  pending_clip_.clear();
}

HWDrawState HWLayer::OnPrepare(HWDrawContext* context) {
  state_.FlushClipDepth();

  gpu_device_ = context->gpuContext->GetGPUDevice();

  HWRenderTargetCache::Pool pool(context->gpuContext->GetRenderTargetCache());

  HWDrawContext sub_context;
  sub_context.ctx_scale = context->ctx_scale;
  sub_context.stageBuffer = context->stageBuffer;
  sub_context.static_buffer = context->static_buffer;
  sub_context.pipelineLib = context->pipelineLib;
  sub_context.gpuContext = context->gpuContext;
  sub_context.mvp = FromGLM(glm::ortho(bounds_.Left(), bounds_.Right(),
                                       bounds_.Bottom(), bounds_.Top()));
  sub_context.pool = &pool;
  sub_context.vertex_vector_cache = context->vertex_vector_cache;
  sub_context.index_vector_cache = context->index_vector_cache;
  sub_context.total_clip_depth = state_.GetDrawDepth() + 1;
  sub_context.arena_allocator = context->arena_allocator;
  sub_context.scale = scale_;
  for (auto pass : draw_passes_) {
    if (pass->resolve_image_for_load) {
      pass->resolve_image_for_load->SetTexture(
          std::make_shared<InternalTexture>(GetResolveColorTexture(),
                                            AlphaType::kPremul_AlphaType));
    }

    for (auto draw : pass->draw_ops) {
      layer_state_ |= draw->Prepare(&sub_context);
    }

    if (pass->needs_copy_to_dst) {
      GPUTextureDescriptor desc;
      desc.width = pass->copy_to_dst_region.width;
      desc.height = pass->copy_to_dst_region.height;
      desc.format = GetColorFormat();
      desc.sample_count = 1;
      desc.usage =
          static_cast<GPUTextureUsageMask>(GPUTextureUsage::kCopyDst) |
          static_cast<GPUTextureUsageMask>(GPUTextureUsage::kTextureBinding);
      desc.storage_mode = GPUTextureStorageMode::kPrivate;
      pass->dst_texture = gpu_device_->CreateTexture(desc);
      GPUSamplerDescriptor sampler_desc;
      pass->dst_sampler = gpu_device_->CreateSampler(sampler_desc);
    }
  }

  // abstract layer no need stencil test and depth for itself
  return HWDrawState::kDrawStateNone;
}

void HWLayer::OnGenerateCommand(HWDrawContext* context, HWDrawState state) {
  HWRenderTargetCache::Pool pool(context->gpuContext->GetRenderTargetCache());

  HWDrawContext sub_context;
  sub_context.ctx_scale = context->ctx_scale;
  sub_context.stageBuffer = context->stageBuffer;
  sub_context.static_buffer = context->static_buffer;
  sub_context.pipelineLib = context->pipelineLib;
  sub_context.gpuContext = context->gpuContext;
  sub_context.mvp = FromGLM(glm::ortho(bounds_.Left(), bounds_.Right(),
                                       bounds_.Bottom(), bounds_.Top()));
  sub_context.pool = &pool;
  sub_context.vertex_vector_cache = context->vertex_vector_cache;
  sub_context.index_vector_cache = context->index_vector_cache;
  sub_context.total_clip_depth = state_.GetDrawDepth() + 1;
  sub_context.arena_allocator = context->arena_allocator;
  sub_context.scale = scale_;

  for (auto pass : draw_passes_) {
    for (auto draw : pass->draw_ops) {
      draw->GenerateCommand(&sub_context, layer_state_);
    }
    sub_context.prev_draw_pass = pass;
  }
}

Matrix HWLayer::GetLayerPhysicalMatrix(const Matrix& matrix) const {
  return bounds_to_physical_matrix_ * matrix;
}

Rect HWLayer::CalculateLayerSpaceBounds(const Rect& local_rect,
                                        const Matrix& matrix) const {
  Rect layer_space_bounds;
  GetLayerPhysicalMatrix(matrix).MapRect(&layer_space_bounds, local_rect);
  return layer_space_bounds;
}

std::shared_ptr<Shader> HWLayer::CreateDrawLayerShader(
    GPUContext* gpu_context, std::shared_ptr<GPUTexture> gpu_texture,
    const Rect& bounds) const {
  auto texture = std::make_shared<InternalTexture>(
      gpu_texture, AlphaType::kPremul_AlphaType);
  auto image = Image::MakeHWImage(texture);
  return CreateDrawLayerShader(image, bounds);
}

std::shared_ptr<Shader> HWLayer::CreateDrawLayerShader(
    std::shared_ptr<Image> image, const Rect& bounds) const {
  Matrix local_matrix;
  if (rt_origin_ == LayerRTOrigin::kBottomLeft) {
    local_matrix =
        Matrix::Translate(bounds.Left(), bounds.Height() + bounds.Top()) *
        Matrix::Scale(bounds.Width() / image->Width(),
                      -(bounds.Height() / image->Height()));
  } else {
    local_matrix = Matrix::Translate(bounds.Left(), bounds.Top()) *
                   Matrix::Scale(bounds.Width() / image->Width(),
                                 bounds.Height() / image->Height());
  }

  return Shader::MakeShader(image, SamplingOptions{}, TileMode::kClamp,
                            TileMode::kClamp, local_matrix);
}

HWDraw* HWLayer::CreateEmulatedLoadDraw(HWDrawPass* draw_pass) {
  // prepare layer back draw
  HWDraw* result;

  const auto& bounds = GetBounds();
  Path path;
  path.AddRect(bounds);

  Paint paint;
  paint.SetStyle(Paint::kFill_Style);
  std::shared_ptr<DeferredTextureImage> image =
      DeferredTextureImage::MakeDeferredTextureImage(
          FromGPUTextureFormat(GetColorFormat()), width_, height_,
          AlphaType::kPremul_AlphaType);
  paint.SetShader(CreateDrawLayerShader(image, bounds));
  paint.SetBlendMode(BlendMode::kSrc);
  result = arena_allocator_->Make<HWDynamicPathDraw>(
      GetTransform(), std::move(path), std::move(paint), false, false);

  // If layer_back_draw_ is null means user want open WGSL pipeline
  // but the library does not open dynamic shader during compile time
  if (result) {
    // TODO 检查一下
    result->SetSampleCount(GetSampleCount());
    result->SetColorFormat(GetColorFormat());
    result->SetScissorBox(GetScissorBox());
  }
  draw_pass->resolve_image_for_load = image;
  return result;
}

}  // namespace skity
