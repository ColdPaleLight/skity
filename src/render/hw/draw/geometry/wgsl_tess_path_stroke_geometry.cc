// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "src/render/hw/draw/geometry/wgsl_tess_path_stroke_geometry.hpp"

#include <_types/_uint32_t.h>

#include <algorithm>
#include <cassert>
#include <optional>
#include <vector>

#include "skity/geometry/point.hpp"
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
constexpr static int32_t kMaxNumSegmentsPerInstance = 16;

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
          20 * sizeof(float),
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
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32x4,
                  8 * sizeof(float),
                  4,
              },
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32x4,
                  12 * sizeof(float),
                  5,
              },
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32x4,
                  16 * sizeof(float),
                  6,
              },
          },
      },
  };

  return layout;
}

struct Instance {
  Vec4 p0p1;
  Vec4 p2p3;
  Vec4 j0j1;
  Vec4 j2j3;
  float index_offset;
  float num_segments;
  float stroke_radius;
  float is_circle;
};

static_assert(sizeof(Instance) == 20 * sizeof(float));

struct TessPathStrokeVisitor : public PathVisitor {
  explicit TessPathStrokeVisitor(const Matrix& matrix, const Paint& paint)
      : PathVisitor(false, matrix),
        xform_(wangs_formula::VectorXform(matrix)),
        stroke_radius_(std::max(0.5f, paint.GetStrokeWidth() * 0.5f)),
        stroke_miter_(paint.GetStrokeMiter()),
        join_(paint.GetStrokeJoin()),
        cap_(paint.GetStrokeCap()) {}

  void OnBeginPath() override {}

  void OnEndPath() override { HandleCaps(); }

  void HandleCaps() {
    if (only_has_move_to_ || is_closed_ || cap_ == Paint::kButt_Cap) {
      return;
    }

    if (cap_ == Paint::kRound_Cap) {
      AddCircleInstance(first_point_);

      if (last_point_ != first_point_) {
        AddCircleInstance(last_point_);
      }
    } else if (cap_ == Paint::kSquare_Cap) {
      if (first_segment_index_ >= 0) {
        {  // start cap
          Instance& instance = instances[first_segment_index_];

          Vec2 p0 = Vec2{instance.p0p1.x, instance.p0p1.y};
          Vec2 p1 = Vec2{instance.p0p1.z, instance.p0p1.w};
          Vec2 p2 = Vec2{instance.p2p3.x, instance.p2p3.y};
          Vec2 p3 = Vec2{instance.p2p3.z, instance.p2p3.w};

          auto out_dir = (p0 - GetTangentPoint(p0, p1, p2, p3)).Normalize();
          AddLineInstance(p0, p0 + out_dir * stroke_radius_, false);
        }

        {  // end cap
           //          const Instance& instance = instances.back();
           //          Vec2 p0 = Vec2{instance.p0p1.x, instance.p0p1.y};
           //          Vec2 p1 = Vec2{instance.p0p1.z, instance.p0p1.w};
           //          Vec2 p2 = Vec2{instance.p2p3.x, instance.p2p3.y};
           //          Vec2 p3 = Vec2{instance.p2p3.z, instance.p2p3.w};
           //          auto out_dir = (p3 - GetTangentPoint(p3, p2, p1,
           //          p0)).Normalize();
          auto out_dir = (last_point_ - join_point_).Normalize();
          AddLineInstance(last_point_, last_point_ + out_dir * stroke_radius_,
                          false);
        }
      } else {
        // 这种情况下，只有一个点，直接生成一个方块就行
        AddLineInstance(first_point_ - Vec2{stroke_radius_, 0},
                        first_point_ + Vec2{stroke_radius_, 0}, false);
      }
    }
  }

  void OnMoveTo(Vec2 const& p) override {
    HandleCaps();

    only_has_move_to_ = true;
    first_point_ = p;
    last_point_ = p;
    first_segment_index_ = -1;
    is_closed_ = false;
  }

  void OnLineTo(Vec2 const& p0, Vec2 const& p1) override {
    only_has_move_to_ = false;
    if (p0 == p1) {
      return;
    }

    uint32_t segment_index = AddLineInstance(p0, p1, first_segment_index_ >= 0);
    if (first_segment_index_ < 0) {
      first_segment_index_ = segment_index;
    }
    join_point_ = p0;
    last_point_ = p1;
  }

