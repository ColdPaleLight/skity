// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "src/render/hw/draw/fragment/wgsl_oval_fragment.hpp"

#include "src/render/hw/draw/wgx_utils.hpp"
#include "src/render/hw/hw_draw.hpp"
#include "src/render/hw/hw_stage_buffer.hpp"
#include "src/tracing.hpp"

namespace skity {

WGSLOvalFragment::WGSLOvalFragment(const Color4f& color) : color_(color) {}

uint32_t WGSLOvalFragment::NextBindingIndex() const { return 1; }

std::string WGSLOvalFragment::GetShaderName() const {
  std::string name = "OvalFragmentWGSL";

  if (filter_) {
    name += "_" + filter_->GetShaderName();
  }

  return name;
}

std::string WGSLOvalFragment::GenSourceWGSL() const {
  std::string wgsl_code = R"(
    @group(1) @binding(0) var<uniform> uColor: vec4<f32>;
  )";

  if (filter_ != nullptr) {
    wgsl_code += filter_->GenSourceWGSL();
  }

  wgsl_code += R"(
    struct FragmentInput {
              @location(0)        v_pos     :   vec2<f32>,
              @location(1)        v_center  :   vec2<f32>,
              @location(2)        v_radius  :   f32,
    };
    
      @fragment
      fn fs_main(
          input : FragmentInput
      ) -> @location(0) vec4<f32> {
    )";

  wgsl_code += R"(
    var dist : f32  = distance(input.v_pos, input.v_center) - input.v_radius;
    var a : f32 = 0.0;
    if dist < 0.0 {
      a = 1.0;
    }
    var color : vec4<f32> = vec4<f32>(uColor.rgb * uColor.a * a, uColor.a * a);
  )";

  if (filter_ != nullptr) {
    wgsl_code += R"(
       color = filter_color(color);
    )";
  }

  
    wgsl_code += R"(
        return color;
      }
    )";
  

  return wgsl_code;
}

const char* WGSLOvalFragment::GetEntryPoint() const { return "fs_main"; }

void WGSLOvalFragment::PrepareCMD(Command* cmd, HWDrawContext* context) {
  SKITY_TRACE_EVENT(WGSLOvalFragment_PrepareCMD);

  if (cmd == nullptr || cmd->pipeline == nullptr) {
    return;
  }

  auto group = cmd->pipeline->GetBindingGroup(1);

  if (group == nullptr) {
    return;
  }

  auto color_binding = group->GetEntry(0);
  if (color_binding->type_definition->name != "vec4<f32>") {
    return;
  }

  color_binding->type_definition->SetData(&color_, sizeof(Color4f));

  UploadBindGroup(color_binding, cmd, context);

  if (filter_ != nullptr) {
    filter_->SetupBindGroup(cmd, context);
  }
}

}  // namespace skity
