// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <gtest/gtest.h>

#include <limits>
#include <skity/geometry/matrix.hpp>
#include <skity/geometry/rrect.hpp>
#include <skity/geometry/stroke.hpp>
#include <skity/graphic/path_builder.hpp>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using skity::Path;
using skity::PathBuilder;
using skity::Rect;
using skity::Vec2;

static_assert(!std::is_copy_constructible_v<PathBuilder>);
static_assert(!std::is_copy_assignable_v<PathBuilder>);
static_assert(std::is_nothrow_move_constructible_v<PathBuilder>);
static_assert(std::is_nothrow_move_assignable_v<PathBuilder>);

void ExpectSamePath(const Path& actual, const Path& expected) {
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(actual.GetSegmentMasks(), expected.GetSegmentMasks());
  EXPECT_EQ(actual.GetIsAType(), expected.GetIsAType());
  EXPECT_EQ(actual.GetBounds(), expected.GetBounds());
  EXPECT_EQ(actual.IsFinite(), expected.IsFinite());
  EXPECT_EQ(actual.GetConvexityType(), expected.GetConvexityType());
  if (actual.IsConvex() && expected.IsConvex()) {
    EXPECT_EQ(actual.GetFirstDirection(), expected.GetFirstDirection());
  }
  skity::RRect a, e;
  EXPECT_EQ(actual.IsSimpleRRect(&a), expected.IsSimpleRRect(&e));
  EXPECT_EQ(a, e);
}

template <typename Target>
void BuildCommands(Target& p) {
  p.LineTo(10, 10);
  p.MoveTo(1, 2).MoveTo(3, 4).LineTo(7, 8);
  p.QuadTo(10, 2, 12, 3).ConicTo(14, 4, 16, 5, 0.75f);
  p.CubicTo(18, 6, 20, 7, 22, 8).Close().Close();
  p.LineTo(24, 9);
  p.SetLastPt(25, 10);
  p.ConicTo(26, 10, 28, 12, 0);
  p.ConicTo(30, 11, 32, 10, 1);
  p.ConicTo(34, 9, 36, 8, std::numeric_limits<float>::infinity());
  p.ConicTo(38, 7, 40, 6, std::numeric_limits<float>::quiet_NaN());
  p.SetFillType(Path::PathFillType::kEvenOdd);
}

TEST(PathBuilder, CommandsAndQueriesMatchFinishedPath) {
  PathBuilder builder;
  BuildCommands(builder);
  Path expected = builder.Snapshot();
  EXPECT_EQ(builder.CountPoints(), expected.CountPoints());
  EXPECT_EQ(builder.CountVerbs(), expected.CountVerbs());
  EXPECT_EQ(builder.GetPoint(0), expected.GetPoint2D(0));
  EXPECT_EQ(builder.GetPoint(-1), Vec2(0, 0));
  EXPECT_EQ(builder.GetPoint(10000), Vec2(0, 0));
  EXPECT_EQ(builder.GetLastMovePt(), expected.GetLastMovePt2D());
  EXPECT_EQ(builder.GetBounds(), expected.GetBounds());
  ExpectSamePath(builder.Detach(), expected);
}

TEST(PathBuilder, Vec2CommandsMatchScalars) {
  PathBuilder b;
  b.MoveTo(Vec2{1, 2}).LineTo(Vec2{3, 4});
  b.QuadTo(Vec2{5, 6}, Vec2{7, 8});
  b.ConicTo(Vec2{9, 10}, Vec2{11, 12}, 0.5f);
  b.CubicTo(Vec2{13, 14}, Vec2{15, 16}, Vec2{17, 18});
  b.SetLastPt(Vec2{19, 20});
  PathBuilder expected;
  expected.MoveTo(1, 2).LineTo(3, 4).QuadTo(5, 6, 7, 8);
  expected.ConicTo(9, 10, 11, 12, 0.5f).CubicTo(13, 14, 15, 16, 17, 18);
  expected.SetLastPt(19, 20);
  ExpectSamePath(b.Detach(), expected.Detach());
}

