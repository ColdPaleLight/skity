// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef SRC_RENDER_HW_DRAW_GEOMETRY_WGSL_OVAL_GEOMETRY_HPP
#define SRC_RENDER_HW_DRAW_GEOMETRY_WGSL_OVAL_GEOMETRY_HPP

#include <skity/graphic/paint.hpp>
#include <skity/graphic/path.hpp>
#include <vector>

#include "src/render/hw/draw/hw_wgsl_geometry.hpp"

namespace skity {

class WGSLOvalGeometry : public HWWGSLGeometry {
 public:
  struct Oval {
    Vec2 center;
    float radius;
    float padding;
  };

  static_assert(sizeof(Oval) == 16);

 public:
  WGSLOvalGeometry(std::vector<Oval>, const Paint& paint);

  ~WGSLOvalGeometry() override = default;

  const std::vector<GPUVertexBufferLayout>& GetBufferLayout() const override;

  std::string GenSourceWGSL() const override;

  std::string GetShaderName() const override;

  const char* GetEntryPoint() const override;

  void PrepareCMD(Command* cmd, HWDrawContext* context, const Matrix& transform,
                  float clip_depth, Command* stencil_cmd) override;

 private:
  std::vector<Oval> ovals_;
  const Paint& paint_;
  std::vector<GPUVertexBufferLayout> layout_;
};

}  // namespace skity

#endif  // SRC_RENDER_HW_DRAW_GEOMETRY_WGSL_PATH_GEOMETRY_HPP
