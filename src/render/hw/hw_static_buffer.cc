// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "src/render/hw/hw_static_buffer.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>

#include "src/gpu/gpu_buffer.hpp"
#include "src/gpu/gpu_device.hpp"
#include "src/render/hw/draw/geometry/wgsl_tess_path_aa_geometry.hpp"
#include "src/render/hw/draw/geometry/wgsl_tess_path_geometry.hpp"
#include "src/render/hw/draw/geometry/wgsl_tess_path_stroke_geometry.hpp"

namespace skity {

HWStaticBuffer::HWStaticBuffer(GPUDevice* device)
    : stage_buffer_(std::make_unique<HWStageBuffer>(device)) {}

HWStaticBuffer::~HWStaticBuffer() = default;

void HWStaticBuffer::Flush() {
  if (needs_flush_) {
    stage_buffer_->Flush();
  }
  needs_flush_ = false;
}

GPUBufferView HWStaticBuffer::GetTessPathVertexBufferView() {
  if (!initialized_) {
    Initialize();
  }
  return tess_path_vertex_buffer_view_.value();
}

GPUBufferView HWStaticBuffer::GetTessPathIndexBufferView() {
  if (!initialized_) {
    Initialize();
  }
  return tess_path_index_buffer_view_.value();
}

GPUBufferView HWStaticBuffer::GetTessPathStrokeVertexBufferView() {
  if (!initialized_) {
    Initialize();
  }
  return tess_path_stroke_vertex_buffer_view_.value();
}

GPUBufferView HWStaticBuffer::GetTessPathStrokeIndexBufferView() {
  if (!initialized_) {
    Initialize();
  }
  return tess_path_stroke_index_buffer_view_.value();
}

GPUBufferView HWStaticBuffer::GetTessPathAAVertexBufferView() {
  // TODO
  return GPUBufferView{};
}

GPUBufferView HWStaticBuffer::GetTessPathAAIndexBufferView() {
  // TODO
  return GPUBufferView{};
}

void HWStaticBuffer::Initialize() {
  tess_path_vertex_buffer_view_ =
      WGSLTessPathGeometry::CreateVertexBufferView(stage_buffer_.get());
  tess_path_index_buffer_view_ =
      WGSLTessPathGeometry::CreateIndexBufferView(stage_buffer_.get());
  tess_path_stroke_vertex_buffer_view_ =
      WGSLTessPathStrokeGeometry::CreateVertexBufferView(stage_buffer_.get());
  tess_path_stroke_index_buffer_view_ =
      WGSLTessPathStrokeGeometry::CreateIndexBufferView(stage_buffer_.get());
  initialized_ = true;
  needs_flush_ = true;
}

}  // namespace skity
