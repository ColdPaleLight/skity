// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "src/render/hw/hw_draw.hpp"

#include "src/render/hw/hw_stage_buffer.hpp"
#include "src/tracing.hpp"
#include "src/utils/arena_allocator.hpp"

namespace skity {

static_assert(sizeof(HWDrawData) == 4 * sizeof(float));

uint32_t HWFrameStorageBuffer::AddTransform(const Matrix& transform) {
  uint32_t id = static_cast<uint32_t>(transforms_.size());
  transforms_.push_back(transform);
  transform_buffer_ = {};
  return id;
}

GPUBufferView HWFrameStorageBuffer::GetTransformBuffer(
    HWStageBuffer* stage_buffer) {
  if (transform_buffer_.buffer != nullptr) {
    return transform_buffer_;
  }

  if (stage_buffer == nullptr || transforms_.empty()) {
    return {};
  }

  transform_buffer_ = stage_buffer->Push(
      transforms_.data(),
      static_cast<uint32_t>(transforms_.size() * sizeof(Matrix)), true);
  return transform_buffer_;
}

uint32_t HWFrameStorageBuffer::AddDrawData(uint32_t transform_id,
                                           float clip_depth) {
  uint32_t id = static_cast<uint32_t>(draw_data_.size());
  draw_data_.push_back(HWDrawData{
      transform_id,
      clip_depth,
      {},
  });
  draw_data_buffer_ = {};
  return id;
}

GPUBufferView HWFrameStorageBuffer::GetDrawDataBuffer(
    HWStageBuffer* stage_buffer) {
  if (draw_data_buffer_.buffer != nullptr) {
    return draw_data_buffer_;
  }

  if (stage_buffer == nullptr || draw_data_.empty()) {
    return {};
  }

  draw_data_buffer_ = stage_buffer->Push(
      draw_data_.data(),
      static_cast<uint32_t>(draw_data_.size() * sizeof(HWDrawData)), true);
  return draw_data_buffer_;
}

HWDrawState HWDraw::Prepare(HWDrawContext* context) {
  SKITY_TRACE_EVENT(HWDraw_Prepare);

  if (!prepared_) {
    if (clip_depth_ > 0) {
      clip_value_ = static_cast<float>(clip_depth_) /
                    static_cast<float>(context->total_clip_depth + 1);
    }

    PrepareFrameData(context);

    draw_state_ = OnPrepare(context);

    prepared_ = true;
  }

  return draw_state_;
}

void HWDraw::GenerateCommand(HWDrawContext* context, HWDrawState state) {
  SKITY_TRACE_EVENT(HWDraw_GenerateCommand);

  if (generated_) {
    return;
  }

  OnGenerateCommand(context, state);

  generated_ = true;
}

void HWDraw::PrepareFrameData(HWDrawContext* context) {
  if (context == nullptr || context->frame_storage_buffer == nullptr) {
    return;
  }

  if (transform_id_ == UINT32_MAX) {
    transform_id_ = context->frame_storage_buffer->AddTransform(transform_);
  }

  if (draw_id_ == UINT32_MAX) {
    draw_id_ =
        context->frame_storage_buffer->AddDrawData(transform_id_, clip_value_);
  }
}

}  // namespace skity
