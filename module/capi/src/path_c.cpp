// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <skity/c/skity_path.h>

#include <algorithm>
#include <cstring>
#include <skity/geometry/matrix.hpp>
#include <skity/geometry/rect.hpp>
#include <skity/geometry/rrect.hpp>
#include <skity/graphic/path.hpp>

#include "path_handle.hpp"

struct skity_path_iter_s : skity_handle_base<skity::Path> {
  skity::Path snapshot;
  skity::Path::Iter iter;
};

struct skity_path_raw_iter_s : skity_handle_base<skity::Path> {
  skity::Path snapshot;
  skity::Path::RawIter iter;
};

namespace {

static_assert(sizeof(skity_vec2) == sizeof(skity::Vec2));
static_assert(alignof(skity_vec2) == alignof(skity::Vec2));

using skity::capi::edit_path_handle;
using skity::capi::path_matrix;
using skity::capi::read_path_handle;
using skity::capi::resolve_path;
using skity::capi::writable_path;

template <typename Append>
void append_path(skity_path dst, skity_path src, Append append) {
  const auto* source = read_path_handle(src);
  if (!source) return;
  skity::Path saved;
  if (dst == src) {
    saved = *source;
    source = &saved;
  }
  if (auto* builder = edit_path_handle(dst)) {
    append(*builder, *source);
  }
}

skity::RRect make_rrect(const skity_rect& rect, const float radii[8]) {
  skity::Vec2 corners[4];
  for (size_t i = 0; i < 4; ++i) {
    corners[i] = {radii[2 * i], radii[2 * i + 1]};
  }
  skity::RRect result;
  result.SetRectRadii({rect.left, rect.top, rect.right, rect.bottom}, corners);
  return result;
}

skity::Path::Direction to_dir(skity_path_direction d) {
  return static_cast<skity::Path::Direction>(d);
}

skity::Path::AddMode to_add_mode(skity_path_add_mode m) {
  return static_cast<skity::Path::AddMode>(m);
}

}  // namespace

