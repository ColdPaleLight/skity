// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "src/render/hw/draw/geometry/wgsl_tess_path_geometry.hpp"

#include <cassert>
#include <vector>

#include "skity/geometry/vector.hpp"
#include "src/geometry/conic.hpp"
#include "src/geometry/wangs_formula.hpp"
#include "src/graphic/path_visitor.hpp"
#include "src/render/hw/draw/wgx_utils.hpp"
#include "src/render/hw/hw_draw.hpp"
#include "src/render/hw/hw_path_aa_outline.hpp"
#include "src/render/hw/hw_path_raster.hpp"
#include "src/render/hw/hw_stage_buffer.hpp"
#include "src/tracing.hpp"

namespace skity {

namespace {

std::vector<GPUVertexBufferLayout> InitVertexBufferLayout() {
  std::vector<GPUVertexBufferLayout> layout = {
      // vertex
      GPUVertexBufferLayout{
          sizeof(float),
          GPUVertexStepMode::kVertex,
          {
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32,
                  0,
                  0,
              },
          },
      },
      // instance
      GPUVertexBufferLayout{
          10 * sizeof(float),
          GPUVertexStepMode::kInstance,
          {
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32x4,
                  0,
                  1,
              },
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32x4,
                  4 * sizeof(float),
                  2,
              },
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32x2,
                  8 * sizeof(float),
                  3,
              },
          },
      },
  };

  return layout;
}

struct Instance {
  Vec4 p0p1;
  Vec4 p2p3;
  Vec2 fan_center;
};

static_assert(sizeof(Instance) == 40);

struct TessPathVisitor : public PathVisitor {
  explicit TessPathVisitor(const Matrix& matrix) : PathVisitor(false, matrix) {}

  void OnBeginPath() override {}

  void OnEndPath() override {}

  void OnMoveTo(Vec2 const& p) override { fan_center = p; }

  void OnLineTo(Vec2 const& p0, Vec2 const& p1) override {
    instances.push_back(Instance{Vec4{p0, p0}, Vec4{p1, p1}, fan_center});
  }

  void OnQuadTo(Vec2 const& p0, Vec2 const& p1, Vec2 const& p2) override {
    auto ctr1 = (p0 + 2 * p1) / 3.f;
    auto ctr2 = (2 * p1 + p2) / 3.f;
    instances.push_back(Instance{Vec4{p0, ctr1}, Vec4{ctr2, p2}, fan_center});
  }

  void OnConicTo(Vec2 const& p1, Vec2 const& p2, Vec2 const& p3,
                 float weight) override {
    Point start = {p1.x, p1.y, 0.f, 1.f};
    Point control = {p2.x, p2.y, 0.f, 1.f};
    Point end = {p3.x, p3.y, 0.f, 1.f};

    std::array<Point, 5> quads{};
    Conic conic{start, control, end, weight};
    conic.ChopIntoQuadsPOW2(quads.data(), 1);
    quads[0] = start;

    OnQuadTo(Vec2{quads[0]}, Vec2{quads[1]}, Vec2{quads[2]});
    OnQuadTo(Vec2{quads[2]}, Vec2{quads[3]}, Vec2{quads[4]});
  }

  void OnCubicTo(Vec2 const& p0, Vec2 const& p1, Vec2 const& p2,
                 Vec2 const& p3) override {
    instances.push_back(Instance{Vec4{p0, p1}, Vec4{p2, p3}, fan_center});
  }

  void OnClose() override {}

  const std::vector<Instance>& GetInstances() const { return instances; }

 private:
  Vec2 fan_center = {};
  std::vector<Instance> instances;
};

}  // namespace

WGSLTessPathGeometry::WGSLTessPathGeometry(const Path& path, const Paint& paint)
    : path_(path), paint_(paint), layout_(InitVertexBufferLayout()) {}

const std::vector<GPUVertexBufferLayout>&
WGSLTessPathGeometry::GetBufferLayout() const {
  return layout_;
}