  void OnQuadTo(Vec2 const& p0, Vec2 const& p1, Vec2 const& p2) override {
    only_has_move_to_ = false;
    auto ctr1 = (p0 + 2 * p1) / 3.f;
    auto ctr2 = (2 * p1 + p2) / 3.f;
    OnCubicTo(p0, ctr1, ctr2, p2);
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
    only_has_move_to_ = false;
    if (p0 == p1 && p1 == p2 && p2 == p3) {
      return;
    }

    arc_[0] = p0;
    arc_[1] = p1;
    arc_[2] = p2;
    arc_[3] = p3;
    uint32_t num = std::ceil(wangs_formula::Cubic(kPrecision, arc_, xform_));
    num = std::max(num, 1u);

    uint32_t remain = num % kMaxNumSegmentsPerInstance;
    uint32_t count = num / kMaxNumSegmentsPerInstance;

    auto segment_index =
        AddCubicInstance(p0, p1, p2, p3, 0.f, num, first_segment_index_ >= 0);

    for (uint32_t i = 1; i < count; i++) {
      AddCubicInstance(p0, p1, p2, p3,
                       static_cast<float>(i * kMaxNumSegmentsPerInstance), num,
                       false);
    }

    if (remain > 0) {
      AddCubicInstance(p0, p1, p2, p3,
                       static_cast<float>(count * kMaxNumSegmentsPerInstance),
                       num, false);
    }

    if (first_segment_index_ < 0) {
      first_segment_index_ = segment_index;
    }

    last_point_ = p3;
    join_point_ = GetTangentPoint(p3, p2, p1, p0);
  }

  void OnClose() override {
    if (only_has_move_to_) {
      return;
    }

    if (first_segment_index_ >= 0) {
      auto& instance = instances[first_segment_index_];
      Vec2 p0 = Vec2{instance.p0p1.x, instance.p0p1.y};
      Vec2 p1 = Vec2{instance.p0p1.z, instance.p0p1.w};
      Vec2 p2 = Vec2{instance.p2p3.x, instance.p2p3.y};
      Vec2 p3 = Vec2{instance.p2p3.z, instance.p2p3.w};
      GenerateJoin(join_point_, p0, GetTangentPoint(p0, p1, p2, p3), instance);
    }

    is_closed_ = true;
  }

  const std::vector<Instance>& GetInstances() const { return instances; }

  Vec2 GetTangentPoint(const Vec2& p0, const Vec2& p1, const Vec2& p2,
                       const Vec2& p3) const {
    if (p1 != p0) {
      return p1;
    }
    if (p2 != p1) {
      return p2;
    }
    return p3;
  }

 private:
  void AddCircleInstance(const Vec2& center) {
    instances.emplace_back();
    Instance& circle = instances.back();
    circle.p0p1 = Vec4{center, center};
    circle.p2p3 = Vec4{center, center};
    circle.j0j1 = Vec4{center, center};
    circle.j2j3 = Vec4{center, center};

    circle.index_offset = 0.f;
    circle.num_segments = kMaxNumSegmentsPerInstance;
    circle.is_circle = 1.f;
    circle.stroke_radius = stroke_radius_;
  }

  uint32_t AddLineInstance(const Vec2& p0, const Vec2& p1, bool needs_join) {
    uint32_t result = instances.size();
    instances.emplace_back();
    Instance& line = instances.back();
    Vec2 ctrl1 = 2.f / 3.f * p0 + 1.f / 3.f * p1;
    Vec2 ctrl2 = 2.f / 3.f * p1 + 1.f / 3.f * p0;
    line.p0p1 = Vec4{p0, ctrl1};
    line.p2p3 = Vec4{ctrl2, p1};
    line.j0j1 = Vec4{p0, p0};
    line.j2j3 = Vec4{p0, p0};

    line.index_offset = 0.f;
    line.num_segments = 1.f;
    line.is_circle = 0.f;
    line.stroke_radius = stroke_radius_;
    if (needs_join) {
      GenerateJoin(join_point_, p0, p1, line);
    }
    return result;
  }

  uint32_t AddCubicInstance(const Vec2& p0, const Vec2& p1, const Vec2& p2,
                            const Vec2& p3, float index_offset,
                            float num_segments, bool needs_join) {
    uint32_t result = instances.size();
    instances.emplace_back();
    Instance& cubic = instances.back();
    cubic.p0p1 = Vec4{p0, p1};
    cubic.p2p3 = Vec4{p2, p3};
    cubic.j0j1 = Vec4{p0, p0};
    cubic.j2j3 = Vec4{p0, p0};

    cubic.index_offset = index_offset;
    cubic.num_segments = num_segments;
    cubic.is_circle = 0.f;
    cubic.stroke_radius = stroke_radius_;

    if (needs_join) {
      GenerateJoin(join_point_, p0, GetTangentPoint(p0, p1, p2, p3), cubic);
    }
    return result;
  }

