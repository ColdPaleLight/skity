// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "src/render/hw/draw/geometry/wgsl_tess_path_aa_geometry.hpp"

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
          2 * sizeof(float),
          GPUVertexStepMode::kVertex,
          {
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32,
                  0,
                  0,
              },
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32,
                  sizeof(float),
                  1,
              },
          },
      },
      // instance
      GPUVertexBufferLayout{
          8 * sizeof(float),
          GPUVertexStepMode::kInstance,
          {
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32x4,
                  0,
                  2,
              },
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32x4,
                  4 * sizeof(float),
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
};

static_assert(sizeof(Instance) == 32);

struct TessPathAAVisitor : public PathVisitor {
  explicit TessPathAAVisitor(const Matrix& matrix)
      : PathVisitor(false, matrix) {}

  void OnBeginPath() override {}

  void OnEndPath() override {}

  void OnMoveTo(Vec2 const& p) override { fan_center = p; }

  void OnLineTo(Vec2 const& p0, Vec2 const& p1) override {
    instances.push_back(Instance{Vec4{p0, p0}, Vec4{p1, p1}});
  }

  void OnQuadTo(Vec2 const& p0, Vec2 const& p1, Vec2 const& p2) override {
    auto ctr1 = (p0 + 2 * p1) / 3.f;
    auto ctr2 = (2 * p1 + p2) / 3.f;
    instances.push_back(Instance{Vec4{p0, ctr1}, Vec4{ctr2, p2}});
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
    instances.push_back(Instance{Vec4{p0, p1}, Vec4{p2, p3}});
  }

  void OnClose() override {}

  const std::vector<Instance>& GetInstances() const { return instances; }

 private:
  Vec2 fan_center = {};
  std::vector<Instance> instances;
};

}  // namespace

WGSLTessPathAAGeometry::WGSLTessPathAAGeometry(const Path& path,
                                               const Paint& paint)
    : path_(path), paint_(paint), layout_(InitVertexBufferLayout()) {}

const std::vector<GPUVertexBufferLayout>&
WGSLTessPathAAGeometry::GetBufferLayout() const {
  return layout_;
}

std::string WGSLTessPathAAGeometry::GenSourceWGSL() const {
  std::string wgsl_code = CommonVertexWGSL();

  wgsl_code += R"(

      fn get_vertex_position_with_offset(a_pos: vec2<f32>, cs: CommonSlot, offset: vec4<f32>) -> vec4<f32> {
        var pos: vec4<f32> = cs.mvp * (cs.userTransform * vec4<f32>(a_pos, 0.0, 1.0) + offset);
        return vec4<f32>(pos.x, pos.y, cs.extraInfo[0] * pos.w, pos.w);
      }
      @group(0) @binding(0) var<uniform> common_slot: CommonSlot;

      struct VSInput {
          @location(0) index: f32,
          @location(1) offset: f32,
          @location(2) p0p1: vec4<f32>,
          @location(3) p2p3: vec4<f32>,
      };

      struct VSOutput {
          @builtin(position) pos: vec4<f32>,
          @location(0)       v_pos_aa  :   f32,
      };


      fn cubic_bezier_tangent(p0: vec4<f32>, p1: vec4<f32>, p2: vec4<f32>, p3: vec4<f32>, t: f32) -> vec4<f32> {
         var u: f32 = 1.0 - t;
         var tangent: vec4<f32> = 3.0 * u * u * (p1 - p0) +
                                  6.0 * u * t * (p2 - p1) +
                                  3.0 * t * t * (p3 - p2);
         return tangent;
      }


      @vertex
      fn vs_main(input: VSInput) -> VSOutput {
          var output: VSOutput;
          var pos: vec2<f32>;

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

          var tp0: vec4<f32> = common_slot.userTransform * vec4<f32>(p0, 0.0, 1.0);
          var tp1: vec4<f32> = common_slot.userTransform * vec4<f32>(p1, 0.0, 1.0);
          var tp2: vec4<f32> = common_slot.userTransform * vec4<f32>(p2, 0.0, 1.0);
          var tp3: vec4<f32> = common_slot.userTransform * vec4<f32>(p3, 0.0, 1.0);

          var tangent: vec4<f32> = normalize(cubic_bezier_tangent(tp0, tp1, tp2, tp3, t));
          var norm: vec4<f32> = vec4<f32>(tangent.y, -tangent.x, tangent.z, tangent.w);
          var offset: vec4<f32> = norm * input.offset;
          output.pos = get_vertex_position_with_offset(pos.xy, common_slot, offset);
          if input.offset == 0.0 {
            output.v_pos_aa = 1.0;
          } else {
            output.v_pos_aa = 0.0;
          }
          return output;
      }
    )";

  return wgsl_code;
}

