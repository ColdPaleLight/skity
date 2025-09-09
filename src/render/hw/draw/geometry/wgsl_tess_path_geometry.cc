// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "src/render/hw/draw/geometry/wgsl_tess_path_geometry.hpp"

#include <_types/_uint32_t.h>

#include <algorithm>
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

constexpr static float kPrecision = 4.0f;
constexpr static int32_t kMaxNumSegmentsPerInstance = 8;

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
          12 * sizeof(float),
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
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32,
                  10 * sizeof(float),
                  4,
              },
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32,
                  11 * sizeof(float),
                  5,
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
  float index_offset;
  float num_segments;
};

static_assert(sizeof(Instance) == 48);

struct TessPathVisitor : public PathVisitor {
  explicit TessPathVisitor(const Matrix& matrix)
      : PathVisitor(false, matrix),
        xform_(wangs_formula::VectorXform(matrix)) {}

  void OnBeginPath() override {}

  void OnEndPath() override {}

  void OnMoveTo(Vec2 const& p) override { fan_center = p; }

  void OnLineTo(Vec2 const& p0, Vec2 const& p1) override {
    instances.push_back(
        Instance{Vec4{p0, p0}, Vec4{p1, p1}, fan_center, 0.f, 1.f});
  }

  void OnQuadTo(Vec2 const& p0, Vec2 const& p1, Vec2 const& p2) override {
    auto ctr1 = (p0 + 2 * p1) / 3.f;
    auto ctr2 = (2 * p1 + p2) / 3.f;

    arc_[0] = p0;
    arc_[1] = p1;
    arc_[2] = p2;
    uint32_t num =
        std::ceil(wangs_formula::Quadratic(kPrecision, arc_, xform_));
    num = std::max(num, 1u);
    uint32_t remain = num % kMaxNumSegmentsPerInstance;
    uint32_t count = num / kMaxNumSegmentsPerInstance;

    for (uint32_t i = 0; i < count; i++) {
      instances.push_back(
          Instance{Vec4{p0, ctr1}, Vec4{ctr2, p2}, fan_center,
                   static_cast<float>(i * kMaxNumSegmentsPerInstance),
                   static_cast<float>(num)});
    }

    if (remain > 0) {
      instances.push_back(
          Instance{Vec4{p0, ctr1}, Vec4{ctr2, p2}, fan_center,
                   static_cast<float>(count * kMaxNumSegmentsPerInstance),
                   static_cast<float>(num)});
    }
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
    arc_[0] = p0;
    arc_[1] = p1;
    arc_[2] = p2;
    arc_[3] = p3;
    uint32_t num = std::ceil(wangs_formula::Cubic(kPrecision, arc_, xform_));
    num = std::max(num, 1u);

    uint32_t remain = num % kMaxNumSegmentsPerInstance;
    uint32_t count = num / kMaxNumSegmentsPerInstance;

    for (uint32_t i = 0; i < count; i++) {
      instances.push_back(
          Instance{Vec4{p0, p1}, Vec4{p2, p3}, fan_center,
                   static_cast<float>(i * kMaxNumSegmentsPerInstance),
                   static_cast<float>(num)});
    }

    if (remain > 0) {
      instances.push_back(
          Instance{Vec4{p0, p1}, Vec4{p2, p3}, fan_center,
                   static_cast<float>(count * kMaxNumSegmentsPerInstance),
                   static_cast<float>(num)});
    }
  }

  void OnClose() override {}

  const std::vector<Instance>& GetInstances() const { return instances; }

 private:
  Vec2 fan_center = {};
  std::vector<Instance> instances;
  wangs_formula::VectorXform xform_;
  Vec2 arc_[4];
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
      // @ExtraUniform

      struct VSInput {
          @location(0) index: f32,
          @location(1) p0p1: vec4<f32>,
          @location(2) p2p3: vec4<f32>,
          @location(3) fan_center: vec2<f32>,
          @location(4) index_offset: f32,
          @location(5) num_segments: f32,
      };