  void GenMiterJoin(const Vec2& center, const Vec2& p1, const Vec2& p2,
                    float stroke_radius, float stroke_miter,
                    Instance& instance) {
    auto pp1 = p1 - center;
    auto pp2 = p2 - center;

    auto out_dir = pp1 + pp2;

    float k = 2.f * stroke_radius * stroke_radius /
              (out_dir.x * out_dir.x + out_dir.y * out_dir.y);

    auto pe = k * out_dir;

    if (pe.Length() >= stroke_miter * stroke_radius) {
      // fallback to bevel_join
      instance.j0j1 = Vec4{center, p1};
      instance.j2j3 = Vec4{p2, center};
      return;
    }

    auto join = center + pe;

    instance.j0j1 = Vec4{center, p1};
    instance.j2j3 = Vec4{join, p2};
  }

  void GenerateJoin(Vec2 prev, Vec2 curr, Vec2 next, Instance& instance) {
    auto orientation = CalculateOrientation(prev, curr, next);

    auto cross_pr = CrossProductResult(prev, curr, next);
    if (orientation == Orientation::kLinear && cross_pr > 0) {
      instance.j0j1 = Vec4{curr, curr};
      instance.j2j3 = Vec4{curr, curr};
      return;
    }

    auto prev_dir = (curr - prev).Normalize();
    auto curr_dir = (next - curr).Normalize();

    auto prev_normal = Vec2{-prev_dir.y, prev_dir.x};
    auto current_normal = Vec2{-curr_dir.y, curr_dir.x};

    Vec2 prev_join = {};
    Vec2 curr_join = {};

    if (orientation == Orientation::kAntiClockWise ||
        (orientation == Orientation::kLinear && cross_pr < 0)) {
      prev_join = curr - prev_normal * stroke_radius_;
      curr_join = curr - current_normal * stroke_radius_;
    } else {
      prev_join = curr + prev_normal * stroke_radius_;
      curr_join = curr + current_normal * stroke_radius_;
    }

    if ((orientation == Orientation::kLinear && join_ != Paint::kRound_Join) ||
        join_ == Paint::kBevel_Join) {
      instance.j0j1 = Vec4{curr, prev_join};
      instance.j2j3 = Vec4{curr_join, curr};
      return;
    }

    if (join_ == Paint::kMiter_Join) {
      GenMiterJoin(curr, prev_join, curr_join, stroke_radius_, stroke_miter_,
                   instance);
    } else if (join_ == Paint::kRound_Join) {
      float delta = (prev_join - curr_join).Length();
      if (delta < 1.f) {
        instance.j0j1 = Vec4{curr, prev_join};
        instance.j2j3 = Vec4{curr_join, curr};
      } else {
        instance.j0j1 = Vec4{curr, curr};
        instance.j2j3 = Vec4{curr, curr};

        AddCircleInstance(curr);
      }
    }
  }

  std::vector<Instance> instances;
  wangs_formula::VectorXform xform_;
  Vec2 arc_[4];

  Vec2 first_point_;
  Vec2 last_point_;
  Vec2 join_point_;
  bool only_has_move_to_ = true;
  int32_t first_segment_index_ = -1;
  bool is_closed_ = false;

  const float stroke_radius_;
  const float stroke_miter_;
  const Paint::Join join_;
  const Paint::Cap cap_;
};

}  // namespace

WGSLTessPathStrokeGeometry::WGSLTessPathStrokeGeometry(const Path& path,
                                                       const Paint& paint)
    : path_(path), paint_(paint), layout_(InitVertexBufferLayout()) {}

const std::vector<GPUVertexBufferLayout>&
WGSLTessPathStrokeGeometry::GetBufferLayout() const {
  return layout_;
}

