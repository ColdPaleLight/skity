// Copyright 2006 The Android Open Source Project

// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <algorithm>
#include <array>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <skity/graphic/path.hpp>
#include <skity/graphic/path_builder.hpp>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <utility>

#include "src/geometry/conic.hpp"
#include "src/geometry/geometry.hpp"
#include "src/geometry/math.hpp"
#include "src/geometry/point_priv.hpp"
#include "src/graphic/path_data.hpp"
#include "src/graphic/path_priv.hpp"
#include "src/graphic/path_scanner.hpp"
#include "src/logging.hpp"

namespace skity {

static bool arc_is_long_point(Rect const& oval, float start_angle,
                              float sweep_angle, Point* pt) {
  if (0 == sweep_angle && (0 == start_angle || 360.f == start_angle)) {
    // Chrome uses this path to move into and out of ovals. If not
    // treated as a special case the moves can distort the oval's
    // bounding box (and break the circle special case).
    PointSet(*pt, oval.Right(), oval.CenterY());
    return true;
  } else if (0 == oval.Width() && 0 == oval.Height()) {
    // Chrome will sometimes create 0 radius round rects. Having degenerate
    // quad segments in the path prevents the path from being recognized as
    // a rect.
    // TODO(tangruiwen): optimizing the case where only one of width or height
    // is zero should also be considered. This case, however, doesn't seem to be
    // as common as the single point case.
    PointSet(*pt, oval.Right(), oval.Top());
    return true;
  }
  return false;
}

static std::tuple<Vec2, Vec2, RotationDirection> angles_to_unit_vectors(
    float startAngle, float sweepAngle) {
  float startRad = glm::radians(startAngle),
        stopRad = glm::radians(startAngle + sweepAngle);

  Vec2 startV;
  Vec2 stopV;
  RotationDirection dir;

  startV.y = FloatSinSnapToZero(startRad);
  startV.x = FloatCosSnapToZero(startRad);
  stopV.y = FloatSinSnapToZero(stopRad);
  stopV.x = FloatCosSnapToZero(stopRad);

  /*  If the sweep angle is nearly (but less than) 360, then due to precision
     loss in radians-conversion and/or sin/cos, we may end up with coincident
     vectors, which will fool SkBuildQuadArc into doing nothing (bad) instead
     of drawing a nearly complete circle (good).
     e.g. canvas.drawArc(0, 359.99, ...)
     -vs- canvas.drawArc(0, 359.9, ...)
     We try to detect this edge case, and tweak the stop vector
     */
  if (startV == stopV) {
    float sw = glm::abs(sweepAngle);
    if (sw < 360.f && sw > 359.f) {
      // make a guess at a tiny angle (in radians) to tweak by
      float deltaRad = FloatCopySign(Float1 / 512, sweepAngle);
      // not sure how much will be enough, so we use a loop
      do {
        stopRad -= deltaRad;
        stopV.y = FloatSinSnapToZero(stopRad);
        stopV.x = FloatCosSnapToZero(stopRad);
      } while (startV == stopV);
    }
  }

  dir = sweepAngle > 0 ? RotationDirection::kCW : RotationDirection::kCCW;

  return {startV, stopV, dir};
}

/**
 *  If this returns 0, then the caller should just line-to the singlePt, else it
 * should ignore singlePt and append the specified number of conics.
 */
static int build_arc_conics(const Rect& oval, const Vec2& start,
                            const Vec2& stop, RotationDirection dir,
                            Conic conics[Conic::kMaxConicsForArc],
                            Point* singlePt) {
  auto matrix = Matrix::Translate(oval.CenterX(), oval.CenterY()) *
                Matrix::Scale(oval.Width() * 0.5f, oval.Height() * 0.5f);

  int count = Conic::BuildUnitArc(start, stop, dir, &matrix, conics);
  if (0 == count) {
    *singlePt = matrix * Vec4{stop.x, stop.y, 0.f, 1.f};
  }
  return count;
}

template <size_t N>
class Path_PointIterator {
 public:
  using Array = std::array<Vec2, N>;

  Path_PointIterator(Path::Direction dir, size_t startIndex)
      : current_(startIndex % N),
        advance_(dir == Path::Direction::kCW ? 1 : N - 1) {}

  const Vec2& current() const {
    assert(current_ < N);
    return pts[current_];
  }

  const Vec2& next() {
    current_ = (current_ + advance_) % N;
    return this->current();
  }

 protected:
  Array pts;

