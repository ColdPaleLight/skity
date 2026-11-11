// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <skity/graphic/path_builder.hpp>
#include <utility>

#include "src/graphic/path_data.hpp"

namespace skity {

PathBuilder::PathBuilder(const Path& src)
    : data_(src.data_ ? new Path::Data(*src.data_) : nullptr),
      fill_type_(src.fill_type_) {
  if (data_) {
    data_->first_direction_ = src.GetFirstDirection();
  }
}

PathBuilder::PathBuilder(Path&& src) : fill_type_(src.fill_type_) {
  if (src.data_) {
    const auto direction = src.GetFirstDirection();
    if (src.data_.IsUnique()) {
      data_ = std::move(src.data_);
      data_->convexity_cache_.store(0, std::memory_order_relaxed);
    } else {
      data_ = Path::StorageRef(new Path::Data(*src.data_));
    }
    data_->first_direction_ = direction;
  }
  src = Path{};
}

PathBuilder::PathBuilder(PathBuilder&& other) noexcept
    : data_(std::move(other.data_)),
      fill_type_(other.fill_type_),
      convexity_hint_(other.convexity_hint_) {
  other.Reset();
}

PathBuilder& PathBuilder::operator=(PathBuilder&& other) noexcept {
  if (this != &other) {
    data_.Swap(other.data_);
    fill_type_ = other.fill_type_;
    convexity_hint_ = other.convexity_hint_;
    other.Reset();
  }
  return *this;
}

PathBuilder& PathBuilder::Transform(const Matrix& matrix) {
  TransformPoints(matrix);
  if (data_) {
    data_->first_direction_ = Direction::kUnknown;
  }
  convexity_hint_ = Path::ConvexityType::kUnknown;
  return *this;
}

void PathBuilder::TransformPoints(const Matrix& matrix) {
  if (!data_) {
    return;
  }
  for (size_t i = 0; i < data_->points_.size(); ++i) {
    data_->SetPoint(i, matrix * data_->Point4(i));
  }
  data_->MarkBoundsDirty();
  data_->type_ = IsAType::kGeneral;
}

PathBuilder& PathBuilder::Reset() {
  if (data_) {
    data_->Reset();
  }
  fill_type_ = Path::PathFillType::kWinding;
  convexity_hint_ = Path::ConvexityType::kUnknown;
  return *this;
}

void PathBuilder::Reserve(size_t point_count, size_t verb_count,
                          size_t conic_weight_count) {
  auto& data = Edit();
  data.points_.reserve(point_count);
  data.verbs_.reserve(verb_count);
  data.conic_weights_.reserve(conic_weight_count);
}

bool PathBuilder::IsEmpty() const { return CountVerbs() == 0; }
size_t PathBuilder::CountPoints() const {
  return data_ ? data_->points_.size() : 0;
}
size_t PathBuilder::CountVerbs() const {
  return data_ ? data_->verbs_.size() : 0;
}
Vec2 PathBuilder::GetPoint(int index) const {
  return index >= 0 && static_cast<size_t>(index) < CountPoints()
             ? data_->points_[index]
             : Vec2{};
}
bool PathBuilder::GetLastPt(Vec2* point) const {
  bool has_point = CountPoints() != 0;
  if (point) {
    *point = has_point ? data_->points_.back() : Vec2{};
  }
  return has_point;
}
Vec2 PathBuilder::GetLastMovePt() const {
  if (IsEmpty()) {
    return {};
  }
  const int32_t index = data_->last_move_to_index_;
  return GetPoint(index < 0 ? ~index : index);
}
bool PathBuilder::GetCurrentPoint(Vec2* point) const {
  if (IsEmpty()) {
    return false;
  }
  if (point) {
    *point = data_->verbs_.back() == Verb::kClose ? GetLastMovePt()
                                                  : data_->points_.back();
  }
  return true;
}
Rect PathBuilder::GetBounds() const {
  if (!data_) {
    return {};
  }
  return data_->GetBounds().rect;
}

Path PathBuilder::Snapshot() const {
  Path::StorageRef copy(IsEmpty() ? nullptr : new Path::Data(*data_));
  if (copy) {
    copy->CanonicalizeZW();
  }
  return Path(std::move(copy), fill_type_, convexity_hint_);
}

Path PathBuilder::Detach() {
  Path::StorageRef data;
  if (!IsEmpty()) {
    data_->CanonicalizeZW();
    data = std::move(data_);
  }
  Path result(std::move(data), fill_type_, convexity_hint_);
  Reset();
  return result;
}

void PathBuilder::DetachTo(Path* dst) {
  if (!dst) {
    return;
  }
  Path::StorageRef recycled;
  if (dst->data_.IsUnique()) {
    recycled = std::move(dst->data_);
  }
  *dst = Detach();
  if (recycled) {
    recycled->Reset();
    data_ = std::move(recycled);
  }
}

}  // namespace skity
