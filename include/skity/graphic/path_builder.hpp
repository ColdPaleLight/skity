// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef INCLUDE_SKITY_GRAPHIC_PATH_BUILDER_HPP
#define INCLUDE_SKITY_GRAPHIC_PATH_BUILDER_HPP

#include <cstddef>
#include <skity/graphic/path.hpp>

namespace skity {

/** @brief Builds an immutable Path using two-dimensional coordinates. */
class SKITY_API PathBuilder {
 public:
  PathBuilder() = default;
  explicit PathBuilder(const Path& src);
  /** @brief Imports src and resets it, reusing its storage when exclusive. */
  explicit PathBuilder(Path&& src);

  PathBuilder(const PathBuilder&) = delete;
  PathBuilder& operator=(const PathBuilder&) = delete;
  PathBuilder(PathBuilder&& other) noexcept;
  PathBuilder& operator=(PathBuilder&& other) noexcept;

  PathBuilder& MoveTo(float x, float y);
  PathBuilder& MoveTo(Vec2 point) { return MoveTo(point.x, point.y); }

  PathBuilder& LineTo(float x, float y);
  PathBuilder& LineTo(Vec2 point) { return LineTo(point.x, point.y); }

  PathBuilder& QuadTo(float x1, float y1, float x2, float y2);
  PathBuilder& QuadTo(Vec2 control, Vec2 end) {
    return QuadTo(control.x, control.y, end.x, end.y);
  }

  PathBuilder& ConicTo(float x1, float y1, float x2, float y2, float weight);
  PathBuilder& ConicTo(Vec2 control, Vec2 end, float weight) {
    return ConicTo(control.x, control.y, end.x, end.y, weight);
  }

  PathBuilder& CubicTo(float x1, float y1, float x2, float y2, float x3,
                       float y3);
  PathBuilder& CubicTo(Vec2 control1, Vec2 control2, Vec2 end) {
    return CubicTo(control1.x, control1.y, control2.x, control2.y, end.x,
                   end.y);
  }

  PathBuilder& ArcTo(float x1, float y1, float x2, float y2, float radius);
  PathBuilder& ArcTo(const Rect& oval, float start_angle, float sweep_angle,
                     bool force_move);
  PathBuilder& ArcTo(float rx, float ry, float rotation,
                     Path::ArcSize large_arc, Path::Direction sweep, float x,
                     float y);
  PathBuilder& Close();

  PathBuilder& AddCircle(float x, float y, float radius,
                         Path::Direction dir = Path::Direction::kCW);
  PathBuilder& AddOval(const Rect& oval,
                       Path::Direction dir = Path::Direction::kCW);
  PathBuilder& AddOval(const Rect& oval, Path::Direction dir, uint32_t start);
  PathBuilder& AddRect(const Rect& rect,
                       Path::Direction dir = Path::Direction::kCW,
                       uint32_t start = 0);
  PathBuilder& AddRect(float left, float top, float right, float bottom,
                       Path::Direction dir = Path::Direction::kCW) {
    return AddRect({left, top, right, bottom}, dir);
  }
  PathBuilder& AddRoundRect(const Rect& rect, float rx, float ry,
                            Path::Direction dir = Path::Direction::kCW);
  PathBuilder& AddRRect(const RRect& rrect,
                        Path::Direction dir = Path::Direction::kCW);
  PathBuilder& AddRRect(const RRect& rrect, Path::Direction dir,
                        uint32_t start);

  PathBuilder& AddPath(const Path& src,
                       Path::AddMode mode = Path::AddMode::kAppend);
  PathBuilder& AddPath(const Path& src, float dx, float dy,
                       Path::AddMode mode = Path::AddMode::kAppend);
  PathBuilder& AddPath(const Path& src, const Matrix& matrix,
                       Path::AddMode mode = Path::AddMode::kAppend);

  /** @brief Transforms points in place using the same mapping as
   * Path::CopyWithMatrix(). */
  PathBuilder& Transform(const Matrix& matrix);

  PathBuilder& SetLastPt(float x, float y);
  PathBuilder& SetLastPt(Vec2 point) { return SetLastPt(point.x, point.y); }
  PathBuilder& SetFillType(Path::PathFillType type) {
    fill_type_ = type;
    return *this;
  }

  /** @brief Clears geometry and restores winding fill, retaining capacity. */
  PathBuilder& Reset();
  /** @brief Reserves total array capacities without changing geometry. */
  void Reserve(size_t point_count, size_t verb_count,
               size_t conic_weight_count = 0);

  bool IsEmpty() const;
  size_t CountPoints() const;
  size_t CountVerbs() const;
  Path::PathFillType GetFillType() const { return fill_type_; }
  Vec2 GetPoint(int index) const;
  bool GetLastPt(Vec2* point) const;
  Vec2 GetLastMovePt() const;
  /** @brief Writes the current point, or the contour start after Close().
   * Returns false for an empty path. The output is optional. */
  bool GetCurrentPoint(Vec2* point) const;
  Rect GetBounds() const;

  /** @brief Returns an independent Path while retaining the builder's data. */
  Path Snapshot() const;
  /** @brief Transfers geometry to a Path and resets the builder. */
  Path Detach();
  /** @brief Replaces dst and recycles its geometry buffers if exclusive. */
  void DetachTo(Path* dst);

 private:
  friend class Path;
  friend class PathPriv;
  friend class PathStroker;

  using AddMode = Path::AddMode;
  using ArcSize = Path::ArcSize;
  using Direction = Path::Direction;
  using IsAType = Path::IsAType;
  using SegmentMask = Path::SegmentMask;
  using Verb = Path::Verb;

  Path::Data& Edit();
  void TransformPoints(const Matrix& matrix);
  void InjectMoveToIfNeed();
  bool IsZeroLengthSincePoint(int start_index) const;
  void ReverseAddPath(const Path& src);
  void ReversePathTo(const PathBuilder& src);

  // A builder never mutates storage that another Path can see.
  Path::StorageRef data_;
  Path::PathFillType fill_type_ = Path::PathFillType::kWinding;
  Path::ConvexityType convexity_hint_ = Path::ConvexityType::kUnknown;
};

}  // namespace skity

#endif  // INCLUDE_SKITY_GRAPHIC_PATH_BUILDER_HPP