std::string WGSLTessPathGeometry::GenSourceWGSL() const {
  std::string wgsl_code = CommonVertexWGSL();

  wgsl_code += R"(
      @group(0) @binding(0) var<uniform> common_slot: CommonSlot;

      struct VSInput {
          @location(0) index: f32,
          @location(1) p0p1: vec4<f32>,
          @location(2) p2p3: vec4<f32>,
          @location(3) fan_center: vec2<f32>,
      };

      struct VSOutput {
          @builtin(position) pos: vec4<f32>,
      };


      @vertex
      fn vs_main(input: VSInput) -> VSOutput {
          var output: VSOutput;
          var pos: vec2<f32>;
          if input.index < 0.0 {
            pos = input.fan_center;
          } else {
            var t: f32 = input.index / 32.0;
            var p0: vec2<f32> = input.p0p1.xy;
            var p1: vec2<f32> = input.p0p1.zw;
            var p2: vec2<f32> = input.p2p3.xy;
            var p3: vec2<f32> = input.p2p3.zw;

            var p01: vec2<f32> = mix(p0, p1, t);
            var p12: vec2<f32> = mix(p1, p2, t);
            var p23: vec2<f32> = mix(p2, p3, t);

            var p012: vec2<f32> = mix(p01, p12, t);
            var p123: vec2<f32> = mix(p12, p23, t);
            pos = mix(p012, p123, t);
          }

          output.pos = get_vertex_position(pos.xy, common_slot);
          return output;
      }
    )";

  return wgsl_code;
}

std::string WGSLTessPathGeometry::GetShaderName() const {
  std::string name = "CommonTessPathVertexWGSL";

  return name;
}

const char* WGSLTessPathGeometry::GetEntryPoint() const { return "vs_main"; }

namespace {}

void WGSLTessPathGeometry::PrepareCMD(Command* cmd, HWDrawContext* context,
                                      const Matrix& transform, float clip_depth,
                                      Command* stencil_cmd) {
  SKITY_TRACE_EVENT(WGSLTessPathGeometry_PrepareCMD);

  // check the stencil cmd to determine if this is inside a coverage step
  // but this may be changed when implement draw call mergeing in dynamic shader
  // pipeline.
  if (stencil_cmd) {
    cmd->index_buffer = stencil_cmd->index_buffer;
    cmd->vertex_buffer = stencil_cmd->vertex_buffer;
    cmd->index_count = stencil_cmd->index_count;
    cmd->uniform_bindings = stencil_cmd->uniform_bindings.Clone();
    cmd->instance_count = stencil_cmd->instance_count;
    cmd->instance_buffer = stencil_cmd->instance_buffer;
    return;
  }

  if (cmd->pipeline == nullptr) {
    return;
  }

  // TODO，添加Instance，vertex， index

  // auto upload_data = [&](const std::vector<float>& vertex,
  //                        const std::vector<uint32_t>& index) {
  //   if (vertex.empty() || index.empty()) {
  //     return;
  //   }

  //   cmd->vertex_buffer = context->stageBuffer->Push(
  //       const_cast<float*>(vertex.data()), vertex.size() * sizeof(float));

  //   cmd->index_buffer = context->stageBuffer->PushIndex(
  //       const_cast<uint32_t*>(index.data()), index.size() *
  //       sizeof(uint32_t));

  //   cmd->index_count = index.size();
  // };

  const Vec2& scale = context->scale;

  // HWPathFillRaster raster{paint_, Matrix::Scale(scale.x, scale.y) *
  // transform,
  //                         context->vertex_vector_cache,
  //                         context->index_vector_cache};

  // raster.FillPath(path_);
  // upload_data(raster.GetRawVertexBuffer(), raster.GetRawIndexBuffer());

  auto pipeline = cmd->pipeline;

  const static int32_t kNumSegments = 32;
  std::vector<float> vertex_array;
  
  for (int i = -1; i <= kNumSegments; i++) {
    vertex_array.push_back(i);
  }

  assert(vertex_array.size() == kNumSegments + 2);

  std::vector<uint32_t> index_array;
  for (int i = 1; i <= kNumSegments; i++) {
    index_array.push_back(0);
    index_array.push_back(i);
    index_array.push_back(i + 1);
  }
  assert(index_array.size() == kNumSegments * 3);

  cmd->vertex_buffer =
      context->stageBuffer->Push(const_cast<float*>(vertex_array.data()),
                                 vertex_array.size() * sizeof(float));
  cmd->index_buffer =
      context->stageBuffer->PushIndex(const_cast<uint32_t*>(index_array.data()),
                                      index_array.size() * sizeof(uint32_t));

  cmd->index_count = index_array.size();

  TessPathVisitor path_visitor(transform);
  path_visitor.VisitPath(path_, true);

  std::vector<Instance> instances = path_visitor.GetInstances();

  cmd->instance_count = instances.size();
  cmd->instance_buffer = context->stageBuffer->Push(
      instances.data(), instances.size() * sizeof(Instance));

  auto group = pipeline->GetBindingGroup(0);

  if (group == nullptr) {
    return;
  }

  // bind CommonSlot
  auto common_slot = group->GetEntry(0);

  if (!SetupCommonInfo(common_slot, context->mvp, transform, clip_depth)) {
    return;
  }

  UploadBindGroup(common_slot, cmd, context);
}

}  // namespace skity