 private:
  size_t current_;
  size_t advance_;
};

class Path_RectPointIterator : public Path_PointIterator<4> {
 public:
  Path_RectPointIterator(const Rect& rect, Path::Direction dir,
                         size_t startIndex)
      : Path_PointIterator<4>(dir, startIndex) {
    pts[0] = Vec2{rect.Left(), rect.Top()};
    pts[1] = Vec2{rect.Right(), rect.Top()};
    pts[2] = Vec2{rect.Right(), rect.Bottom()};
    pts[3] = Vec2{rect.Left(), rect.Bottom()};
  }
};

class Path_OvalPointIterator : public Path_PointIterator<4> {
 public:
  Path_OvalPointIterator(const Rect& oval, Path::Direction dir,
                         size_t startIndex)
      : Path_PointIterator<4>(dir, startIndex) {
    const float cx = oval.CenterX();
    const float cy = oval.CenterY();

    pts[0] = Vec2{cx, oval.Top()};
    pts[1] = Vec2{oval.Right(), cy};
    pts[2] = Vec2{cx, oval.Bottom()};
    pts[3] = Vec2{oval.Left(), cy};
  }
};

class Path_RRectPointIterator : public Path_PointIterator<8> {
 public:
  Path_RRectPointIterator(RRect const& rrect, Path::Direction dir,
                          uint32_t start_index)
      : Path_PointIterator(dir, start_index) {
    Rect const& bounds = rrect.GetBounds();
    float L = bounds.Left();
    float T = bounds.Top();
    float R = bounds.Right();
    float B = bounds.Bottom();

    pts[0] = Vec2{L + rrect.Radii(RRect::kUpperLeft).x, T};
    pts[1] = Vec2{R - rrect.Radii(RRect::kUpperRight).x, T};
    pts[2] = Vec2{R, T + rrect.Radii(RRect::kUpperRight).y};
    pts[3] = Vec2{R, B - rrect.Radii(RRect::kLowerRight).y};
    pts[4] = Vec2{R - rrect.Radii(RRect::kLowerRight).x, B};
    pts[5] = Vec2{L + rrect.Radii(RRect::kLowerLeft).x, B};
    pts[6] = Vec2{L, B - rrect.Radii(RRect::kLowerLeft).y};
    pts[7] = Vec2{L, T + rrect.Radii(RRect::kUpperLeft).y};
  }
};

namespace {

constexpr int PointsInVerb(Path::Verb verb) {
  switch (verb) {
    case Path::Verb::kMove:
    case Path::Verb::kLine:
      return 1;
    case Path::Verb::kQuad:
    case Path::Verb::kConic:
      return 2;
    case Path::Verb::kCubic:
      return 3;
    case Path::Verb::kClose:
    case Path::Verb::kDone:
      return 0;
  }
  return 0;
}

template <typename P>
P ReadPoint(const Vec2* point, const Vec2* begin, const Vec2* zw) {
  if constexpr (std::is_same_v<P, Vec2>) {
    return *point;
  } else {
    return {*point, zw ? zw[point - begin] : Vec2{0, 1}};
  }
}

template <typename P>
void CopySegment(P* out, const Vec2* point, int count, const Vec2* begin,
                 const Vec2* zw) {
  if constexpr (std::is_same_v<P, Vec2>) {
    std::copy_n(point, count, out);
  } else {
    for (int i = 0; i < count; ++i) {
      out[i] = ReadPoint<P>(point + i, begin, zw);
    }
  }
}

}  // namespace

Path::Iter::Iter()
    : pts_(nullptr),
      points_begin_(nullptr),
      zw_(nullptr),
      verbs_(nullptr),
      verb_stop_(nullptr),
      conic_weights_(nullptr),
      force_close_(false),
      need_close_(false),
      close_line_(false),
      move_to_(nullptr),
      last_pt_(nullptr),
      segment_state_(SegmentState::kEmptyContour) {}

Path::Iter::Iter(const Path& path, bool force_close) : Iter() {
  SetPath(path, force_close);
}

void Path::Iter::SetPath(const Path& path, bool force_close) {
  const auto& data = path.GetData();
  pts_ = points_begin_ = data.points_.data();
  zw_ = data.zw_.empty() ? nullptr : data.zw_.data();
  verbs_ = data.verbs_.data();
  verb_stop_ = path.VerbsEnd();
  conic_weights_ = data.conic_weights_.data();
  force_close_ = force_close;
  need_close_ = close_line_ = false;
  segment_state_ = SegmentState::kEmptyContour;
}

Path::Verb Path::Iter::Next(Vec2 pts[4]) { return NextImpl(pts); }
Path::Verb Path::Iter::Next(Point pts[4]) { return NextImpl(pts); }

template <typename P>
Path::Verb Path::Iter::NextImpl(P pts[4]) {
  if (verbs_ == verb_stop_) {
    if (need_close_ && segment_state_ == SegmentState::kAfterPrimitive) {
      if (AutoClose(pts) == Verb::kLine) {
        return Verb::kLine;
      }
      need_close_ = false;
      return Verb::kClose;
    }
    return Verb::kDone;
  }

  Verb verb = *verbs_++;
  switch (verb) {
    case Verb::kMove:
      if (need_close_) {
        --verbs_;
        verb = AutoClose(pts);
        if (verb == Verb::kClose) {
          need_close_ = false;
        }
        return verb;
      }
      if (verbs_ == verb_stop_) {
        return Verb::kDone;
      }
      move_to_ = last_pt_ = pts_++;
      pts[0] = ReadPoint<P>(move_to_, points_begin_, zw_);
      segment_state_ = SegmentState::kAfterMove;
      need_close_ = force_close_;
      break;
    case Verb::kConic:
      ++conic_weights_;
      [[fallthrough]];
    case Verb::kLine:
    case Verb::kQuad:
    case Verb::kCubic: {
      int count = PointsInVerb(verb);
      pts[0] = ReadPoint<P>(ConsMoveTo(), points_begin_, zw_);
      CopySegment(pts + 1, pts_, count, points_begin_, zw_);
      pts_ += count;
      last_pt_ = pts_ - 1;
      if (verb == Verb::kLine) {
        close_line_ = false;
      }
      break;
    }
    case Verb::kClose:
      verb = AutoClose(pts);
      if (verb == Verb::kLine) {
        --verbs_;
      } else {
        need_close_ = false;
        segment_state_ = SegmentState::kEmptyContour;
      }
      last_pt_ = move_to_;
      break;
    case Verb::kDone:
      break;
  }
  return verb;
}

float Path::Iter::ConicWeight() const { return conic_weights_[-1]; }

bool Path::Iter::IsClosedContour() const {
  if (verbs_ == verb_stop_) {
    return false;
  }
  if (force_close_) {
    return true;
  }
  auto verbs = verbs_;
  if (*verbs == Verb::kMove) {
    ++verbs;
  }
  while (verbs != verb_stop_ && *verbs != Verb::kMove) {
    if (*verbs++ == Verb::kClose) {
      return true;
    }
  }
  return false;
}

template <typename P>
Path::Verb Path::Iter::AutoClose(P* pts) {
  if (*last_pt_ != *move_to_ ||
      (zw_ && zw_[last_pt_ - points_begin_] != zw_[move_to_ - points_begin_])) {
    if (FloatIsNan(last_pt_->x) || FloatIsNan(last_pt_->y) ||
        FloatIsNan(move_to_->x) || FloatIsNan(move_to_->y)) {
      return Verb::kClose;
    }
    pts[0] = ReadPoint<P>(last_pt_, points_begin_, zw_);
    pts[1] = ReadPoint<P>(move_to_, points_begin_, zw_);
    last_pt_ = move_to_;
    close_line_ = true;
    return Verb::kLine;
  }
  if constexpr (std::is_same_v<P, Point>) {
    pts[0] = ReadPoint<P>(move_to_, points_begin_, zw_);
  }
  return Verb::kClose;
}

const Vec2* Path::Iter::ConsMoveTo() {
  if (segment_state_ == SegmentState::kAfterMove) {
    segment_state_ = SegmentState::kAfterPrimitive;
    return move_to_;
  }
  return pts_ - 1;
}

bool Path::Iter::IsCloseLine() const { return close_line_; }

Path::RawIter::RawIter()
    : pts_(nullptr),
      points_begin_(nullptr),
      zw_(nullptr),
      verbs_(nullptr),
      verb_stop_(nullptr),
      conic_weights_(nullptr) {}

void Path::RawIter::SetPath(const Path& path) {
  const auto& data = path.GetData();
  pts_ = points_begin_ = data.points_.data();
  zw_ = data.zw_.empty() ? nullptr : data.zw_.data();
  verbs_ = data.verbs_.data();
  verb_stop_ = path.IsFinite() ? path.VerbsEnd() : verbs_;
  conic_weights_ = data.conic_weights_.data();
}

Path::Verb Path::RawIter::Next(Vec2 pts[4]) { return NextImpl(pts); }
Path::Verb Path::RawIter::Next(Point pts[4]) { return NextImpl(pts); }

template <typename P>
Path::Verb Path::RawIter::NextImpl(P pts[4]) {
  if (verbs_ == verb_stop_) {
    return Verb::kDone;
  }
  const auto verb = *verbs_++;
  const int count = PointsInVerb(verb);
  if (count) {
    const int back = verb == Verb::kMove ? 0 : 1;
    CopySegment(pts, pts_ - back, count + back, points_begin_, zw_);
    pts_ += count;
  }
  if (verb == Verb::kConic) {
    ++conic_weights_;
  }
  return verb;
}

Path::Verb Path::RawIter::Peek() const {
  return verbs_ != verb_stop_ ? *verbs_ : Verb::kDone;
}

float Path::RawIter::ConicWeight() const { return conic_weights_[-1]; }

PathBuilder& PathBuilder::MoveTo(float x, float y) {
  auto& data = Edit();
  if (!data.verbs_.empty() && data.verbs_.back() == Verb::kMove) {
    DEBUG_CHECK(!data.points_.empty());
    data.SetPoint2D(data.points_.size() - 1, {x, y});
  } else {
    data.last_move_to_index_ = static_cast<int32_t>(data.points_.size());
    data.verbs_.push_back(Verb::kMove);
    data.AddPoint({x, y});
    data.type_ = IsAType::kGeneral;
  }
  data.MarkBoundsDirty();
  return *this;
}

PathBuilder& PathBuilder::LineTo(float x, float y) {
  InjectMoveToIfNeed();
  auto& data = *data_;

  data.verbs_.push_back(Verb::kLine);
  data.AddPoint({x, y});
  data.segment_masks_ |= SegmentMask::kLine;
  data.type_ = IsAType::kGeneral;
  data.MarkBoundsDirty();

  return *this;
}

PathBuilder& PathBuilder::QuadTo(float x1, float y1, float x2, float y2) {
  InjectMoveToIfNeed();
  auto& data = *data_;

  data.verbs_.push_back(Verb::kQuad);
  const Vec2 points[] = {{x1, y1}, {x2, y2}};
  data.AddPoints(points);
  data.segment_masks_ |= SegmentMask::kQuad;
  data.type_ = IsAType::kGeneral;
  data.MarkBoundsDirty();

  return *this;
}

PathBuilder& PathBuilder::ConicTo(float x1, float y1, float x2, float y2,
                                  float weight) {
  if (!(weight > 0)) {
    this->LineTo(x2, y2);
  } else if (glm::isinf(weight)) {
    this->LineTo(x1, y1);
    this->LineTo(x2, y2);
  } else if (weight == Float1) {
    this->QuadTo(x1, y1, x2, y2);
  } else {
    InjectMoveToIfNeed();
    auto& data = *data_;

    data.verbs_.push_back(Verb::kConic);
    data.conic_weights_.push_back(weight);
    const Vec2 points[] = {{x1, y1}, {x2, y2}};
    data.AddPoints(points);
    data.segment_masks_ |= SegmentMask::kConic;
    data.type_ = IsAType::kGeneral;
    data.MarkBoundsDirty();
  }
  return *this;
}

PathBuilder& PathBuilder::CubicTo(float x1, float y1, float x2, float y2,
                                  float x3, float y3) {
  InjectMoveToIfNeed();
  auto& data = *data_;

  data.verbs_.push_back(Verb::kCubic);

  const Vec2 points[] = {{x1, y1}, {x2, y2}, {x3, y3}};
  data.AddPoints(points);
  data.segment_masks_ |= SegmentMask::kCubic;
  data.type_ = IsAType::kGeneral;
  data.MarkBoundsDirty();

  return *this;
}

PathBuilder& PathBuilder::ArcTo(float x1, float y1, float x2, float y2,
                                float radius) {
  if (radius == 0) {
    return LineTo(x1, y1);
  }

  Vec2 start;
  GetLastPt(std::addressof(start));
  // need to know prev point so we can construct tangent vectors
  glm::dvec4 befored, afterd;

  befored = glm::normalize(glm::dvec4{x1 - start.x, y1 - start.y, 0, 0});
  afterd = glm::normalize(glm::dvec4{x2 - x1, y2 - y1, 0, 0});

  double cosh = glm::dot(befored, afterd);
  double sinh = befored.x * afterd.y - befored.y * afterd.x;

  if (!PointIsFinite(befored) || !PointIsFinite(afterd) ||
      FloatNearlyZero(static_cast<float>(sinh))) {
    return LineTo(x1, y1);
  }

  Vec4 before =
      Vec4{static_cast<float>(befored.x), static_cast<float>(befored.y),
           static_cast<float>(befored.z), static_cast<float>(befored.w)};
  Vec4 after = Vec4{static_cast<float>(afterd.x), static_cast<float>(afterd.y),
                    static_cast<float>(afterd.z), static_cast<float>(afterd.w)};

  float dist = glm::abs(static_cast<float>(radius * (1 - cosh) / sinh));
  float xx = x1 - dist * before.x;
  float yy = y1 - dist * before.y;
  PointSetLength<false>(after, after.x, after.y, dist);

  LineTo(xx, yy);
  float weight = glm::sqrt(static_cast<float>(FloatHalf + cosh * 0.5));

  return ConicTo(x1, y1, x1 + after.x, y1 + after.y, weight);
}

PathBuilder& PathBuilder::ArcTo(Rect const& oval, float startAngle,
                                float sweepAngle, bool forceMove) {
  if (oval.Width() < 0 || oval.Height() < 0) {
    return *this;
  }

  if (CountVerbs() == 0) {
    forceMove = true;
  }

  Point long_pt;
  if (arc_is_long_point(oval, startAngle, sweepAngle, &long_pt)) {
    return forceMove ? this->MoveTo(Vec2{long_pt})
                     : this->LineTo(Vec2{long_pt});
  }

  Vec2 startV, stopV;
  RotationDirection dir;
  std::tie(startV, stopV, dir) = angles_to_unit_vectors(startAngle, sweepAngle);

  Point singlePt;

  // Adds a move-to to 'pt' if forceMoveTo is true. Otherwise a LineTo unless
  // we're sufficiently close to 'pt' currently. This prevents spurious LineTos
  // when adding a series of contiguous arcs from the same oval.
  auto addPt = [&forceMove, this](const Point& pt) {
    Vec2 lastPt;
    if (forceMove) {
      this->MoveTo(Vec2{pt});
    } else if (!this->GetLastPt(&lastPt) || !FloatNearlyZero(lastPt.x - pt.x) ||
               !FloatNearlyZero(lastPt.y - pt.y)) {
      this->LineTo(Vec2{pt});
    }
  };

  // At this point, we know that the arc is not a lone point, but startV ==
  // stopV indicates that the sweepAngle is too small such that
  // angles_to_unit_vectors cannot handle it.
  if (startV == stopV) {
    float endAngle = glm::radians(startAngle + sweepAngle);
    float radiusX = oval.Width() / 2;
    float radiusY = oval.Height() / 2;
    // We do not use SkScalar[Sin|Cos]SnapToZero here. When sin(startAngle) is 0
    // and sweepAngle is very small and radius is huge, the expected behavior
    // here is to draw a line. But calling SkScalarSinSnapToZero will make
    // sin(endAngle) be 0 which will then draw a dot.
    PointSet(singlePt, oval.CenterX() + radiusX * glm::cos(endAngle),
             oval.CenterY() + radiusY * glm::sin(endAngle));
    addPt(singlePt);
    return *this;
  }

  Conic conics[Conic::kMaxConicsForArc];
  int count = build_arc_conics(oval, startV, stopV, dir, conics, &singlePt);
  if (count) {
    const Point& pt = conics[0].pts[0];
    addPt(pt);
    for (int i = 0; i < count; ++i) {
      this->ConicTo(Vec2{conics[i].pts[1]}, Vec2{conics[i].pts[2]},
                    conics[i].w);
    }
  } else {
    addPt(singlePt);
  }
  return *this;
}

PathBuilder& PathBuilder::ArcTo(float rx, float ry, float xAxisRotate,
                                ArcSize largeArc, Direction sweep, float x,
                                float y) {
  // convert degree to angle
  xAxisRotate = glm::radians(xAxisRotate);
  this->InjectMoveToIfNeed();
  std::array<Point, 2> src_pts{};
  src_pts[0] = data_->Point4(data_->points_.size() - 1);
  // If rx = 0 or ry = 0 then this arc is treated as a straight line segment
  // joining the endpoints.
  // http://www.w3.org/TR/SVG/implnote.html#ArcOutOfRangeParameters
  if (!rx || !ry) {
    return this->LineTo(x, y);
  }
  // If the current point and target point for the arc are identical, it should
  // be treated as a zero length path. This ensures continuity in animations.
  src_pts[1].x = x;
  src_pts[1].y = y;
  src_pts[1].z = 0;
  src_pts[1].w = 1;

  if (src_pts[0] == src_pts[1]) {
    return this->LineTo(x, y);
  }

  rx = std::abs(rx);
  ry = std::abs(ry);
  Vector mid_point_distance = (src_pts[0] - src_pts[1]) * 0.5f;

  Matrix point_transform = Matrix::RotateRad(-xAxisRotate);

  Point transformed_mid_point = point_transform * mid_point_distance;
  float square_rx = rx * rx;
  float square_ry = ry * ry;
  float square_x = transformed_mid_point.x * transformed_mid_point.x;
  float square_y = transformed_mid_point.y * transformed_mid_point.y;

  // Check if the radii are big enough to draw the arc, scale radii if not.
  //  http://www.w3.org/TR/SVG/implnote.html#ArcCorrectionOutOfRangeRadii
  float radii_scale = square_x / square_rx + square_y / square_ry;
  if (radii_scale > 1.f) {
    radii_scale = std::sqrt(radii_scale);
    rx *= radii_scale;
    ry *= radii_scale;
  }

  point_transform = Matrix::Scale(1.f / rx, 1.f / ry);

  point_transform = point_transform * Matrix::RotateRad(-xAxisRotate);

  std::array<Point, 2> unit_pts{};
  unit_pts[0] = point_transform * src_pts[0];
  unit_pts[1] = point_transform * src_pts[1];

  Vector delta = unit_pts[1] - unit_pts[0];
  float d = delta.x * delta.x + delta.y * delta.y;
  float scale_factor_squared = std::max(1.f / d - 0.25f, 0.f);
  float scale_factor = std::sqrt(scale_factor_squared);

  if ((sweep == Direction::kCCW) != static_cast<bool>(largeArc)) {
    scale_factor = -scale_factor;
  }

  PointScale(delta, scale_factor, &delta);
  Point center_point = (unit_pts[0] + unit_pts[1]) * 0.5f;
  center_point.x -= delta.y;
  center_point.y += delta.x;
  unit_pts[0] -= center_point;
  unit_pts[1] -= center_point;
  float theta1 = std::atan2(unit_pts[0].y, unit_pts[0].x);
  float theta2 = std::atan2(unit_pts[1].y, unit_pts[1].x);
  float theta_arc = theta2 - theta1;
  if (theta_arc < 0 && (sweep == Direction::kCW)) {
    // sweep flipped from the original implementation
    theta_arc += glm::pi<float>() * 2.f;
  } else if (theta_arc > 0 && (sweep != Direction::kCW)) {
    theta_arc -= glm::pi<float>() * 2.f;
  }

  // Very tiny angles cause our subsequent math to go wonky (skbug.com/9272)
  // so we do a quick check here. The precise tolerance amount is just made up.
  // PI/million happens to fix the bug in 9272, but a larger value is probably
  // ok too.
  if (std::abs(theta_arc) < (glm::pi<float>() / (1000.f * 1000.f))) {
    return this->LineTo(x, y);
  }

  point_transform = Matrix::RotateRad(xAxisRotate);
  point_transform = point_transform * Matrix::Scale(rx, ry);

  // the arc may be slightly bigger than 1/4 circle, so allow up to 1/3rd
  int segments =
      std::ceil(std::abs(theta_arc / (2.f * glm::pi<float>() / 3.f)));
  float theta_width = theta_arc / segments;
  float t = std::tan(theta_width * 0.5f);

  if (!FloatIsFinite(t)) {
    return *this;
  }

  float start_theta = theta1;
  float w = std::sqrt(FloatHalf + std::cos(theta_width) * FloatHalf);
  auto float_is_integer = [](float scalar) -> bool {
    return scalar == std::floor(scalar);
  };

  bool expect_integers =
      FloatNearlyZero(glm::pi<float>() / 2.f - std::abs(theta_width)) &&
      float_is_integer(rx) && float_is_integer(ry) && float_is_integer(x) &&
      float_is_integer(y);

  for (int i = 0; i < segments; i++) {
    float end_theta = start_theta + theta_width;
    float sin_end_theta = FloatSinSnapToZero(end_theta);
    float cos_end_theta = FloatCosSnapToZero(end_theta);

    PointSet(unit_pts[1], cos_end_theta, sin_end_theta);
    unit_pts[1] += center_point;
    unit_pts[1].w = 1;  // FIXME

    unit_pts[0] = unit_pts[1];
    unit_pts[0].x += t * sin_end_theta;
    unit_pts[0].y += -t * cos_end_theta;

    std::array<Point, 2> mapped;
    mapped[0] = point_transform * unit_pts[0];
    mapped[1] = point_transform * unit_pts[1];

    if (expect_integers) {
      for (auto& point : mapped) {
        point.x = std::round(point.x);
        point.y = std::round(point.y);
      }
    }

    this->ConicTo(Vec2{mapped[0]}, Vec2{mapped[1]}, w);
    start_theta = end_theta;
  }

  return *this;
}

PathBuilder& PathBuilder::AddRect(Rect const& rect, Direction dir,
                                  uint32_t start) {
  auto& data = Edit();
  bool was_empty = data.segment_masks_ == 0;
  data.first_direction_ = was_empty ? dir : Direction::kUnknown;

  Path_RectPointIterator iter{rect, dir, start};

  this->MoveTo(iter.current());
  this->LineTo(iter.next());
  this->LineTo(iter.next());
  this->LineTo(iter.next());
  this->Close();
  if (was_empty) {
    data.type_ = IsAType::kRect;
  }

  return *this;
}

PathBuilder& PathBuilder::Close() {
  if (!IsEmpty() && data_->verbs_.back() != Verb::kClose) {
    data_->verbs_.push_back(Verb::kClose);
    data_->last_move_to_index_ = ~data_->last_move_to_index_;
    data_->MarkBoundsDirty();
  }
  return *this;
}

PathBuilder& PathBuilder::AddRoundRect(Rect const& rect, float rx, float ry,
                                       Direction dir) {
  if (rx < 0 || ry < 0) {
    return *this;
  }

  RRect rrect;
  rrect.SetRectXY(rect, rx, ry);

  return this->AddRRect(rrect, dir);
}

PathBuilder& PathBuilder::AddRRect(RRect const& rrect, Direction dir) {
  // legacy start indices: 6 (CW) and 7(CCW)
  return this->AddRRect(rrect, dir, dir == Direction::kCW ? 6 : 7);
}

PathBuilder& PathBuilder::AddRRect(RRect const& rrect, Direction dir,
                                   uint32_t start_index) {
  Rect const& bounds = rrect.GetBounds();

  if (rrect.IsRect() || rrect.IsEmpty()) {
    // degenerate(rect) => radii points are collapsing
    this->AddRect(bounds, dir, (start_index + 1) / 2);
  } else if (rrect.IsOval()) {
    // degenerate(oval) => line points are collapsing
    this->AddOval(bounds, dir, start_index / 2);
  } else {
    auto& data = Edit();
    bool was_empty = data.segment_masks_ == 0;
    data.first_direction_ = was_empty ? dir : Direction::kUnknown;

    // we start with a conic on odd indices when moving CW vs.
    // even indices when moving CCW
    bool starts_with_conic = ((start_index & 1) == (dir == Direction::kCW));
    float weight = FloatRoot2Over2;

    Path_RRectPointIterator rrect_iter{rrect, dir, start_index};
    // Corner iterator indices follow the collapsed radii model,
    // adjusted such that the start pt is "behind" the radii start pt.
    uint32_t rect_start_index =
        start_index / 2 + (dir == Direction::kCW ? 0 : 1);
    Path_RectPointIterator rect_iter{bounds, dir, rect_start_index};

    this->MoveTo(rrect_iter.current());
    if (starts_with_conic) {
      for (uint32_t i = 0; i < 3; i++) {
        this->ConicTo(rect_iter.next(), rrect_iter.next(), weight);
        this->LineTo(rrect_iter.next());
      }
      this->ConicTo(rect_iter.next(), rrect_iter.next(), weight);
      // final LineTo handled by close().
    } else {
      for (uint32_t i = 0; i < 4; i++) {
        this->LineTo(rrect_iter.next());
        this->ConicTo(rect_iter.next(), rrect_iter.next(), weight);
      }
    }

    this->Close();

    if (was_empty && rrect.IsSimple()) {
      data.type_ = IsAType::kSimpleRRect;
      data.radii_ = rrect.GetSimpleRadii();
    }
  }

  return *this;
}

PathBuilder& PathBuilder::AddCircle(float x, float y, float radius,
                                    Direction dir) {
  if (radius > 0) {
    AddOval(Rect::MakeLTRB(x - radius, y - radius, x + radius, y + radius),
            dir);
  }

  return *this;
}

PathBuilder& PathBuilder::AddOval(const Rect& oval, Direction dir) {
  return AddOval(oval, dir, 1);
}

PathBuilder& PathBuilder::AddOval(const Rect& oval, Direction dir,
                                  uint32_t start) {
  auto& data = Edit();
  bool is_oval = data.segment_masks_ == 0;
  data.first_direction_ = is_oval ? dir : Direction::kUnknown;

  Path_OvalPointIterator oval_iter{oval, dir, start};
  Path_RectPointIterator rect_iter{oval, dir,
                                   start + (dir == Direction::kCW ? 0 : 1)};

  float weight = FloatRoot2Over2;

  MoveTo(oval_iter.current());
  for (uint32_t i = 0; i < 4; i++) {
    ConicTo(rect_iter.next(), oval_iter.next(), weight);
  }
  Close();
  if (is_oval) {
    data.type_ = IsAType::kOval;
  }
  return *this;
}

Path::Path(StorageRef data, PathFillType fill_type,
           ConvexityType convexity_hint)
    : data_(std::move(data)),
      fill_type_(fill_type),
      convexity_hint_(convexity_hint) {}

const Path::Data& Path::GetData() const {
  static const Data empty;
  return data_ ? *data_ : empty;
}

size_t Path::CountPoints() const { return GetData().points_.size(); }
size_t Path::CountVerbs() const { return GetData().verbs_.size(); }
size_t Path::CountConicWeights() const {
  return GetData().conic_weights_.size();
}
const Vec2* Path::Points() const { return GetData().points_.data(); }
const Path::Verb* Path::VerbsBegin() const { return GetData().verbs_.data(); }
const Path::Verb* Path::VerbsEnd() const {
  return data_ ? data_->verbs_.data() + data_->verbs_.size() : VerbsBegin();
}
const float* Path::ConicWeights() const {
  return GetData().conic_weights_.data();
}
Rect Path::GetBounds() const { return GetData().GetBounds().rect; }
bool Path::IsFinite() const { return GetData().GetBounds().finite; }
Path::IsAType Path::GetIsAType() const { return GetData().type_; }
uint32_t Path::GetSegmentMasks() const { return GetData().segment_masks_; }

Path Path::WithFillType(PathFillType type) const {
  return Path(data_, type, convexity_hint_);
}

Path::ConvexityType Path::GetConvexityType() const {
  if (convexity_hint_ != ConvexityType::kUnknown) {
    return convexity_hint_;
  }
  const auto& data = GetData();
  auto cached = data.convexity_cache_.load(std::memory_order_relaxed);
  if (!cached) {
    Direction direction = data.first_direction_;
    ConvexityType type = ComputeConvexity(&direction);
    cached =
        static_cast<uint8_t>(type) | (static_cast<uint8_t>(direction) << 2);
    data.convexity_cache_.store(cached, std::memory_order_relaxed);
  }
  return static_cast<ConvexityType>(cached & 3);
}

Path::Direction Path::GetFirstDirection() const {
  const auto& data = GetData();
  auto cached = data.convexity_cache_.load(std::memory_order_relaxed);
  return cached && convexity_hint_ == ConvexityType::kUnknown
             ? static_cast<Direction>(cached >> 2)
             : data.first_direction_;
}

Point Path::GetPoint(int index) const {
  return index >= 0 && static_cast<size_t>(index) < CountPoints()
             ? data_->Point4(index)
             : Point{0, 0, 0, 1};
}

bool Path::GetLastPt(Point* last_pt) const {
  const size_t count = CountPoints();
  if (last_pt) {
    *last_pt = count ? data_->Point4(count - 1) : Point{0, 0, 0, 1};
  }
  return count != 0;
}

Path::Verb Path::GetVerb(int index) const {
  return index >= 0 && static_cast<size_t>(index) < CountVerbs()
             ? data_->verbs_[index]
             : Verb::kDone;
}

Point Path::GetLastMovePt() const {
  const int32_t index = GetData().last_move_to_index_;
  return GetPoint(index < 0 ? ~index : index);
}

Vec2 Path::GetPoint2D(int index) const {
  return index >= 0 && static_cast<size_t>(index) < CountPoints()
             ? data_->points_[index]
             : Vec2{};
}

bool Path::GetLastPt2D(Vec2* last_pt) const {
  const bool has_point = !IsEmpty();
  if (last_pt) {
    *last_pt = has_point ? data_->points_.back() : Vec2{};
  }
  return has_point;
}

Vec2 Path::GetLastMovePt2D() const {
  const int32_t index = GetData().last_move_to_index_;
  return GetPoint2D(index < 0 ? ~index : index);
}

size_t Path::CopyPoints2D(Vec2* dst, size_t capacity) const {
  if (!dst) {
    return CountPoints();
  }
  const size_t count = std::min(capacity, CountPoints());
  if (count) {
    std::copy_n(data_->points_.data(), count, dst);
  }
  return count;
}

bool Path::IsLine(Point* line) const {
  if (!IsLine2D(nullptr)) {
    return false;
  }
  if (line) {
    line[0] = data_->Point4(0);
    line[1] = data_->Point4(1);
  }
  return true;
}

bool Path::IsLine2D(Vec2* line) const {
  if (CountVerbs() != 2 || data_->verbs_[0] != Verb::kMove ||
      data_->verbs_[1] != Verb::kLine) {
    return false;
  }
  if (line) {
    std::copy_n(data_->points_.data(), 2, line);
  }
  return true;
}

bool Path::operator==(const Path& other) const {
  if (fill_type_ != other.fill_type_) {
    return false;
  }
  if (data_ == other.data_) {
    return true;
  }
  const auto& a = GetData();
  const auto& b = other.GetData();
  return a.last_move_to_index_ == b.last_move_to_index_ &&
         a.verbs_ == b.verbs_ && a.points_ == b.points_ &&
         a.conic_weights_ == b.conic_weights_ && a.zw_ == b.zw_;
}

void PathBuilder::ReverseAddPath(const Path& src) {
  const auto& data = src.GetData();
  size_t point = data.points_.size();
  size_t weight = data.conic_weights_.size();
  bool need_move = true;
  bool need_close = false;
  for (size_t i = data.verbs_.size(); i > 0; --i) {
    auto verb = data.verbs_[i - 1];
    if (need_move) {
      MoveTo(data.points_[--point]);
      need_move = false;
    }
    if (verb == Verb::kMove) {
      if (need_close) {
        Close();
        need_close = false;
      }
      need_move = true;
      continue;
    }
    point -= PointsInVerb(verb);
    const auto* pts = data.points_.data() + point;
    switch (verb) {
      case Verb::kLine:
        LineTo(pts[0]);
        break;
      case Verb::kQuad:
        QuadTo(pts[1], pts[0]);
        break;
      case Verb::kConic:
        ConicTo(pts[1], pts[0], data.conic_weights_[--weight]);
        break;
      case Verb::kCubic:
        CubicTo(pts[2], pts[1], pts[0]);
        break;
      case Verb::kClose:
        need_close = true;
        break;
      default:
        break;
    }
  }
}

void PathBuilder::ReversePathTo(const PathBuilder& src) {
  if (src.IsEmpty()) {
    return;
  }
  const auto& data = *src.data_;
  size_t point = data.points_.size() - 1;
  size_t weight = data.conic_weights_.size();
  for (size_t i = data.verbs_.size(); i > 0; --i) {
    auto verb = data.verbs_[i - 1];
    if (verb == Verb::kMove) {
      return;
    }
    point -= PointsInVerb(verb);
    const auto* pts = data.points_.data() + point;
    switch (verb) {
      case Verb::kLine:
        LineTo(pts[0]);
        break;
      case Verb::kQuad:
        QuadTo(pts[1], pts[0]);
        break;
      case Verb::kConic:
        ConicTo(pts[1], pts[0], data.conic_weights_[--weight]);
        break;
      case Verb::kCubic:
        CubicTo(pts[2], pts[1], pts[0]);
        break;
      default:
        break;
    }
  }
}

PathBuilder& PathBuilder::AddPath(const Path& src, float dx, float dy,
                                  AddMode mode) {
  return AddPath(src, Matrix::Translate(dx, dy), mode);
}

PathBuilder& PathBuilder::AddPath(const Path& src, AddMode mode) {
  return AddPath(src, Matrix{}, mode);
}

PathBuilder& PathBuilder::AddPath(const Path& src, const Matrix& matrix,
                                  AddMode mode) {
  if (src.IsEmpty()) {
    return *this;
  }

  if (mode == AddMode::kAppend) {
    auto& data = Edit();
    const auto& source = src.GetData();
    const size_t offset = data.points_.size();
    const auto last_move = source.last_move_to_index_;
    data.last_move_to_index_ = last_move >= 0
                                   ? last_move + static_cast<int32_t>(offset)
                                   : last_move - static_cast<int32_t>(offset);

    data.verbs_.Append(source.verbs_);
    data.conic_weights_.Append(source.conic_weights_);
    data.points_.Append(source.points_);
    if (!data.zw_.empty()) {
      data.zw_.resize(data.points_.size(), Path::Data::kDefaultZW);
    }
    if (!matrix.IsIdentity() || !src.IsFinite()) {
      for (size_t i = 0; i < source.points_.size(); ++i) {
        data.SetPoint(offset + i, matrix * source.Point4(i));
      }
    } else if (!source.zw_.empty()) {
      if (data.zw_.empty()) {
        data.zw_.resize(data.points_.size(), Path::Data::kDefaultZW);
      }
      std::copy(source.zw_.begin(), source.zw_.end(),
                data.zw_.begin() + offset);
    }
    data.segment_masks_ |= source.segment_masks_;
    data.type_ = IsAType::kGeneral;
    data.MarkBoundsDirty();
    return *this;
  }

  Path::RawIter iter{src};
  Point pts[4];
  Verb verb;
  bool first_verb = true;
  while ((verb = iter.Next(pts)) != Verb::kDone) {
    switch (verb) {
      case Verb::kMove:
        pts[0] = matrix * pts[0];
        if (first_verb && !IsEmpty()) {
          InjectMoveToIfNeed();
          if (data_->Point4(data_->points_.size() - 1) != pts[0]) {
            LineTo(Vec2{pts[0]});
          }
        } else {
          MoveTo(Vec2{pts[0]});
        }
        break;
      case Verb::kLine:
        LineTo(Vec2{matrix * pts[1]});
        break;
      case Verb::kQuad:
        QuadTo(Vec2{matrix * pts[1]}, Vec2{matrix * pts[2]});
        break;
      case Verb::kConic:
        ConicTo(Vec2{matrix * pts[1]}, Vec2{matrix * pts[2]},
                iter.ConicWeight());
        break;
      case Verb::kCubic:
        CubicTo(Vec2{matrix * pts[1]}, Vec2{matrix * pts[2]},
                Vec2{matrix * pts[3]});
        break;
      case Verb::kClose:
        Close();
        break;
      case Verb::kDone:
        break;
    }
    first_verb = false;
  }
  return *this;
}

PathBuilder& PathBuilder::SetLastPt(float x, float y) {
  if (IsEmpty()) {
    return MoveTo(x, y);
  }
  data_->SetPoint2D(data_->points_.size() - 1, {x, y});
  data_->MarkBoundsDirty();
  data_->type_ = IsAType::kGeneral;
  return *this;
}

Path Path::CopyWithMatrix(const Matrix& matrix) const {
  PathBuilder builder(*this);
  builder.TransformPoints(matrix);
  builder.convexity_hint_ = convexity_hint_;
  return builder.Detach();
}

Path Path::CopyWithScale(float scale) const {
  PathBuilder builder(*this);
  builder.convexity_hint_ = convexity_hint_;
  if (builder.data_) {
    for (auto& point : builder.data_->points_) {
      point *= scale;
    }
    builder.data_->first_direction_ = GetFirstDirection();
    builder.data_->type_ = IsAType::kGeneral;
    builder.data_->MarkBoundsDirty();
  }
  return builder.Detach();
}

void PathBuilder::InjectMoveToIfNeed() {
  if (!data_ || data_->last_move_to_index_ < 0) {
    MoveTo(GetLastMovePt());
  }
}

bool PathBuilder::IsZeroLengthSincePoint(int start_index) const {
  if (!data_ || data_->points_.size() <= static_cast<size_t>(start_index) + 1) {
    return true;
  }
  const auto point = data_->Point4(start_index);
  for (size_t i = start_index + 1; i < data_->points_.size(); ++i) {
    if (point != data_->Point4(i)) {
      return false;
    }
  }
  return true;
}

Path::Data::Bounds Path::Data::GetBounds() const {
  if (bounds_state_.load(std::memory_order_acquire) == kReady) {
    return bounds_;
  }

  // Compute outside the publication window so readers never wait for a scan.
  const auto computed = ComputeBounds();
  auto expected = kDirty;
  if (bounds_state_.compare_exchange_strong(expected, kPublishing,
                                            std::memory_order_relaxed)) {
    bounds_ = computed;
    bounds_state_.store(kReady, std::memory_order_release);
  }
  return computed;
}

bool PathPriv::IsPlanar(const Path& path) { return path.GetData().zw_.empty(); }

bool PathPriv::IsPlanarTransform(const Matrix& matrix) {
  return matrix[0].z == 0 && matrix[0].w == 0 && matrix[1].z == 0 &&
         matrix[1].w == 0 && matrix[2] == Vec4{0, 0, 1, 0} &&
         matrix[3].z == 0 && matrix[3].w == 1;
}

void PathPriv::TransformPoints2D(const Matrix& matrix, const Vec2* src,
                                 Vec2* dst, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    dst[i] = Vec2{matrix * Point{src[i], 0.f, 1.f}};
  }
}

