// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <skity/effect/path_effect.hpp>
#include <skity/graphic/path.hpp>
#include <skity/graphic/path_builder.hpp>

#include "src/effect/dash_path_effect.hpp"
#include "src/effect/discrete_path_effect.hpp"

namespace skity {

bool PathEffect::FilterPath(Path* dst, Path const& src, bool stroke,
                            Paint const& paint) const {
  if (!dst) {
    return false;
  }
  const bool in_place = dst == &src;
  PathBuilder builder(in_place ? Path{} : std::move(*dst));
  bool success = OnFilterPath(&builder, src, stroke, paint);
  if (success || !in_place) {
    builder.DetachTo(dst);
  }
  return success;
}

PathEffect::DashType PathEffect::AsADash(DashInfo* info) const {
  return this->OnAsADash(info);
}

std::shared_ptr<PathEffect> PathEffect::MakeDiscretePathEffect(
    float seg_length, float dev, uint32_t seed_assist) {
  return std::make_shared<DiscretePathEffect>(seg_length, dev, seed_assist);
}

std::shared_ptr<PathEffect> PathEffect::MakeDashPathEffect(
    const float* intervals, int count, float phase) {
  return std::make_shared<DashPathEffect>(intervals, count, phase);
}

}  // namespace skity