std::string WGSLTessPathStrokeGeometry::GenSourceWGSL() const {
  std::string wgsl_code = CommonVertexWGSL();

  wgsl_code += R"(
      @group(0) @binding(0) var<uniform> common_slot: CommonSlot;
      // @ExtraUniform

      struct VSInput {
          @location(0) index: f32,
          @location(1) sign : f32,
          @location(2) p0p1 : vec4<f32>,
          @location(3) p2p3 : vec4<f32>,
          @location(4) j0j1 : vec4<f32>,
          @location(5) j2j3 : vec4<f32>,
          @location(6) pack : vec4<f32>,
      };

      struct VSOutput {
          @builtin(position) pos: vec4<f32>,
          // @ExtraVSOutput
      };

      fn cubic_bezier_tangent(p0: vec2<f32>, p1: vec2<f32>, p2: vec2<f32>, p3: vec2<f32>, t: f32) -> vec2<f32> {
         var u: f32 = 1.0 - t;
         var tangent: vec2<f32> = 3.0 * u * u * (p1 - p0) +
                                  6.0 * u * t * (p2 - p1) +
                                  3.0 * t * t * (p3 - p2);
         return tangent;
      }

      fn get_join_pos(index: i32, j0: vec2<f32>, j1: vec2<f32>, j2: vec2<f32>, j3: vec2<f32>) -> vec2<f32> {
        var points: array<vec2<f32>, 4> = array<vec2<f32>, 4>(j0, j1, j2, j3);
        let idx: u32 = u32(-index - 1);
        return points[idx];
      }

      @vertex
      fn vs_main(input: VSInput) -> VSOutput {
          var output: VSOutput;
          var pos: vec2<f32>;
         
          // 如果是round cap或者sqare cap，会放进单独的instance进行处理
          // 对于round cap，我们直接绘制一个圆
          // 然后num_segments代表半圆需要被切成几段
          // index表示这是圆的第几段
          // 然后对圆进行拉链方式的拆解

          var p0: vec2<f32> = input.p0p1.xy;
          var p1: vec2<f32> = input.p0p1.zw;
          var p2: vec2<f32> = input.p2p3.xy;
          var p3: vec2<f32> = input.p2p3.zw;
          var j0: vec2<f32> = input.j0j1.xy;
          var j1: vec2<f32> = input.j0j1.zw;
          var j2: vec2<f32> = input.j2j3.xy;
          var j3: vec2<f32> = input.j2j3.zw;

          var index_offset: f32 = input.pack.x;
          var num_segments: f32 = input.pack.y;
          var stroke_radius: f32 = input.pack.z;
          var is_circle: f32 = input.pack.w;

          var index: f32 = input.index + index_offset;
          if is_circle == 1.0 {
            // 圆
            var angle: f32 = index / num_segments * 3.1415926;
            var dir: vec2<f32> = vec2<f32>(cos(angle), sin(angle));
            pos = p0 + input.sign * dir * stroke_radius;
          } else if index < 0.0 {
            pos = get_join_pos(index, j0, j1, j2, j3);
          } else if index > num_segments {
            pos = p3;
          } else {
            var t: f32 = index / num_segments;
            var p01: vec2<f32> = mix(p0, p1, t);
            var p12: vec2<f32> = mix(p1, p2, t);
            var p23: vec2<f32> = mix(p2, p3, t);

            var p012: vec2<f32> = mix(p01, p12, t);
            var p123: vec2<f32> = mix(p12, p23, t);
            pos = mix(p012, p123, t);

            var tangent: vec2<f32> = normalize(cubic_bezier_tangent(p0, p1, p2, p3, t));
            var norm: vec2<f32> = vec2<f32>(-tangent.y, tangent.x);
            pos = pos + norm.xy * stroke_radius * input.sign;
          }
          
          output.pos = get_vertex_position(pos.xy, common_slot);
          // @ExtraBeforeReturn
          return output;
      }
    )";

  return wgsl_code;
}

std::string WGSLTessPathStrokeGeometry::GetShaderName() const {
  std::string name = "CommonTessPathStrokeVertexWGSL";

  return name;
}

const char* WGSLTessPathStrokeGeometry::GetEntryPoint() const {
  return "vs_main";
}

namespace {}