      struct VSOutput {
          @builtin(position) pos: vec4<f32>,
          // @ExtraVSOutput
      };


      @vertex
      fn vs_main(input: VSInput) -> VSOutput {
          var output: VSOutput;
          var pos: vec2<f32>;
          var index: f32 = input.index + input.index_offset;
          if input.index < 0.0 || index > input.num_segments {
            pos = input.fan_center;
          } else {
            var t: f32 = index / input.num_segments;
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
          // @ExtraBeforeReturn
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
  // but this may be changed when implement draw call mergeing in dynamic
  // shader pipeline.
  //  if (stencil_cmd) {
  //    cmd->index_buffer = stencil_cmd->index_buffer;
  //    cmd->vertex_buffer = stencil_cmd->vertex_buffer;
  //    cmd->index_count = stencil_cmd->index_count;
  //    cmd->uniform_bindings = stencil_cmd->uniform_bindings.Clone();
  //    cmd->instance_count = stencil_cmd->instance_count;
  //    cmd->instance_buffer = stencil_cmd->instance_buffer;
  //    return;
  //  }

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

  std::vector<float> vertex_array;

  for (int i = -1; i <= kMaxNumSegmentsPerInstance; i++) {
    vertex_array.push_back(i);
  }

  assert(vertex_array.size() == kMaxNumSegmentsPerInstance + 2);

  std::vector<uint32_t> index_array;
  for (int i = 1; i <= kMaxNumSegmentsPerInstance; i++) {
    index_array.push_back(0);
    index_array.push_back(i);
    index_array.push_back(i + 1);
  }
  assert(index_array.size() == kMaxNumSegmentsPerInstance * 3);

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

WGSLGradientTessPath::WGSLGradientTessPath(const Path& path, const Paint& paint,
                                           const Matrix& local_matrix)
    : WGSLTessPathGeometry(path, paint), local_matrix_(local_matrix) {}

std::string WGSLGradientTessPath::GenSourceWGSL() const {
  auto wgsl = WGSLTessPathGeometry::GenSourceWGSL();
  std::unordered_map<std::string, std::string> replacements = {
      {
          "// @ExtraUniform",
          "@group(0) @binding(1) var<uniform> inv_matrix   : mat4x4<f32>;",
      },
      {
          "// @ExtraVSOutput",
          "@location(0)        v_pos   :   vec2<f32>,",
      },
      {
          "// @ExtraBeforeReturn",
          "output.v_pos = (inv_matrix * vec4<f32>(pos.xy, 0.0, 1.0)).xy;",
      },
  };

  ReplacePlaceholder(wgsl, replacements);
  return wgsl;
}

std::string WGSLGradientTessPath::GetShaderName() const {
  std::string name = "CommonGradientTessPathVertexWGSL";

  return name;
}

const char* WGSLGradientTessPath::GetEntryPoint() const { return "vs_main"; }

void WGSLGradientTessPath::PrepareCMD(Command* cmd, HWDrawContext* context,
                                      const Matrix& transform, float clip_depth,
                                      Command* stencil_cmd) {
  SKITY_TRACE_EVENT(WGSLGradientPath_PrepareCMD);

  if (stencil_cmd) {
    cmd->index_buffer = stencil_cmd->index_buffer;
    cmd->vertex_buffer = stencil_cmd->vertex_buffer;
    cmd->index_count = stencil_cmd->index_count;
    cmd->uniform_bindings = stencil_cmd->uniform_bindings.Clone();
    cmd->instance_count = stencil_cmd->instance_count;
    cmd->instance_buffer = stencil_cmd->instance_buffer;
    return;
  }

  WGSLTessPathGeometry::PrepareCMD(cmd, context, transform, clip_depth,
                                   stencil_cmd);

  if (cmd->pipeline == nullptr) {
    return;
  }

  auto group = cmd->pipeline->GetBindingGroup(0);

  if (group == nullptr) {
    return;
  }

  auto inv_matrix_entry = group->GetEntry(1);

  if (inv_matrix_entry == nullptr ||
      inv_matrix_entry->type_definition->name != "mat4x4<f32>") {
    return;
  }

  Matrix inv_matrix{};

  local_matrix_.Invert(&inv_matrix);

  inv_matrix_entry->type_definition->SetData(&inv_matrix, sizeof(Matrix));

  UploadBindGroup(inv_matrix_entry, cmd, context);
}

WGSLTextureTessPath::WGSLTextureTessPath(const Path& path, const Paint& paint,

                                         const Matrix& local_matrix,
                                         float width, float height)
    : WGSLTessPathGeometry(path, paint),
      local_matrix_(local_matrix),
      width_(width),
      height_(height) {}

std::string WGSLTextureTessPath::GenSourceWGSL() const {
  std::string wgsl_code = CommonVertexWGSL();
  wgsl_code += R"(
    struct ImageBoundsInfo {
      bounds      : vec2<f32>,
      inv_matrix  : mat4x4<f32>,
    };

  )";
  wgsl_code += WGSLTessPathGeometry::GenSourceWGSL();
  std::unordered_map<std::string, std::string> replacements = {
      {
          "// @ExtraUniform",
          "@group(0) @binding(1) var<uniform> image_bounds : ImageBoundsInfo;",
      },
      {
          "// @ExtraVSOutput",
          "@location(0)        frag_coord  : vec2<f32>,",
      },
      {"// @ExtraBeforeReturn",
       R"(
          var mapped_pos  : vec2<f32>     = (image_bounds.inv_matrix * vec4<f32>(pos.xy, 0.0, 1.0)).xy;
          var mapped_lt   : vec2<f32>     = vec2<f32>(0.0, 0.0);
          var mapped_rb   : vec2<f32>     = image_bounds.bounds;
          var total_x     : f32           = mapped_rb.x - mapped_lt.x;
          var total_y     : f32           = mapped_rb.y - mapped_lt.y;
          var v_x         : f32           = (mapped_pos.x - mapped_lt.x) / total_x;
          var v_y         : f32           = (mapped_pos.y - mapped_lt.y) / total_y;

          output.frag_coord = vec2<f32>(v_x, v_y);
        )"}};

  ReplacePlaceholder(wgsl_code, replacements);

  return wgsl_code;
}  // namespace skity

std::string WGSLTextureTessPath::GetShaderName() const {
  std::string name = "ImageTessPathVertexWGSL";

  return name;
}

const char* WGSLTextureTessPath::GetEntryPoint() const { return "vs_main"; }

void WGSLTextureTessPath::PrepareCMD(Command* cmd, HWDrawContext* context,
                                     const Matrix& transform, float clip_depth,
                                     Command* stencil_cmd) {
  SKITY_TRACE_EVENT(WGSLTextureTessPath_PrepareCMD);

  WGSLTessPathGeometry::PrepareCMD(cmd, context, transform, clip_depth,
                                   stencil_cmd);

  if (cmd->pipeline == nullptr) {
    return;
  }

  auto group = cmd->pipeline->GetBindingGroup(0);
  if (group == nullptr) {
    return;
  }

  auto image_bounds_entry = group->GetEntry(1);
  if (image_bounds_entry == nullptr ||
      image_bounds_entry->type_definition->name != "ImageBoundsInfo") {
    return;
  }

  auto image_bounds_struct = static_cast<wgx::StructDefinition*>(
      image_bounds_entry->type_definition.get());

  std::array<float, 2> bounds{width_, height_};
  image_bounds_struct->GetMember("bounds")->type->SetData(
      bounds.data(), bounds.size() * sizeof(float));

  image_bounds_struct->GetMember("inv_matrix")
      ->type->SetData(&local_matrix_, sizeof(Matrix));

  UploadBindGroup(image_bounds_entry, cmd, context);
}

}  // namespace skity