TEST(PathBuilder, ShapeStartIndicesWrap) {
  const Rect rect{1, 2, 101, 82};
  const auto rrect = skity::RRect::MakeRectXY(rect, 12, 13);
  for (auto dir : {Path::Direction::kCW, Path::Direction::kCCW}) {
    for (uint32_t start = 0; start < 8; ++start) {
      EXPECT_EQ(PathBuilder().AddRect(rect, dir, start).Detach(),
                PathBuilder().AddRect(rect, dir, start + 4).Detach());
      EXPECT_EQ(PathBuilder().AddOval(rect, dir, start).Detach(),
                PathBuilder().AddOval(rect, dir, start + 4).Detach());
      EXPECT_EQ(PathBuilder().AddRRect(rrect, dir, start).Detach(),
                PathBuilder().AddRRect(rrect, dir, start + 8).Detach());
    }
  }
}

TEST(PathBuilder, SnapshotIsIndependent) {
  PathBuilder builder;
  builder.AddRect({0, 0, 10, 20});
  Path first = builder.Snapshot();
  Path second = builder.Snapshot();
  ASSERT_NE(first.Points(), second.Points());
  builder.LineTo(30, 40);
  second = PathBuilder(std::move(second)).LineTo(50, 60).Detach();
  EXPECT_EQ(first.CountVerbs(), 5u);
  EXPECT_EQ(builder.GetPoint(static_cast<int>(builder.CountPoints() - 1)),
            Vec2(30, 40));
  EXPECT_EQ(second.GetPoint2D(static_cast<int>(second.CountPoints() - 1)),
            Vec2(50, 60));
}

TEST(PathBuilder, DetachTransfersAllArraysAndResetsSource) {
  PathBuilder commands;
  BuildCommands(commands);
  Path expected = commands.Snapshot();
  Path original = commands.Detach();
  const auto* points = original.Points();
  const auto* verbs = original.VerbsBegin();
  const auto* weights = original.ConicWeights();
  PathBuilder builder(std::move(original));
  EXPECT_TRUE(original.IsEmpty());
  EXPECT_EQ(original.GetFillType(), Path::PathFillType::kWinding);
  Path result = builder.Detach();
  EXPECT_EQ(result.Points(), points);
  EXPECT_EQ(result.VerbsBegin(), verbs);
  EXPECT_EQ(result.ConicWeights(), weights);
  ExpectSamePath(result, expected);
  EXPECT_TRUE(builder.IsEmpty());
  EXPECT_EQ(builder.GetFillType(), Path::PathFillType::kWinding);
  EXPECT_EQ(builder.GetLastMovePt(), Vec2(0, 0));
  builder.LineTo(8, 9);
  ExpectSamePath(builder.Detach(), PathBuilder().LineTo(8, 9).Detach());
  original = PathBuilder().LineTo(3, 4).Detach();
  EXPECT_EQ(original.GetPoint2D(0), Vec2(0, 0));
}

TEST(PathBuilder, DetachToRecyclesDestinationArrays) {
  PathBuilder capacity;
  capacity.Reserve(128, 128, 32);
  capacity.MoveTo(0, 0).ConicTo(1, 2, 3, 4, 0.5f);
  Path destination = capacity.Detach();
  const auto* points = destination.Points();
  const auto* verbs = destination.VerbsBegin();
  const auto* weights = destination.ConicWeights();
  PathBuilder builder;
  builder.AddRoundRect({0, 0, 100, 100}, 10, 20, Path::Direction::kCCW);
  builder.SetFillType(Path::PathFillType::kEvenOdd);
  Path expected = builder.Snapshot();
  builder.DetachTo(&destination);
  ExpectSamePath(destination, expected);
  EXPECT_EQ(builder.GetFillType(), Path::PathFillType::kWinding);
  EXPECT_TRUE(builder.IsEmpty());
  builder.Reserve(2, 2, 1);
  for (int i = 0; i < 12; ++i) builder.ConicTo(i, 1, i + 1, 2, 0.5f);
  builder.Reset();
  builder.ConicTo(4, 5, 6, 7, 0.5f);
  Path reused = builder.Detach();
  EXPECT_EQ(reused.Points(), points);
  EXPECT_EQ(reused.VerbsBegin(), verbs);
  EXPECT_EQ(reused.ConicWeights(), weights);
  builder.LineTo(8, 9);
  builder.DetachTo(nullptr);
  EXPECT_FALSE(builder.IsEmpty());
}

