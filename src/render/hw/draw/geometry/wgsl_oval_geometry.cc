// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "src/render/hw/draw/geometry/wgsl_oval_geometry.hpp"

#include <vector>

#include "src/render/hw/draw/wgx_utils.hpp"
#include "src/render/hw/hw_draw.hpp"
#include "src/render/hw/hw_stage_buffer.hpp"
#include "src/tracing.hpp"

namespace skity {

namespace {

std::vector<GPUVertexBufferLayout> InitVertexBufferLayout() {
  std::vector<GPUVertexBufferLayout> layout = {
      // vertex buffer
      GPUVertexBufferLayout{
          2 * sizeof(float),
          GPUVertexStepMode::kVertex,
          {
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32x2,
                  0,
                  0,
              },
          },
      },
      // instance buffer
      GPUVertexBufferLayout{
          4 * sizeof(float),
          GPUVertexStepMode::kInstance,
          {
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32x2,
                  0,
                  1,
              },
              GPUVertexAttribute{
                  GPUVertexFormat::kFloat32,
                  2 * sizeof(float),
                  2,
              },
          },
      },
  };

  return layout;
}

}  // namespace

WGSLOvalGeometry::WGSLOvalGeometry(std::vector<Oval> ovals, const Paint& paint)
    : ovals_(std::move(ovals)),
      paint_(paint),
      layout_(InitVertexBufferLayout()) {}

const std::vector<GPUVertexBufferLayout>& WGSLOvalGeometry::GetBufferLayout()
    const {
  return layout_;
}

std::string WGSLOvalGeometry::GenSourceWGSL() const {
  std::string wgsl_code = CommonVertexWGSL();

  wgsl_code += R"(
      struct OvalVertex {
          @location(0)  a_pos     :   vec2<f32>,
          @location(1)  center    :   vec2<f32>,
          @location(2)  radius    :   f32,
      };


      struct OvalVSOutput {
          @builtin(position)  pos       :   vec4<f32>,
          @location(0)        v_pos     :   vec2<f32>,
          @location(1)        v_center  :   vec2<f32>,
          @location(2)        v_radius  :   f32,
      };

      @group(0) @binding(0) var<uniform> common_slot  : CommonSlot;
      @vertex
      fn vs_main(vertex : OvalVertex) -> OvalVSOutput {
          var output: OvalVSOutput;
          var pos: vec2<f32> = vertex.a_pos * vertex.radius + vertex.center;
          output.pos      = get_vertex_position(pos, common_slot);
          output.v_pos    = pos;
          output.v_center = vertex.center;
          output.v_radius = vertex.radius;

          return output;
      }
    )";

  return wgsl_code;
}

std::string WGSLOvalGeometry::GetShaderName() const {
  std::string name = "CommonOvalVertexWGSL";

  return name;
}

const char* WGSLOvalGeometry::GetEntryPoint() const { return "vs_main"; }

void WGSLOvalGeometry::PrepareCMD(Command* cmd, HWDrawContext* context,
                                  const Matrix& transform, float clip_depth,
                                  Command* stencil_cmd) {
  SKITY_TRACE_EVENT(WGSLOvalGeometry_PrepareCMD);

  if (cmd->pipeline == nullptr) {
    return;
  }

  std::vector<float> vertex_array = {
      -1.0f, -1.0f,  // top left
      1.0f,  -1.0f,  // top right
      1.0f,  1.0f,   // bottom right
      -1.0f, 1.0f,   // bottom left
  };
  std::vector<uint32_t> index_array = {0, 1, 2, 0, 2, 3};

  cmd->vertex_buffer =
      context->stageBuffer->Push(const_cast<float*>(vertex_array.data()),
                                 vertex_array.size() * sizeof(float));
  cmd->index_buffer =
      context->stageBuffer->PushIndex(const_cast<uint32_t*>(index_array.data()),
                                      index_array.size() * sizeof(uint32_t));
  cmd->index_count = index_array.size();
  struct Oval {
    Vec2 center;
    float radius;
    float padding;
  };

  ovals_.push_back({
      Vec2{50, 80},
      65,
      0,
  });

  cmd->instance_count = ovals_.size();
  cmd->instance_buffer =
      context->stageBuffer->Push(ovals_.data(), ovals_.size() * sizeof(Oval));

  auto pipeline = cmd->pipeline;

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