Rect PathPriv::ComputeBounds(const Vec2* points, size_t count, bool skip_last,
                             bool* finite) {
  *finite = true;
  if (!count) {
    return {};
  }
  const auto is_finite = [](Vec2 pt) {
    return std::isfinite(pt.x) && std::isfinite(pt.y);
  };
  if (skip_last && !is_finite(points[--count])) {
    *finite = false;
    return {};
  }
  Vec2 min = points[0], max = min;
  for (size_t i = 0; i < count; ++i) {
    Vec2 point = points[i];
    if (!is_finite(point)) {
      *finite = false;
      return {};
    }
    min = Vec2::Min(min, point);
    max = Vec2::Max(max, point);
  }
  return {min.x, min.y, max.x, max.y};
}

Path::Data::Bounds Path::Data::ComputeBounds() const {
  if (!std::all_of(zw_.begin(), zw_.end(), [](Vec2 zw) {
        return std::isfinite(zw.x) && std::isfinite(zw.y);
      })) {
    return {Rect{}, false};
  }
  Bounds result;
  result.rect = PathPriv::ComputeBounds(
      points_.data(), points_.size(),
      verbs_.size() > 1 && verbs_.back() == Verb::kMove, &result.finite);
  return result;
}

static void append_params(std::ostream& os, const std::string& label,
                          const Vec2 pts[], int count,
                          float conicWeight = -12345) {
  os << label << "(";
  for (int i = 0; i < count; i++) {
    const Vec2* point = pts + i;
    float x = point->x;
    float y = point->y;
    std::stringstream ss;
    ss << "( " << x << ", " << y << ")";
    os << ss.str();
  }
  if (conicWeight != -12345) {
    os << ", " << conicWeight;
  }
  os << ");";
  os << std::endl;
}