TEST(PathBuilder, MovePreservesValues) {
  PathBuilder a;
  a.AddOval({0, 0, 20, 40}, Path::Direction::kCCW);
  a.SetFillType(Path::PathFillType::kEvenOdd);
  Path expected = a.Snapshot();
  PathBuilder b(std::move(a));
  EXPECT_TRUE(a.IsEmpty());
  a.LineTo(1, 2);
  a = std::move(b);
  EXPECT_TRUE(b.IsEmpty());
  EXPECT_EQ(b.GetFillType(), Path::PathFillType::kWinding);
  auto* same = &a;
  a = std::move(*same);
  ExpectSamePath(a.Detach(), expected);
  b.LineTo(5, 6);
  ExpectSamePath(b.Detach(), PathBuilder().LineTo(5, 6).Detach());
}

TEST(PathBuilder, ImportedConvexityIsInvalidated) {
  Path source = PathBuilder().AddRect({0, 0, 10, 10}).Detach();
  ASSERT_TRUE(source.IsConvex());
  PathBuilder copied(source);
  PathBuilder moved(std::move(source));
  for (auto* builder : {&copied, &moved}) {
    builder->AddRect({20, 20, 30, 30});
    EXPECT_FALSE(builder->Detach().IsConvex());
  }
}

TEST(PathBuilder, ImportAndAppendPreserveLegacyComponents) {
  PathBuilder commands;
  BuildCommands(commands);
  skity::Matrix transform;
  transform.Set(2, 3, 3);
  transform.Set(3, 3, 2);
  Path source = commands.Detach().CopyWithMatrix(transform);
  PathBuilder imported(source);
  ExpectSamePath(imported.Detach(), source);
  const Path appended = PathBuilder()
                            .MoveTo(4, 5)
                            .AddPath(source)
                            .AddPath(source, transform)
                            .Detach();
  ASSERT_EQ(appended.CountPoints(), 1 + 2 * source.CountPoints());
  for (size_t i = 0; i < source.CountPoints(); ++i) {
    EXPECT_EQ(appended.GetPoint(1 + i), source.GetPoint(i));
    EXPECT_EQ(appended.GetPoint(1 + source.CountPoints() + i),
              transform * source.GetPoint(i));
  }
}

