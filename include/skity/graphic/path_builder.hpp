// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef INCLUDE_SKITY_GRAPHIC_PATH_BUILDER_HPP
#define INCLUDE_SKITY_GRAPHIC_PATH_BUILDER_HPP

#include <skity/graphic/path.hpp>

namespace skity {

// Owns writable geometry. Published Paths are independent of later edits.
class SKITY_API PathBuilder {
 public:
  using Direction = Path::Direction;
  using ArcSize = Path::ArcSize;
  using AddMode = Path::AddMode;
  using PathFillType = Path::PathFillType;
  using ConvexityType = Path::ConvexityType;

  PathBuilder() = default;
  // Copies geometry for editing; the source Path remains unchanged.
  explicit PathBuilder(const Path& path);
  PathBuilder(const PathBuilder& other);
  PathBuilder& operator=(const PathBuilder& other);
  PathBuilder(PathBuilder&&) noexcept = default;
  PathBuilder& operator=(PathBuilder&&) noexcept = default;

  // Transfers geometry without copying and resets to an empty winding path.
  // Transferred capacity belongs to the returned Path, not this builder.
  Path Detach();
  // Copies geometry, retaining this builder's content and capacity.
  Path Snapshot() const;
  // Clears geometry and retains this builder's capacity.
  PathBuilder& Reset() {
    path_.Reset();
    return *this;
  }

  PathBuilder& MoveTo(float x, float y) {
    path_.MoveTo(x, y);
    return *this;
  }
  PathBuilder& MoveTo(const Point& point) {
    path_.MoveTo(point);
    return *this;
  }
  PathBuilder& LineTo(float x, float y) {
    path_.LineTo(x, y);
    return *this;
  }
  PathBuilder& LineTo(const Point& point) {
    path_.LineTo(point);
    return *this;
  }
  PathBuilder& QuadTo(float x1, float y1, float x2, float y2) {
    path_.QuadTo(x1, y1, x2, y2);
    return *this;
  }
  PathBuilder& QuadTo(const Point& p1, const Point& p2) {
    path_.QuadTo(p1, p2);
    return *this;
  }
  PathBuilder& ConicTo(float x1, float y1, float x2, float y2, float weight) {
    path_.ConicTo(x1, y1, x2, y2, weight);
    return *this;
  }
  PathBuilder& ConicTo(const Point& p1, const Point& p2, float weight) {
    path_.ConicTo(p1, p2, weight);
    return *this;
  }
  PathBuilder& CubicTo(float x1, float y1, float x2, float y2, float x3,
                       float y3) {
    path_.CubicTo(x1, y1, x2, y2, x3, y3);
    return *this;
  }
  PathBuilder& CubicTo(const Point& p1, const Point& p2, const Point& p3) {
    path_.CubicTo(p1, p2, p3);
    return *this;
  }
  PathBuilder& ArcTo(float x1, float y1, float x2, float y2, float radius) {
    path_.ArcTo(x1, y1, x2, y2, radius);
    return *this;
  }
  PathBuilder& ArcTo(const Rect& oval, float start_angle, float sweep_angle,
                     bool force_move) {
    path_.ArcTo(oval, start_angle, sweep_angle, force_move);
    return *this;
  }
  PathBuilder& ArcTo(float rx, float ry, float x_axis_rotate, ArcSize large_arc,
                     Direction sweep, float x, float y) {
    path_.ArcTo(rx, ry, x_axis_rotate, large_arc, sweep, x, y);
    return *this;
  }
  PathBuilder& Close() {
    path_.Close();
    return *this;
  }
  PathBuilder& ReverseAddPath(const Path& src) {
    path_.ReverseAddPath(src);
    return *this;
  }
  PathBuilder& AddCircle(float x, float y, float radius,
                         Direction dir = Direction::kCW) {
    path_.AddCircle(x, y, radius, dir);
    return *this;
  }
  PathBuilder& AddOval(const Rect& oval, Direction dir = Direction::kCW) {
    path_.AddOval(oval, dir);
    return *this;
  }
  PathBuilder& AddOval(const Rect& oval, Direction dir, uint32_t start) {
    path_.AddOval(oval, dir, start);
    return *this;
  }
  PathBuilder& AddRect(const Rect& rect, Direction dir, uint32_t start) {
    path_.AddRect(rect, dir, start);
    return *this;
  }
  PathBuilder& AddRect(const Rect& rect, Direction dir = Direction::kCW) {
    path_.AddRect(rect, dir);
    return *this;
  }
  PathBuilder& AddRect(float left, float top, float right, float bottom,
                       Direction dir = Direction::kCW) {
    path_.AddRect(left, top, right, bottom, dir);
    return *this;
  }
  PathBuilder& AddRoundRect(const Rect& rect, float rx, float ry,
                            Direction dir = Direction::kCW) {
    path_.AddRoundRect(rect, rx, ry, dir);
    return *this;
  }
  PathBuilder& AddRoundRect(const Rect& rect, const float radii[],
                            Direction dir = Direction::kCW) {
    path_.AddRoundRect(rect, radii, dir);
    return *this;
  }
  PathBuilder& AddRRect(const RRect& rrect, Direction dir = Direction::kCW) {
    path_.AddRRect(rrect, dir);
    return *this;
  }
  PathBuilder& AddRRect(const RRect& rrect, Direction dir, uint32_t start) {
    path_.AddRRect(rrect, dir, start);
    return *this;
  }
  PathBuilder& ReversePathTo(const Path& src) {
    path_.ReversePathTo(src);
    return *this;
  }
  PathBuilder& AddPath(const Path& src, float dx, float dy,
                       AddMode mode = AddMode::kAppend) {
    path_.AddPath(src, dx, dy, mode);
    return *this;
  }
  PathBuilder& AddPath(const Path& src, AddMode mode = AddMode::kAppend) {
    path_.AddPath(src, mode);
    return *this;
  }
  PathBuilder& AddPath(const Path& src, const Matrix& matrix,
                       AddMode mode = AddMode::kAppend) {
    path_.AddPath(src, matrix, mode);
    return *this;
  }
  PathBuilder& SetLastPt(float x, float y) {
    path_.SetLastPt(x, y);
    return *this;
  }
  PathBuilder& SetLastPt(const Point& point) {
    path_.SetLastPt(point);
    return *this;
  }
  PathBuilder& SetFirstDirection(Direction dir) {
    path_.SetFirstDirection(dir);
    return *this;
  }
  PathBuilder& SetFillType(PathFillType type) {
    path_.SetFillType(type);
    return *this;
  }
  PathBuilder& SetConvexityType(ConvexityType type) {
    path_.SetConvexityType(type);
    return *this;
  }

  bool IsEmpty() const { return path_.IsEmpty(); }
  bool IsFinite() const { return path_.IsFinite(); }
  size_t CountPoints() const { return path_.CountPoints(); }
  size_t CountVerbs() const { return path_.CountVerbs(); }
  Rect GetBounds() const { return path_.GetBounds(); }
  PathFillType GetFillType() const { return path_.GetFillType(); }
  bool GetLastPt(Point* point) const { return path_.GetLastPt(point); }
  Point GetPoint(int index) const { return path_.GetPoint(index); }
  Point GetLastMovePt() const { return path_.GetLastMovePt(); }

 private:
  Path path_;
};

}  // namespace skity

#endif  // INCLUDE_SKITY_GRAPHIC_PATH_BUILDER_HPP