bool Path::IsRect(Rect* rect, bool* is_closed, Direction* direction) const {
  const auto& data = GetData();
  if (data.segment_masks_ != SegmentMask::kLine) {
    return false;
  }

  int32_t verb_cnt = CountVerbs();

  int32_t corners = 0;
  int32_t curr_verb = 0;
  const Vec2* first_pt = nullptr;
  const Vec2* last_pt = nullptr;
  Point first_corner;
  Point third_corner;
  const Vec2* pts = data.points_.data();
  const auto read = [&](const Vec2* point) {
    return data.Point4(point - data.points_.data());
  };
  Point line_start;
  Point close_xy;

  bool closed_or_moved = false;
  bool auto_close = false;
  // -1 to 3, -1 is uninitialized
  std::array<int32_t, 5> directions{-1, -1, -1, -1, -1};

  while (curr_verb < verb_cnt && (!auto_close)) {
    auto verb = data.verbs_[curr_verb];

    switch (verb) {
      case Verb::kClose:
        auto_close = true;
        [[fallthrough]];
      case Verb::kLine: {
        if (verb != Verb::kClose) {
          last_pt = pts;
        }
        Point line_end = verb == Verb::kClose ? read(first_pt) : read(pts++);
        Vec2 line_delta = Vec2{line_end - line_start};
        if (!FloatNearlyZero(line_delta.x) && !FloatNearlyZero(line_delta.y)) {
          return false;  // not a straight line
        }
        if (!FloatIsFinite(line_delta.x) || !FloatIsFinite(line_delta.y)) {
          return false;  // contains NaN number
        }

        if (line_start == line_end) {
          break;  // single point on side OK
        }

        auto next_direction = PathPriv::RectMakeDir(line_delta.x, line_delta.y);

        if (0 == corners) {
          directions[0] = next_direction;
          corners = 1;
          closed_or_moved = false;
          line_start = line_end;
          break;
        }
        if (closed_or_moved) {
          return false;  // closed followed by a line
        }

        if (auto_close && next_direction == directions[0]) {
          break;  // colinear with first
        }

        closed_or_moved = auto_close;
        if (directions[corners - 1] == next_direction) {
          if (3 == corners && verb == Verb::kLine) {
            third_corner = line_end;
          }
          line_start = line_end;
          break;  // colinear segment
        }

        directions[corners++] = next_direction;

        // opposite lines must point in opposite directions;
        // xoring them should equal 2
        switch (corners) {
          case 2:
            first_corner = line_start;
            break;
          case 3:
            if ((directions[0] ^ directions[2]) != 2) {
              return false;
            }
            third_corner = line_end;
            break;
          case 4:
            if ((directions[1] ^ directions[3]) != 2) {
              return false;
            }
            break;
          default:
            return false;  // too many direction changes
        }
        line_start = line_end;
      } break;
      case Verb::kQuad:
      case Verb::kConic:
      case Verb::kCubic:
        return false;
      case Verb::kMove:
        if (!corners) {
          first_pt = pts;
        } else {
          close_xy = read(first_pt) - read(last_pt);
          if (close_xy.x && close_xy.y) {
            return false;
          }
        }

        line_start = read(pts++);
        closed_or_moved = true;
        break;
      default:
        // unexpected verb
        return false;
    }
    curr_verb++;
  }

  // success if 4 corners and first point equals last
  if (corners < 3 || corners > 4) {
    return false;
  }

  // check if close generates diagonal
  close_xy = read(first_pt) - read(last_pt);
  if (close_xy.x && close_xy.y) {
    return false;
  }

  if (rect) {
    rect->Set(first_corner, third_corner);
  }

  if (is_closed) {
    *is_closed = auto_close;
  }

  if (direction) {
    *direction = directions[0] == ((directions[1] + 1) & 3) ? Direction::kCW
                                                            : Direction::kCCW;
  }

  return true;
}