TEST(PathBuilder, TransformMatchesLegacyAndKeepsBuffers) {
  Path source = PathBuilder()
                    .AddRoundRect({0, 0, 100, 80}, 10, 15)
                    .SetFillType(Path::PathFillType::kEvenOdd)
                    .Detach();
  const Rect original_bounds = source.GetBounds();
  const auto* points = source.Points();
  const auto* verbs = source.VerbsBegin();
  const auto* weights = source.ConicWeights();

  skity::Matrix first = skity::Matrix::Scale(-1, 1);
  first.Set(2, 0, 2);
  first.Set(3, 1, 0.125f);
  skity::Matrix second = skity::Matrix::Translate(2, 3);
  second.Set(0, 2, 1.5f);
  std::vector<skity::Point> expected;
  for (size_t i = 0; i < source.CountPoints(); ++i) {
    expected.push_back(second * (first * source.GetPoint(i)));
  }

  PathBuilder builder(std::move(source));
  builder.Transform(first).Transform(second);
  EXPECT_NE(builder.GetBounds(), original_bounds);
  Path transformed = builder.Detach();
  EXPECT_EQ(transformed.Points(), points);
  EXPECT_EQ(transformed.VerbsBegin(), verbs);
  EXPECT_EQ(transformed.ConicWeights(), weights);
  EXPECT_EQ(transformed.GetFillType(), Path::PathFillType::kEvenOdd);
  EXPECT_EQ(transformed.GetIsAType(), Path::IsAType::kGeneral);
  ASSERT_EQ(transformed.CountPoints(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(transformed.GetPoint(i), expected[i]);
  }
}

TEST(PathBuilder, CurrentPointFollowsTheLastContour) {
  PathBuilder builder;
  Vec2 point{9, 9};
  EXPECT_FALSE(builder.GetCurrentPoint(&point));
  EXPECT_EQ(point, Vec2(9, 9));

  builder.MoveTo(1, 2).LineTo(3, 4);
  ASSERT_TRUE(builder.GetCurrentPoint(&point));
  EXPECT_EQ(point, Vec2(3, 4));
  builder.Close();
  ASSERT_TRUE(builder.GetCurrentPoint(&point));
  EXPECT_EQ(point, Vec2(1, 2));
  ASSERT_TRUE(builder.GetLastPt(&point));
  EXPECT_EQ(point, Vec2(3, 4));

  Path contour = PathBuilder().MoveTo(5, 6).LineTo(7, 8).Close().Detach();
  builder.AddPath(contour);
  ASSERT_TRUE(builder.GetCurrentPoint(&point));
  EXPECT_EQ(point, Vec2(5, 6));
  builder.LineTo(10, 11);
  ASSERT_TRUE(builder.GetCurrentPoint(&point));
  EXPECT_EQ(point, Vec2(10, 11));
  EXPECT_TRUE(builder.GetCurrentPoint(nullptr));
  EXPECT_FALSE(builder.Reset().GetCurrentPoint(nullptr));
}

TEST(Path, SharesGeometryWithCopiesAndFillVariants) {
  Path path = PathBuilder().AddRect({0, 0, 100, 80}).Detach();
  Path copy = path;
  Path even_odd = path.WithFillType(Path::PathFillType::kEvenOdd);
  EXPECT_EQ(path.Points(), copy.Points());
  EXPECT_EQ(path.Points(), even_odd.Points());
  EXPECT_EQ(path, copy);
  EXPECT_NE(path, even_odd);

  PathBuilder editor(std::move(path));
  editor.LineTo(120, 130);
  Path edited = editor.Detach();
  EXPECT_TRUE(path.IsEmpty());
  EXPECT_NE(edited.Points(), copy.Points());
  EXPECT_EQ(copy.CountPoints(), 4u);
  EXPECT_EQ(even_odd.CountPoints(), 4u);
  EXPECT_EQ(copy.GetFillType(), Path::PathFillType::kWinding);
  EXPECT_EQ(even_odd.GetFillType(), Path::PathFillType::kEvenOdd);
}

TEST(Path, StrokePreservesDestinationFillType) {
  Path source = PathBuilder()
                    .MoveTo(1, 2)
                    .LineTo(30, 40)
                    .QuadTo(50, 5, 60, 40)
                    .Close()
                    .Detach();
  skity::Paint paint;
  paint.SetStrokeWidth(4);
  skity::Stroke stroke(paint);
  Path winding, even_odd;
  even_odd = even_odd.WithFillType(Path::PathFillType::kEvenOdd);
  stroke.StrokePath(source, &winding);
  stroke.StrokePath(source, &even_odd);
  ASSERT_FALSE(winding.IsEmpty());
  EXPECT_EQ(even_odd.GetFillType(), Path::PathFillType::kEvenOdd);
  EXPECT_EQ(winding.GetFillType(), Path::PathFillType::kWinding);
  even_odd = even_odd.WithFillType(Path::PathFillType::kWinding);
  ExpectSamePath(winding, even_odd);
}
}  // namespace
