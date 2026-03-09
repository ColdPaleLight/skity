// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef SRC_RENDER_HW_DRAW_WGX_ADVANCED_BLENDING_HPP
#define SRC_RENDER_HW_DRAW_WGX_ADVANCED_BLENDING_HPP

#include <memory>
#include <skity/effect/color_filter.hpp>
#include <string>
#include <vector>

#include "skity/graphic/blend_mode.hpp"
#include "src/gpu/gpu_sampler.hpp"
#include "src/gpu/gpu_texture.hpp"
#include "src/render/hw/hw_pipeline_key.hpp"

namespace skity {

struct Command;
struct HWDrawContext;

enum class DstReadStrategy { kNonRequired, kFramebufferFetch, kTextureCopy };

/**
 * Common code generator for all advanced blending shader.
 */
class WGXAdvancedBlending {
 public:
  explicit WGXAdvancedBlending(BlendMode blend_mode,
                               DstReadStrategy dst_read_strategy)
      : blend_mode_(blend_mode), dst_read_strategy_(dst_read_strategy) {}

  ~WGXAdvancedBlending() = default;

  std::string GenSourceWGSL() const;

  uint32_t GetAdvancedBlendingKey() const {
    return static_cast<uint32_t>(blend_mode_) |
           static_cast<uint32_t>(dst_read_strategy_) << 8;
  }

  bool SupportsFramebufferFetch() const {
    return dst_read_strategy_ == DstReadStrategy::kFramebufferFetch;
  }

  DstReadStrategy GetReadDstStrategy() const { return dst_read_strategy_; }

  void SetupBindGroup(Command* cmd, HWDrawContext* context);

  static std::unique_ptr<WGXAdvancedBlending> Make(
      BlendMode blend_mode, DstReadStrategy dst_read_strategy) {
    return std::make_unique<WGXAdvancedBlending>(blend_mode, dst_read_strategy);
  }

  // void SetDstUVMapping(Vec4 dst_uv_mapping) {
  //   dst_uv_mapping_ = dst_uv_mapping;
  // }
  // void SetTexture(std::shared_ptr<GPUTexture> texture) { texture_ = texture;
  // } void SetSampler(std::shared_ptr<GPUSampler> sampler) { sampler_ =
  // sampler; }

  // Vec4 GetDstUVMapping() const { return dst_uv_mapping_; }
  // std::shared_ptr<GPUTexture> GetTexture() const { return texture_; }
  // std::shared_ptr<GPUSampler> GetSampler() const { return sampler_; }

 private:
  BlendMode blend_mode_;
  DstReadStrategy dst_read_strategy_ = DstReadStrategy::kNonRequired;
  // Vec4 dst_uv_mapping_;
  // std::shared_ptr<GPUTexture> texture_;
  // std::shared_ptr<GPUSampler> sampler_;
};

}  // namespace skity

#endif  // SRC_RENDER_HW_DRAW_WGX_ADVANCED_BLENDING_HPP