bool Path::IsSimpleRRect(RRect* rrect) const {
  const auto& data = GetData();
  if (data.type_ != IsAType::kSimpleRRect) {
    return false;
  }
  if (rrect) {
    rrect->SetRectXY(GetBounds(), data.radii_.x, data.radii_.y);
  }
  return true;
}

bool Path::Contains(float x, float y) const {
  if (IsEmpty()) {
    return false;
  }

  auto rect = GetBounds();

  if (x < rect.Left() || x > rect.Right() || y < rect.Top() ||
      y > rect.Bottom()) {
    return false;
  }

  PathScanner scanner{x, y};

  scanner.VisitPath(*this, true);

  int32_t winding = scanner.GetWindingCount();
  int32_t on_curve_count = scanner.GetOnCurveCount();
  int32_t ray_intesects_vertex_count = scanner.GetRayIntersectsVertexCount();

  if (GetFillType() == PathFillType::kEvenOdd) {
    winding = (winding & 1);
  }

  if (winding != 0) {
    return true;
  }

  if ((on_curve_count & 1) || GetFillType() == PathFillType::kEvenOdd) {
    if ((on_curve_count & 1) != 0) {
      return true;
    }
  }

  if (GetFillType() == PathFillType::kEvenOdd &&
      scanner.GetWindingCount() > 0) {
    return (ray_intesects_vertex_count & 1) != 0;
  }

  return false;
}

