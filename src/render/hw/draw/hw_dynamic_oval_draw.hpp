// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef SRC_RENDER_HW_DRAW_HW_DYNAMIC_OVAL_DRAW_HPP
#define SRC_RENDER_HW_DRAW_HW_DYNAMIC_OVAL_DRAW_HPP

#include <skity/graphic/paint.hpp>
#include <skity/graphic/path.hpp>

#include "src/render/hw/draw/hw_dynamic_draw.hpp"

namespace skity {

class HWDynamicOvalDraw : public HWDynamicDraw {
 public:
  HWDynamicOvalDraw(Matrix transform, float radius, Vec2 center, Paint paint);

  ~HWDynamicOvalDraw() override = default;

 protected:
  void OnGenerateDrawStep(ArrayList<HWDrawStep *, 2> &steps,
                          HWDrawContext *context) override;

  //  private:
  // HWWGSLGeometry *GenGeometry(HWDrawContext *context, bool aa) const;

  // HWWGSLFragment *GenFragment(HWDrawContext *context) const;

 private:
  float radius_;
  Vec2 center_;
  Paint paint_;
};

}  // namespace skity

#endif  // SRC_RENDER_HW_DRAW_HW_DYNAMIC_OVAL_DRAW_HPP
