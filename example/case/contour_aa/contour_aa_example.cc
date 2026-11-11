// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "case/contour_aa/contour_aa_example.hpp"

#include <skity/graphic/path_builder.hpp>
#include <skity/skity.hpp>

namespace skity::example::contour::aa {

static void fill_convex_path(skity::Canvas* canvas) {
  skity::Paint paint;
  paint.SetColor(skity::ColorSetA(skity::Color_RED, 48));
  paint.SetStyle(skity::Paint::kFill_Style);
  paint.SetAntiAlias(false);

  skity::PathBuilder path_builder;
  path_builder.MoveTo(30.0f, 80.0f);
  path_builder.LineTo(120.0f, 150.0f);
  path_builder.LineTo(100.0f, 20.0f);
  path_builder.Close();
  skity::Path path = path_builder.Detach();
  canvas->DrawPath(path, paint);

  canvas->Translate(150, 0);
  paint.SetAntiAlias(true);
  canvas->DrawPath(path, paint);
}

static void fill_concave_path(skity::Canvas* canvas) {
  skity::Paint paint;
  paint.SetColor(skity::ColorSetA(skity::Color_RED, 48));
  paint.SetStyle(skity::Paint::kFill_Style);
  paint.SetAntiAlias(false);

  skity::PathBuilder path_builder;
  path_builder.MoveTo(30.0f, 80.0f);
  path_builder.LineTo(120.0f, 20.0f);
  path_builder.LineTo(080.0f, 75.0f);
  path_builder.LineTo(120.0f, 150.0f);
  path_builder.Close();
  skity::Path path = path_builder.Detach();
  canvas->DrawPath(path, paint);

  canvas->Translate(150, 0);
  paint.SetAntiAlias(true);
  canvas->DrawPath(path, paint);
}

static void fill_self_intersection_path(skity::Canvas* canvas) {
  skity::Paint paint;
  paint.SetColor(skity::ColorSetA(skity::Color_RED, 48));
  paint.SetStyle(skity::Paint::kFill_Style);
  paint.SetAntiAlias(false);

  skity::PathBuilder path_builder;
  path_builder.MoveTo(30.0f, 30.0f);
  path_builder.LineTo(120.0f, 30.0f);
  path_builder.LineTo(30.0f, 130.0f);
  path_builder.LineTo(130.0f, 130.0f);
  path_builder.Close();
  skity::Path path = path_builder.Detach();
  canvas->DrawPath(path, paint);

  canvas->Translate(150, 0);
  paint.SetAntiAlias(true);
  canvas->DrawPath(path, paint);
}

static void fill_acute_path(skity::Canvas* canvas) {
  skity::Paint paint;
  paint.SetColor(skity::ColorSetA(skity::Color_RED, 48));
  paint.SetStyle(skity::Paint::kFill_Style);
  paint.SetAntiAlias(false);

  skity::PathBuilder path_builder;
  path_builder.MoveTo(30.0f, 30.0f);
  path_builder.LineTo(140.0f, 32.0f);
  path_builder.LineTo(30.0f, 34.0f);
  path_builder.Close();
  skity::Path path = path_builder.Detach();
  canvas->DrawPath(path, paint);

  canvas->Translate(150, 0);
  paint.SetAntiAlias(true);
  canvas->DrawPath(path, paint);
}

void fill_even_odd_fill(skity::Canvas* canvas) {
  skity::Paint paint;
  paint.SetStyle(skity::Paint::kFill_Style);
  paint.SetColor(skity::ColorSetA(skity::Color_RED, 64));
  paint.SetAntiAlias(false);

  skity::PathBuilder path_builder;
  path_builder.MoveTo(100, 10);
  path_builder.LineTo(40, 180);
  path_builder.LineTo(190, 60);
  path_builder.LineTo(10, 60);
  path_builder.LineTo(160, 180);
  path_builder.Close();
  skity::Path path = path_builder.Detach();

  path = path.WithFillType(skity::Path::PathFillType::kWinding);
  canvas->DrawPath(path, paint);

  canvas->Save();
  canvas->Translate(200, 0);
  paint.SetAntiAlias(false);

  path = path.WithFillType(skity::Path::PathFillType::kEvenOdd);
  canvas->DrawPath(path, paint);
  canvas->Restore();

  canvas->Save();
  canvas->Translate(0, 200);
  paint.SetAntiAlias(true);
  path = path.WithFillType(skity::Path::PathFillType::kWinding);
  canvas->DrawPath(path, paint);
  canvas->Restore();

  canvas->Save();
  canvas->Translate(200, 200);
  paint.SetAntiAlias(true);
  path = path.WithFillType(skity::Path::PathFillType::kEvenOdd);
  canvas->DrawPath(path, paint);
  canvas->Restore();
}

void fill_even_odd_fill_with_clip(skity::Canvas* canvas) {
  skity::Paint paint;
  paint.SetStyle(skity::Paint::kFill_Style);
  paint.SetColor(skity::ColorSetA(skity::Color_RED, 64));
  paint.SetAntiAlias(true);

  skity::PathBuilder path_builder;
  path_builder.MoveTo(100, 10);
  path_builder.LineTo(40, 180);
  path_builder.LineTo(190, 60);
  path_builder.LineTo(10, 60);
  path_builder.LineTo(160, 180);
  path_builder.Close();
  skity::Path path = path_builder.Detach();

  path = path.WithFillType(skity::Path::PathFillType::kWinding);

  canvas->Save();
  canvas->ClipRect(skity::Rect::MakeXYWH(0, 10, 80, 200));
  canvas->DrawPath(path, paint);
  canvas->Restore();

  canvas->Save();
  canvas->Translate(200, 0);
  paint.SetAntiAlias(true);
  path = path.WithFillType(skity::Path::PathFillType::kEvenOdd);
  canvas->ClipRect(skity::Rect::MakeXYWH(0, 10, 100, 200));
  canvas->DrawPath(path, paint);
  canvas->Restore();
}

static void stroke_convex_path(skity::Canvas* canvas) {
  skity::Paint paint;
  paint.SetColor(skity::ColorSetA(skity::Color_RED, 48));
  paint.SetStyle(skity::Paint::kStroke_Style);
  paint.SetStrokeWidth(2.0f);
  paint.SetAntiAlias(false);

  skity::PathBuilder path_builder;
  path_builder.MoveTo(30.0f, 80.0f);
  path_builder.LineTo(100.0f, 20.0f);
  path_builder.LineTo(120.0f, 150.0f);
  path_builder.Close();
  skity::Path path = path_builder.Detach();
  canvas->DrawPath(path, paint);

  canvas->Translate(150, 0);
  paint.SetAntiAlias(true);
  canvas->DrawPath(path, paint);
}

static void stroke_concave_path(skity::Canvas* canvas) {
  skity::Paint paint;
  paint.SetColor(skity::ColorSetA(skity::Color_RED, 48));
  paint.SetStyle(skity::Paint::kStroke_Style);
  paint.SetAntiAlias(false);

  skity::PathBuilder path_builder;
  path_builder.MoveTo(30.0f, 80.0f);
  path_builder.LineTo(120.0f, 20.0f);
  path_builder.LineTo(080.0f, 75.0f);
  path_builder.LineTo(120.0f, 150.0f);
  path_builder.Close();
  skity::Path path = path_builder.Detach();
  canvas->DrawPath(path, paint);

  canvas->Translate(150, 0);
  paint.SetAntiAlias(true);
  canvas->DrawPath(path, paint);
}

static void stroke_self_intersection_path(skity::Canvas* canvas) {
  skity::Paint paint;
  paint.SetColor(skity::ColorSetA(skity::Color_RED, 48));
  paint.SetStyle(skity::Paint::kStroke_Style);
  paint.SetAntiAlias(false);

  skity::PathBuilder path_builder;
  path_builder.MoveTo(30.0f, 30.0f);
  path_builder.LineTo(120.0f, 30.0f);
  path_builder.LineTo(30.0f, 130.0f);
  path_builder.LineTo(130.0f, 130.0f);
  path_builder.Close();
  skity::Path path = path_builder.Detach();
  canvas->DrawPath(path, paint);

  canvas->Translate(150, 0);
  paint.SetAntiAlias(true);
  canvas->DrawPath(path, paint);
}

static void stroke_acute_path(skity::Canvas* canvas) {
  skity::Paint paint;
  paint.SetColor(skity::ColorSetA(skity::Color_RED, 48));
  paint.SetStyle(skity::Paint::kStroke_Style);
  paint.SetAntiAlias(false);

  skity::PathBuilder path_builder;
  path_builder.MoveTo(30.0f, 30.0f);
  path_builder.LineTo(140.0f, 32.0f);
  path_builder.LineTo(30.0f, 34.0f);
  path_builder.Close();
  skity::Path path = path_builder.Detach();
  canvas->DrawPath(path, paint);

  canvas->Translate(150, 0);
  paint.SetAntiAlias(true);
  canvas->DrawPath(path, paint);
}

void draw_contour_aa(skity::Canvas* canvas) {
  canvas->Save();
  canvas->Translate(0.0f, 100.0f);
  fill_convex_path(canvas);
  canvas->Restore();

  canvas->Save();
  canvas->Translate(0.0f, 250.0f);
  fill_concave_path(canvas);
  canvas->Restore();

  canvas->Save();
  canvas->Translate(0.0f, 400.0f);
  fill_self_intersection_path(canvas);
  canvas->Restore();

  canvas->Save();
  canvas->Translate(0.0f, 530.0f);
  fill_acute_path(canvas);
  canvas->Restore();

  canvas->Save();
  canvas->Translate(300.0f, 100.0f);
  fill_even_odd_fill(canvas);
  canvas->Restore();

  canvas->Save();
  canvas->Translate(300.0f, 500.0f);
  fill_even_odd_fill_with_clip(canvas);
  canvas->Restore();

  canvas->Save();
  canvas->Translate(700.0f, 100.0f);
  stroke_convex_path(canvas);
  canvas->Restore();

  canvas->Save();
  canvas->Translate(700.0f, 250.0f);
  stroke_concave_path(canvas);
  canvas->Restore();

  canvas->Save();
  canvas->Translate(700.0f, 400.0f);
  stroke_self_intersection_path(canvas);
  canvas->Restore();

  canvas->Save();
  canvas->Translate(700.0f, 530.0f);
  stroke_acute_path(canvas);
  canvas->Restore();
}
}  // namespace skity::example::contour::aa