void Path::Dump() const {
  Iter iter{*this, false};

  Vec2 pts[4];
  Verb verb;

  std::ostringstream res;

  while ((verb = iter.Next(pts)) != Verb::kDone) {
    switch (verb) {
      case Verb::kMove:
        append_params(res, "path.MoveTo", std::addressof(pts[0]), 1);
        break;
      case Verb::kLine:
        append_params(res, "path.LineTo", std::addressof(pts[1]), 1);
        break;
      case Verb::kQuad:
        append_params(res, "path.QuadTo", std::addressof(pts[1]), 2);
        break;
      case Verb::kConic:
        append_params(res, "path.ConicTo", std::addressof(pts[1]), 2,
                      iter.ConicWeight());
        break;
      case Verb::kCubic:
        append_params(res, "path.CubicTo", std::addressof(pts[1]), 3);
        break;
      case Verb::kClose:
        append_params(res, "path.close()", nullptr, 0);
        break;
      default:
        verb = Verb::kDone;
        break;
    }
  }
  LOGI("------ {}", res.str());
}

int Path::LeadingMoveToCount() const {
  const auto& verbs = GetData().verbs_;
  int count = verbs.size();
  for (int i = 0; i < count; i++) {
    if (verbs[i] != Verb::kMove) {
      return i;
    }
  }
  return count;
}