void WGSLTessPathStrokeGeometry::PrepareCMD(Command* cmd,
                                            HWDrawContext* context,
                                            const Matrix& transform,
                                            float clip_depth,
                                            Command* stencil_cmd) {
  SKITY_TRACE_EVENT(WGSLTessPathStrokeGeometry_PrepareCMD);

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

  std::vector<float> vertex_array;

  for (int i = 0; i < 2 * (kMaxNumSegmentsPerInstance + 1); i++) {
    vertex_array.push_back(i % (kMaxNumSegmentsPerInstance + 1));  // index
    if (i / (kMaxNumSegmentsPerInstance + 1) == 0) {
      vertex_array.push_back(1);  // offset
    } else {
      vertex_array.push_back(-1);  // offset
    }
  }

  std::vector<uint32_t> index_array;
  for (int i = 0; i < kMaxNumSegmentsPerInstance; i++) {
    uint32_t outer_curr = i;
    uint32_t outer_next = i + 1;
    uint32_t inner_curr = outer_curr + (kMaxNumSegmentsPerInstance + 1);
    uint32_t inner_next = outer_next + (kMaxNumSegmentsPerInstance + 1);

    index_array.push_back(outer_curr);
    index_array.push_back(outer_next);
    index_array.push_back(inner_next);

    index_array.push_back(outer_curr);
    index_array.push_back(inner_next);
    index_array.push_back(inner_curr);
  }

  uint32_t join_index_base = 2 * (kMaxNumSegmentsPerInstance + 1);

  // Join Vertex 0
  vertex_array.push_back(-1);
  vertex_array.push_back(0);

  // Join Vertex 1
  vertex_array.push_back(-2);
  vertex_array.push_back(0);

  // Join Vertex 2
  vertex_array.push_back(-3);
  vertex_array.push_back(0);

  // Join Vertex 3
  vertex_array.push_back(-4);
  vertex_array.push_back(0);

  index_array.push_back(join_index_base);      // -1
  index_array.push_back(join_index_base + 1);  // -2
  index_array.push_back(join_index_base + 2);  // -3

  index_array.push_back(join_index_base);      // -1
  index_array.push_back(join_index_base + 2);  // -3
  index_array.push_back(join_index_base + 3);  // -4

  cmd->vertex_buffer =
      context->stageBuffer->Push(const_cast<float*>(vertex_array.data()),
                                 vertex_array.size() * sizeof(float));
  cmd->index_buffer =
      context->stageBuffer->PushIndex(const_cast<uint32_t*>(index_array.data()),
                                      index_array.size() * sizeof(uint32_t));

  cmd->index_count = index_array.size();

  TessPathStrokeVisitor path_visitor(transform, paint_);
  path_visitor.VisitPath(path_, false);

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

WGSLGradientTessPathStroke::WGSLGradientTessPathStroke(
    const Path& path, const Paint& paint, const Matrix& local_matrix)
    : WGSLTessPathStrokeGeometry(path, paint), local_matrix_(local_matrix) {}

std::string WGSLGradientTessPathStroke::GenSourceWGSL() const {
  auto wgsl = WGSLTessPathStrokeGeometry::GenSourceWGSL();
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

std::string WGSLGradientTessPathStroke::GetShaderName() const {
  std::string name = "CommonGradientTessPathStrokeVertexWGSL";

  return name;
}

const char* WGSLGradientTessPathStroke::GetEntryPoint() const {
  return "vs_main";
}

void WGSLGradientTessPathStroke::PrepareCMD(Command* cmd,
                                            HWDrawContext* context,
                                            const Matrix& transform,
                                            float clip_depth,
                                            Command* stencil_cmd) {
  SKITY_TRACE_EVENT(WGSLGradientPath_PrepareCMD);

  WGSLTessPathStrokeGeometry::PrepareCMD(cmd, context, transform, clip_depth,
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

WGSLTextureTessPathStroke::WGSLTextureTessPathStroke(const Path& path,
                                                     const Paint& paint,

                                                     const Matrix& local_matrix,
                                                     float width, float height)
    : WGSLTessPathStrokeGeometry(path, paint),
      local_matrix_(local_matrix),
      width_(width),
      height_(height) {}

std::string WGSLTextureTessPathStroke::GenSourceWGSL() const {
  std::string wgsl_code = CommonVertexWGSL();
  wgsl_code += R"(
    struct ImageBoundsInfo {
      bounds      : vec2<f32>,
      inv_matrix  : mat4x4<f32>,
    };

  )";
  wgsl_code += WGSLTessPathStrokeGeometry::GenSourceWGSL();
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

std::string WGSLTextureTessPathStroke::GetShaderName() const {
  std::string name = "ImageTessPathStrokeVertexWGSL";

  return name;
}

const char* WGSLTextureTessPathStroke::GetEntryPoint() const {
  return "vs_main";
}

void WGSLTextureTessPathStroke::PrepareCMD(Command* cmd, HWDrawContext* context,
                                           const Matrix& transform,
                                           float clip_depth,
                                           Command* stencil_cmd) {
  SKITY_TRACE_EVENT(WGSLTextureTessPath_PrepareCMD);

  WGSLTessPathStrokeGeometry::PrepareCMD(cmd, context, transform, clip_depth,
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