extern "C" {

skity_path skity_path_create(void) {
  return skity::capi::alloc_handle<skity_path_s>(
      SKITY_OBJECT_TYPE_PATH, SKITY_HANDLE_OWNING,
      std::make_shared<skity::Path>());
}

void skity_path_destroy(skity_path path) {
  skity::capi::destroy_handle<skity_path_s>(path, SKITY_OBJECT_TYPE_PATH);
}

skity_path skity_path_clone(skity_path src) {
  auto* s = read_path_handle(src);
  if (s == nullptr) return nullptr;
  return skity::capi::alloc_handle<skity_path_s>(
      SKITY_OBJECT_TYPE_PATH, SKITY_HANDLE_OWNING,
      std::make_shared<skity::Path>(*s));
}

uint32_t skity_path_is_equal(skity_path a, skity_path b) {
  auto* pa = read_path_handle(a);
  auto* pb = read_path_handle(b);
  if (pa == nullptr || pb == nullptr) return 0u;
  return *pa == *pb ? 1u : 0u;
}

void skity_path_reset(skity_path path) {
  auto* handle = resolve_path(path);
  if (!handle || !handle->impl) return;
  if (handle->editing ||
      ((handle->header.flags & SKITY_HANDLE_OWNING) &&
       skity::PathPriv::HasExclusiveStorage(*handle->impl))) {
    skity::capi::edit_path(handle)->Reset();
  } else {
    *writable_path(handle) = {};
  }
}

void skity_path_set_fill_type(skity_path path, skity_path_fill_type type) {
  auto* handle = resolve_path(path);
  auto fill = static_cast<skity::Path::PathFillType>(type);
  if (handle && handle->editing) {
    handle->editor->SetFillType(fill);
  } else if (auto* value = writable_path(handle)) {
    *value = value->WithFillType(fill);
  }
}

void skity_path_move_to(skity_path path, float x, float y) {
  if (auto* p = edit_path_handle(path)) p->MoveTo(x, y);
}

void skity_path_line_to(skity_path path, float x, float y) {
  if (auto* p = edit_path_handle(path)) p->LineTo(x, y);
}

void skity_path_quad_to(skity_path path, float x1, float y1, float x2,
                        float y2) {
  if (auto* p = edit_path_handle(path)) p->QuadTo(x1, y1, x2, y2);
}

void skity_path_conic_to(skity_path path, float x1, float y1, float x2,
                         float y2, float weight) {
  if (auto* p = edit_path_handle(path)) p->ConicTo(x1, y1, x2, y2, weight);
}

void skity_path_cubic_to(skity_path path, float x1, float y1, float x2,
                         float y2, float x3, float y3) {
  if (auto* p = edit_path_handle(path)) p->CubicTo(x1, y1, x2, y2, x3, y3);
}

void skity_path_arc_to(skity_path path, float x1, float y1, float x2, float y2,
                       float radius) {
  if (auto* p = edit_path_handle(path)) p->ArcTo(x1, y1, x2, y2, radius);
}

void skity_path_close(skity_path path) {
  if (auto* p = edit_path_handle(path)) p->Close();
}

void skity_path_add_circle(skity_path path, float x, float y, float radius,
                           skity_path_direction dir) {
  if (auto* p = edit_path_handle(path)) p->AddCircle(x, y, radius, to_dir(dir));
}

void skity_path_add_oval(skity_path path, const skity_rect* oval,
                         skity_path_direction dir) {
  auto* p = edit_path_handle(path);
  if (p != nullptr && oval != nullptr) {
    p->AddOval(*reinterpret_cast<const skity::Rect*>(oval), to_dir(dir));
  }
}

void skity_path_add_rect(skity_path path, const skity_rect* rect,
                         skity_path_direction dir) {
  auto* p = edit_path_handle(path);
  if (p != nullptr && rect != nullptr) {
    p->AddRect(*reinterpret_cast<const skity::Rect*>(rect), to_dir(dir));
  }
}

void skity_path_add_round_rect(skity_path path, const skity_rect* rect,
                               float rx, float ry, skity_path_direction dir) {
  auto* p = edit_path_handle(path);
  if (p != nullptr && rect != nullptr) {
    p->AddRoundRect(*reinterpret_cast<const skity::Rect*>(rect), rx, ry,
                    to_dir(dir));
  }
}

void skity_path_transform(skity_path path, const skity_matrix* matrix) {
  if (!matrix) return;
  if (auto* builder = edit_path_handle(path)) {
    skity::PathPriv::TransformLegacy(*builder, path_matrix(*matrix));
  }
}

void skity_path_get_bounds(skity_path path, skity_rect* out_bounds) {
  auto* p = read_path_handle(path);
  if (p == nullptr || out_bounds == nullptr) return;
  skity::Rect b = p->GetBounds();
  out_bounds->left = b.Left();
  out_bounds->top = b.Top();
  out_bounds->right = b.Right();
  out_bounds->bottom = b.Bottom();
}

void skity_path_add_path(skity_path path, skity_path src, float dx, float dy) {
  append_path(path, src, [&](auto& builder, const auto& source) {
    builder.AddPath(source, dx, dy);
  });
}

void skity_path_add_rrect(skity_path path, const skity_rect* rect, float rx,
                          float ry, skity_path_direction dir) {
  auto* p = edit_path_handle(path);
  if (p != nullptr && rect != nullptr) {
    p->AddRoundRect(*reinterpret_cast<const skity::Rect*>(rect), rx, ry,
                    to_dir(dir));
  }
}

uint32_t skity_path_contains(skity_path path, float x, float y) {
  auto* p = read_path_handle(path);
  return (p != nullptr && p->Contains(x, y)) ? 1u : 0u;
}

skity_path_fill_type skity_path_get_fill_type(skity_path path) {
  auto* p = read_path_handle(path);
  return p ? static_cast<skity_path_fill_type>(p->GetFillType())
           : SKITY_PATH_FILL_TYPE_WINDING;
}

void skity_path_add_path_matrix(skity_path path, skity_path src,
                                const skity_matrix* matrix) {
  skity_path_add_path_matrix_with_mode(path, src, matrix,
                                       SKITY_PATH_ADD_MODE_APPEND);
}

void skity_path_add_path_with_mode(skity_path path, skity_path src,
                                   skity_path_add_mode mode) {
  append_path(path, src, [&](auto& builder, const auto& source) {
    builder.AddPath(source, to_add_mode(mode));
  });
}

void skity_path_add_path_matrix_with_mode(skity_path path, skity_path src,
                                          const skity_matrix* matrix,
                                          skity_path_add_mode mode) {
  if (!matrix) return;
  append_path(path, src, [&](auto& builder, const auto& source) {
    builder.AddPath(source, path_matrix(*matrix), to_add_mode(mode));
  });
}

void skity_path_reverse_add_path(skity_path path, skity_path src) {
  append_path(path, src, [](auto& builder, const auto& source) {
    skity::PathPriv::ReverseAddPath(builder, source);
  });
}

uint32_t skity_path_count_verbs(skity_path path) {
  auto* p = read_path_handle(path);
  return p ? (uint32_t)p->CountVerbs() : 0u;
}

skity_path_verb skity_path_get_verb(skity_path path, int32_t index) {
  auto* p = read_path_handle(path);
  if (p == nullptr || index < 0) {
    return SKITY_PATH_VERB_DONE;
  }
  // skity::Path::Verb and skity_path_verb are value-aligned enums; out of
  // range yields kDone on the C++ side as well.
  return static_cast<skity_path_verb>(p->GetVerb(index));
}

uint32_t skity_path_get_conic_weight(skity_path path, int32_t index,
                                     float* out) {
  auto* p = read_path_handle(path);
  if (p == nullptr || out == nullptr || index < 0) return 0u;
  if (static_cast<size_t>(index) >= p->CountConicWeights()) return 0u;
  *out = p->ConicWeights()[index];
  return 1u;
}

uint32_t skity_path_count_points(skity_path path) {
  auto* p = read_path_handle(path);
  return p ? (uint32_t)p->CountPoints() : 0u;
}

int32_t skity_path_get_verbs(skity_path path, skity_path_verb* out,
                             int32_t count) {
  auto* p = read_path_handle(path);
  if (p == nullptr) return 0;
  int32_t total = static_cast<int32_t>(p->CountVerbs());
  if (out == nullptr || count <= 0) return total;
  int32_t n = count < total ? count : total;
  // The public C enum and compact C++ enum use the same values.
  const skity::Path::Verb* verbs = p->VerbsBegin();
  for (int32_t i = 0; i < n; i++) {
    out[i] = static_cast<skity_path_verb>(verbs[i]);
  }
  return n;
}

int32_t skity_path_get_points(skity_path path, skity_point* out,
                              int32_t count) {
  auto* p = read_path_handle(path);
  if (p == nullptr) return 0;
  int32_t total = static_cast<int32_t>(p->CountPoints());
  if (out == nullptr || count <= 0) return total;
  int32_t n = count < total ? count : total;
  for (int32_t i = 0; i < n; ++i) {
    auto point = p->GetPoint(i);
    out[i] = {{{point.x, point.y, point.z, point.w}}};
  }
  return n;
}

int32_t skity_path_get_conic_weights(skity_path path, float* out,
                                     int32_t count) {
  auto* p = read_path_handle(path);
  if (p == nullptr) return 0;
  int32_t total = static_cast<int32_t>(p->CountConicWeights());
  if (out == nullptr || count <= 0) return total;
  int32_t n = count < total ? count : total;
  if (n) std::memcpy(out, p->ConicWeights(), sizeof(float) * (size_t)n);
  return n;
}

uint32_t skity_path_get_point(skity_path path, int32_t index, skity_vec4* out) {
  auto* p = read_path_handle(path);
  if (p == nullptr || out == nullptr || index < 0) return 0u;
  skity::Point pt = p->GetPoint(index);
  *reinterpret_cast<skity::Vec4*>(out) = pt;
  return 1u;
}

uint32_t skity_path_is_rect(skity_path path, skity_rect* out_rect,
                            uint32_t* out_is_closed) {
  auto* p = read_path_handle(path);
  if (p == nullptr) return 0u;
  skity::Rect r;
  bool is_closed = false;
  bool ok = p->IsRect(&r, &is_closed);
  if (!ok) return 0u;
  if (out_rect != nullptr) {
    out_rect->left = r.Left();
    out_rect->top = r.Top();
    out_rect->right = r.Right();
    out_rect->bottom = r.Bottom();
  }
  if (out_is_closed != nullptr) *out_is_closed = is_closed ? 1u : 0u;
  return 1u;
}

skity_convexity_type skity_path_get_convexity_type(skity_path path) {
  auto* p = read_path_handle(path);
  // skity::Path::ConvexityType and skity_convexity_type are value-aligned.
  return p ? static_cast<skity_convexity_type>(p->GetConvexityType())
           : SKITY_CONVEXITY_UNKNOWN;
}

void skity_path_set_convexity_type(skity_path path, skity_convexity_type type) {
  if (auto* p = edit_path_handle(path)) {
    skity::PathPriv::SetConvexityType(
        *p, static_cast<skity::Path::ConvexityType>(type));
  }
}

uint32_t skity_path_get_segment_masks(skity_path path) {
  auto* p = read_path_handle(path);
  return p ? p->GetSegmentMasks() : 0u;
}

uint32_t skity_path_is_finite(skity_path path) {
  auto* p = read_path_handle(path);
  return p && p->IsFinite() ? 1u : 0u;
}

uint32_t skity_path_is_empty(skity_path path) {
  auto* p = read_path_handle(path);
  return p && p->IsEmpty() ? 1u : 0u;
}

uint32_t skity_path_is_line(skity_path path, skity_vec4* out_pts) {
  auto* p = read_path_handle(path);
  if (p == nullptr) return 0u;
  skity::Point pts[2];
  if (!p->IsLine(pts)) return 0u;
  if (out_pts != nullptr) {
    reinterpret_cast<skity::Vec4*>(out_pts)[0] = pts[0];
    reinterpret_cast<skity::Vec4*>(out_pts)[1] = pts[1];
  }
  return 1u;
}

void skity_path_get_last_move_pt(skity_path path, skity_vec4* out) {
  auto* p = read_path_handle(path);
  if (p == nullptr || out == nullptr) return;
  *reinterpret_cast<skity::Vec4*>(out) = p->GetLastMovePt();
}

void skity_path_arc_to_oval(skity_path path, const skity_rect* oval,
                            float start_angle, float sweep_angle,
                            uint32_t force_move_to) {
  auto* p = edit_path_handle(path);
  if (p != nullptr && oval != nullptr) {
    p->ArcTo(*reinterpret_cast<const skity::Rect*>(oval), start_angle,
             sweep_angle, force_move_to != 0u);
  }
}

void skity_path_arc_to_svg(skity_path path, float rx, float ry,
                           float x_axis_rotate, skity_arc_size large_arc,
                           skity_path_direction sweep, float x, float y) {
  auto* p = edit_path_handle(path);
  if (p == nullptr) return;
  p->ArcTo(rx, ry, x_axis_rotate, static_cast<skity::Path::ArcSize>(large_arc),
           to_dir(sweep), x, y);
}

void skity_path_add_round_rect_radii(skity_path path, const skity_rect* rect,
                                     const float* radii,
                                     skity_path_direction dir) {
  auto* p = edit_path_handle(path);
  if (p != nullptr && rect != nullptr && radii != nullptr) {
    p->AddRRect(make_rrect(*rect, radii), to_dir(dir));
  }
}

void skity_path_add_round_rect_radii_start(skity_path path,
                                           const skity_rect* rect,
                                           const float* radii,
                                           skity_path_direction dir,
                                           int32_t start_index) {
  auto* p = edit_path_handle(path);
  if (p == nullptr || rect == nullptr || radii == nullptr || start_index < 0) {
    return;
  }
  p->AddRRect(make_rrect(*rect, radii), to_dir(dir),
              static_cast<uint32_t>(start_index));
}

uint32_t skity_path_get_last_pt(skity_path path, skity_point* out) {
  auto* p = read_path_handle(path);
  if (p == nullptr || out == nullptr) return 0u;
  skity::Point pt;
  if (!p->GetLastPt(&pt)) return 0u;
  *reinterpret_cast<skity::Vec4*>(out) = pt;
  return 1u;
}

void skity_path_set_last_pt(skity_path path, float x, float y) {
  if (auto* p = edit_path_handle(path)) p->SetLastPt(x, y);
}

skity_path skity_path_copy_with_matrix(skity_path path,
                                       const skity_matrix* matrix) {
  auto* p = read_path_handle(path);
  if (p == nullptr || matrix == nullptr) return nullptr;
  skity::Path copy = p->CopyWithMatrix(path_matrix(*matrix));
  return skity::capi::alloc_handle<skity_path_s>(
      SKITY_OBJECT_TYPE_PATH, SKITY_HANDLE_OWNING,
      std::make_shared<skity::Path>(std::move(copy)));
}

skity_path skity_path_copy_with_scale(skity_path path, float scale) {
  auto* p = read_path_handle(path);
  if (p == nullptr) return nullptr;
  skity::Path copy = p->CopyWithScale(scale);
  return skity::capi::alloc_handle<skity_path_s>(
      SKITY_OBJECT_TYPE_PATH, SKITY_HANDLE_OWNING,
      std::make_shared<skity::Path>(std::move(copy)));
}

uint32_t skity_path_get_point_2d(skity_path path, int32_t index,
                                 skity_vec2* out) {
  auto* p = read_path_handle(path);
  if (!p || !out) return 0u;
  auto point = p->GetPoint2D(index);
  *out = {{{point.x, point.y}}};
  return index >= 0 && static_cast<size_t>(index) < p->CountPoints() ? 1u : 0u;
}

uint32_t skity_path_get_last_pt_2d(skity_path path, skity_vec2* out) {
  auto* p = read_path_handle(path);
  if (!p) return 0u;
  skity::Vec2 point;
  bool ok = p->GetLastPt2D(out ? &point : nullptr);
  if (out) *out = {{{point.x, point.y}}};
  return ok ? 1u : 0u;
}

void skity_path_get_last_move_pt_2d(skity_path path, skity_vec2* out) {
  auto* p = read_path_handle(path);
  if (!p || !out) return;
  auto point = p->GetLastMovePt2D();
  *out = {{{point.x, point.y}}};
}

size_t skity_path_copy_points_2d(skity_path path, skity_vec2* out,
                                 size_t capacity) {
  auto* p = read_path_handle(path);
  if (!p) return 0;
  if (!out) return p->CountPoints();
  const auto count = std::min(capacity, p->CountPoints());
  if (count) std::memcpy(out, p->Points(), count * sizeof(skity_vec2));
  return count;
}

uint32_t skity_path_is_line_2d(skity_path path, skity_vec2 out_pts[2]) {
  auto* p = read_path_handle(path);
  if (!p) return 0u;
  skity::Vec2 points[2];
  if (!p->IsLine2D(out_pts ? points : nullptr)) return 0u;
  if (out_pts) {
    for (int i = 0; i < 2; ++i) out_pts[i] = {{{points[i].x, points[i].y}}};
  }
  return 1u;
}

skity_path_iter skity_path_iter_create(skity_path path, uint32_t force_closed) {
  const auto* p = read_path_handle(path);
  if (!p) return nullptr;
  auto* w = skity::capi::alloc_handle<skity_path_iter_s>(
      SKITY_OBJECT_TYPE_PATH_ITER, SKITY_HANDLE_OWNING,
      std::shared_ptr<skity::Path>{});
  if (w) {
    w->snapshot = *p;
    w->iter.SetPath(w->snapshot, force_closed != 0);
  }
  return w;
}

skity_path_raw_iter skity_path_raw_iter_create(skity_path path) {
  const auto* p = read_path_handle(path);
  if (!p) return nullptr;
  auto* w = skity::capi::alloc_handle<skity_path_raw_iter_s>(
      SKITY_OBJECT_TYPE_PATH_RAW_ITER, SKITY_HANDLE_OWNING,
      std::shared_ptr<skity::Path>{});
  if (w) {
    w->snapshot = *p;
    w->iter.SetPath(w->snapshot);
  }
  return w;
}

uint32_t skity_path_iter_set_path(skity_path_iter iter, skity_path path,
                                  uint32_t force_closed) {
  auto* w = skity::capi::resolve<skity_path_iter_s>(
      iter, SKITY_OBJECT_TYPE_PATH_ITER);
  const auto* p = read_path_handle(path);
  if (!w || !p) return 0u;
  w->snapshot = *p;
  w->iter.SetPath(w->snapshot, force_closed != 0);
  return 1u;
}

uint32_t skity_path_raw_iter_set_path(skity_path_raw_iter iter,
                                      skity_path path) {
  auto* w = skity::capi::resolve<skity_path_raw_iter_s>(
      iter, SKITY_OBJECT_TYPE_PATH_RAW_ITER);
  const auto* p = read_path_handle(path);
  if (!w || !p) return 0u;
  w->snapshot = *p;
  w->iter.SetPath(w->snapshot);
  return 1u;
}

void skity_path_iter_destroy(skity_path_iter iter) {
  skity::capi::destroy_handle<skity_path_iter_s>(iter,
                                                 SKITY_OBJECT_TYPE_PATH_ITER);
}

skity_path_verb skity_path_iter_next_2d(skity_path_iter iter,
                                        skity_vec2 out_pts[4]) {
  auto* w = skity::capi::resolve<skity_path_iter_s>(
      iter, SKITY_OBJECT_TYPE_PATH_ITER);
  return w && out_pts ? static_cast<skity_path_verb>(w->iter.Next(
                            reinterpret_cast<skity::Vec2*>(out_pts)))
                      : SKITY_PATH_VERB_DONE;
}

float skity_path_iter_conic_weight(skity_path_iter iter) {
  auto* w = skity::capi::resolve<skity_path_iter_s>(
      iter, SKITY_OBJECT_TYPE_PATH_ITER);
  return w ? w->iter.ConicWeight() : 0.f;
}

void skity_path_raw_iter_destroy(skity_path_raw_iter iter) {
  skity::capi::destroy_handle<skity_path_raw_iter_s>(
      iter, SKITY_OBJECT_TYPE_PATH_RAW_ITER);
}

skity_path_verb skity_path_raw_iter_next_2d(skity_path_raw_iter iter,
                                            skity_vec2 out_pts[4]) {
  auto* w = skity::capi::resolve<skity_path_raw_iter_s>(
      iter, SKITY_OBJECT_TYPE_PATH_RAW_ITER);
  return w && out_pts ? static_cast<skity_path_verb>(w->iter.Next(
                            reinterpret_cast<skity::Vec2*>(out_pts)))
                      : SKITY_PATH_VERB_DONE;
}

float skity_path_raw_iter_conic_weight(skity_path_raw_iter iter) {
  auto* w = skity::capi::resolve<skity_path_raw_iter_s>(
      iter, SKITY_OBJECT_TYPE_PATH_RAW_ITER);
  return w ? w->iter.ConicWeight() : 0.f;
}

uint32_t skity_path_iter_is_close_line(skity_path_iter iter) {
  auto* w = skity::capi::resolve<skity_path_iter_s>(
      iter, SKITY_OBJECT_TYPE_PATH_ITER);
  return w && w->iter.IsCloseLine() ? 1u : 0u;
}

uint32_t skity_path_iter_is_closed_contour(skity_path_iter iter) {
  auto* w = skity::capi::resolve<skity_path_iter_s>(
      iter, SKITY_OBJECT_TYPE_PATH_ITER);
  return w && w->iter.IsClosedContour() ? 1u : 0u;
}

skity_path_verb skity_path_raw_iter_peek(skity_path_raw_iter iter) {
  auto* w = skity::capi::resolve<skity_path_raw_iter_s>(
      iter, SKITY_OBJECT_TYPE_PATH_RAW_ITER);
  return w ? static_cast<skity_path_verb>(w->iter.Peek())
           : SKITY_PATH_VERB_DONE;
}

}  // extern "C"