static int sign(float x) { return x < 0; }
#define kValueNeverReturnedBySign 2

enum class DirChange {
  kUnknown,
  kLeft,
  kRight,
  kStraight,
  kBackwards,  // if double back, allow simple lines to be convex
  kInvalid
};

// only valid for a single contour
struct Convexicator {
  /** The direction returned is only valid if the path is determined convex */
  Path::Direction GetFirstDirection() const { return first_direction_; }

  void SetMovePt(const Vec2& pt) {
    first_pt_ = last_pt_ = pt;
    expected_dir_ = DirChange::kInvalid;
  }

  bool AddPt(const Vec2& pt) {
    if (last_pt_ == pt) {
      return true;
    }
    // should only be true for first non-zero vector after setMovePt was called.
    if (first_pt_ == last_pt_ && expected_dir_ == DirChange::kInvalid) {
      last_vec_ = pt - last_pt_;
      first_vec_ = last_vec_;
    } else if (!this->AddVec(pt - last_pt_)) {
      return false;
    }
    last_pt_ = pt;
    return true;
  }

  static Path::ConvexityType BySign(const Vec2 points[], int count) {
    if (count <= 3) {
      // point, line, or triangle are always convex
      return Path::ConvexityType::kConvex;
    }

    const Vec2* last = points + count;
    Vec2 currPt = *points++;
    Vec2 firstPt = currPt;
    int dxes = 0;
    int dyes = 0;
    int lastSx = kValueNeverReturnedBySign;
    int lastSy = kValueNeverReturnedBySign;
    for (int outerLoop = 0; outerLoop < 2; ++outerLoop) {
      while (points != last) {
        Vec2 vec = *points - currPt;
        if (vec != Vec2{}) {
          // give up if vector construction failed
          if (!std::isfinite(vec.x) || !std::isfinite(vec.y)) {
            return Path::ConvexityType::kUnknown;
          }
          int sx = sign(vec.x);
          int sy = sign(vec.y);
          dxes += (sx != lastSx);
          dyes += (sy != lastSy);
          if (dxes > 3 || dyes > 3) {
            return Path::ConvexityType::kConcave;
          }
          lastSx = sx;
          lastSy = sy;
        }
        currPt = *points++;
        if (outerLoop) {
          break;
        }
      }
      points = &firstPt;
    }
    return Path::ConvexityType::kConvex;  // that is, it may be convex, don't
                                          // know yet
  }