std::string WGSLTessPathAAGeometry::GetShaderName() const {
  std::string name = "CommonTessPathAAVertexWGSL";

  return name;
}

const char* WGSLTessPathAAGeometry::GetEntryPoint() const { return "vs_main"; }

void WGSLTessPathAAGeometry::PrepareCMD(Command* cmd, HWDrawContext* context,
                                        const Matrix& transform,
                                        float clip_depth,
                                        Command* stencil_cmd) {
  SKITY_TRACE_EVENT(WGSLTessPathGeometry_PrepareCMD);

  // check the stencil cmd to determine if this is inside a coverage step
  // but this may be changed when implement draw call mergeing in dynamic shader
  // pipeline.
  // if (stencil_cmd) {
  //   cmd->index_buffer = stencil_cmd->index_buffer;
  //   cmd->vertex_buffer = stencil_cmd->vertex_buffer;
  //   cmd->index_count = stencil_cmd->index_count;
  //   cmd->uniform_bindings = stencil_cmd->uniform_bindings.Clone();
  //   cmd->instance_count = stencil_cmd->instance_count;
  //   cmd->instance_buffer = stencil_cmd->instance_buffer;
  //   return;
  // }

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

  for (int i = 0; i < 3 * (kNumSegments + 1); i++) {
    vertex_array.push_back(i % (kNumSegments + 1));  // index

    if (i / (kNumSegments + 1) == 0) {
      vertex_array.push_back(0.0);
    } else if (i / (kNumSegments + 1) == 1) {
      vertex_array.push_back(1);  // offset
    } else {
      vertex_array.push_back(-1);  // offset
    }
  }

  assert(vertex_array.size() == 6 * (kNumSegments + 1));

  std::vector<uint32_t> index_array;
  for (int i = 0; i < kNumSegments; i++) {
    uint32_t curr = i;
    uint32_t next = i + 1;
    uint32_t outer_curr = curr + (kNumSegments + 1);
    uint32_t outer_next = next + (kNumSegments + 1);
    uint32_t inner_curr = outer_curr + (kNumSegments + 1);
    uint32_t inner_next = outer_next + (kNumSegments + 1);

    index_array.push_back(curr);
    index_array.push_back(next);
    index_array.push_back(outer_next);

    index_array.push_back(curr);
    index_array.push_back(outer_next);
    index_array.push_back(outer_curr);

    index_array.push_back(inner_curr);
    index_array.push_back(inner_next);
    index_array.push_back(next);

    index_array.push_back(inner_curr);
    index_array.push_back(next);
    index_array.push_back(curr);
  }

  assert(index_array.size() == 12 * kNumSegments);

  cmd->vertex_buffer =
      context->stageBuffer->Push(const_cast<float*>(vertex_array.data()),
                                 vertex_array.size() * sizeof(float));
  cmd->index_buffer =
      context->stageBuffer->PushIndex(const_cast<uint32_t*>(index_array.data()),
                                      index_array.size() * sizeof(uint32_t));

  cmd->index_count = index_array.size();

  TessPathAAVisitor path_visitor(transform);
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