  bool Close() {
    // If this was an explicit close, there was already a lineTo to fFirstPoint,
    // so this addPt() is a no-op. Otherwise, the addPt implicitly closes the
    // contour. In either case, we have to check the direction change along the
    // first vector in case it is concave.
    return this->AddPt(first_pt_) && this->AddVec(first_vec_);
  }

  bool IsFinite() const { return is_finite_; }

  int Reversals() const { return reversals_; }

 private:
  DirChange DirectionChange(const Vec2& cur_vec) {
    float cross = Vec2::Cross(last_vec_, cur_vec);
    if (glm::isinf(cross)) {
      return DirChange::kUnknown;
    }
    if (cross == 0) {
      return Vec2::Dot(last_vec_, cur_vec) < 0 ? DirChange::kBackwards
                                               : DirChange::kStraight;
    }
    return 1 == FloatSignAsInt(cross) ? DirChange::kRight : DirChange::kLeft;
  }

  bool AddVec(const Vec2& cur_vec) {
    DirChange dir = this->DirectionChange(cur_vec);
    switch (dir) {
      case DirChange::kLeft:  // fall through
      case DirChange::kRight:
        if (DirChange::kInvalid == expected_dir_) {
          expected_dir_ = dir;
          first_direction_ = (DirChange::kRight == dir) ? Path::Direction::kCW
                                                        : Path::Direction::kCCW;
        } else if (dir != expected_dir_) {
          first_direction_ = Path::Direction::kUnknown;
          return false;
        }
        last_vec_ = cur_vec;
        break;
      case DirChange::kStraight:
        break;
      case DirChange::kBackwards:
        //  allow path to reverse direction twice
        //    Given path.moveTo(0, 0); path.lineTo(1, 1);
        //    - 1st reversal: direction change formed by line (0,0 1,1), line
        //    (1,1 0,0)
        //    - 2nd reversal: direction change formed by line (1,1 0,0), line
        //    (0,0 1,1)
        last_vec_ = cur_vec;
        return ++reversals_ < 3;
      case DirChange::kUnknown:
        return (is_finite_ = false);
      case DirChange::kInvalid:
        break;
    }
    return true;
  }

  Vec2 first_pt_{0, 0};   // The first point of the contour, e.g. moveTo(x,y)
  Vec2 first_vec_{0, 0};  // The direction leaving first_pt_ to the next vertex

  Vec2 last_pt_{0, 0};   // The last point passed to addPt()
  Vec2 last_vec_{0, 0};  // The direction that brought the path to last_pt_

  DirChange expected_dir_{DirChange::kInvalid};
  Path::Direction first_direction_{Path::Direction::kUnknown};
  int reversals_{0};
  bool is_finite_{true};
};

Path::ConvexityType Path::ComputeConvexity(Direction* direction) const {
  const auto& data = GetData();
  int point_count = data.points_.size();
  int skip_count = LeadingMoveToCount() - 1;

  if (data.last_move_to_index_ >= 0) {
    if (data.last_move_to_index_ ==
        static_cast<int32_t>(data.points_.size() - 1)) {
      for (int i = data.verbs_.size() - 1; i >= 0; i--) {
        if (data.verbs_[i] == Verb::kMove) {
          point_count--;
        }
      }
    } else if (data.last_move_to_index_ != skip_count) {
      return Path::ConvexityType::kConcave;
    }
  }

  const Vec2* points = data.points_.data();
  if (skip_count > 0) {
    points += skip_count;
    point_count -= skip_count;
  }

  auto convexity = Convexicator::BySign(points, point_count);
  if (convexity == Path::ConvexityType::kConcave) {
    return convexity;
  }

  int contour_count = 0;
  bool needs_close = false;
  Convexicator state;

  Path::Iter iter{*this, false};
  std::array<Vec2, 4> pts = {};
  for (;;) {
    Path::Verb verb = iter.Next(pts.data());
    if (verb == Path::Verb::kDone) {
      break;
    }

    if (contour_count == 0) {
      if (verb == Path::Verb::kMove) {
        state.SetMovePt(pts[0]);
      } else {
        contour_count++;
        needs_close = true;
      }
    }

    if (contour_count == 1) {
      if (verb == Path::Verb::kMove || verb == Path::Verb::kClose) {
        if (!state.Close()) {
          return Path::ConvexityType::kConcave;
        }
        needs_close = false;
        contour_count++;
      } else {
        int count = PointsInVerb(verb);
        for (int i = 1; i <= count; i++) {
          if (!state.AddPt(pts[i])) {
            return Path::ConvexityType::kConcave;
          }
        }
      }
    } else {
      // The first contour has closed and anything other than spurious trailing
      // moves means there's multiple contours and the path can't be convex
      if (verb != Path::Verb::kMove) {
        return Path::ConvexityType::kConcave;
      }
    }
  }

  if (needs_close && !state.Close()) {
    return Path::ConvexityType::kConcave;
  }
  *direction = state.GetFirstDirection();
  return Path::ConvexityType::kConvex;
}

}  // namespace skity
