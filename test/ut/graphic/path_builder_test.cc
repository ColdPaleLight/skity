// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <gtest/gtest.h>

#include <atomic>
#include <limits>
#include <skity/graphic/path_builder.hpp>
#include <thread>
#include <utility>
#include <vector>

namespace {

using skity::Path;
using skity::PathBuilder;
using skity::Point;
using skity::Rect;

Path MakePath() {
  Path path;
  path.MoveTo(1, 2)
      .LineTo(10, 20)
      .QuadTo(15, 25, 20, 10)
      .ConicTo(25, 5, 30, 20, 0.75f)
      .CubicTo(20, 30, 10, 40, 1, 2);
  return path;
}

TEST(PathSharing, CopiesShareAllArrays) {
  const Path original = MakePath();
  Path copy(original);
  Path assigned;
  assigned = original;
  for (const auto* path : {&copy, &assigned}) {
    EXPECT_EQ(path->Points(), original.Points());
    EXPECT_EQ(path->VerbsBegin(), original.VerbsBegin());
    EXPECT_EQ(path->ConicWeights(), original.ConicWeights());
    EXPECT_EQ(*path, original);
  }
  copy.SetFillType(Path::PathFillType::kEvenOdd);
  copy.SetFirstDirection(Path::Direction::kCW);
  copy.SetConvexityType(Path::ConvexityType::kConcave);
  EXPECT_EQ(copy.Points(), original.Points());
  EXPECT_EQ(original.GetFillType(), Path::PathFillType::kWinding);
  EXPECT_NE(copy, original);
}

TEST(PathSharing, LegacyEditsDetachAndPreserveSource) {
  using Edit = void (*)(Path&);
  const Edit edits[] = {
      [](Path& p) { p.MoveTo(40, 50); },
      [](Path& p) { p.LineTo(40, 50); },
      [](Path& p) { p.QuadTo(40, 50, 60, 70); },
      [](Path& p) { p.ConicTo(40, 50, 60, 70, 0.5f); },
      [](Path& p) { p.CubicTo(40, 50, 60, 70, 80, 90); },
      [](Path& p) { p.ArcTo(40, 50, 60, 70, 5); },
      [](Path& p) { p.ArcTo(Rect::MakeWH(40, 50), 0, 120, false); },
      [](Path& p) {
        p.ArcTo(10, 20, 30, Path::kSmall, Path::Direction::kCW, 40, 50);
      },
      [](Path& p) { p.Close(); },
      [](Path& p) { p.SetLastPt(40, 50); },
      [](Path& p) { p.AddRect(Rect::MakeWH(40, 50)); },
      [](Path& p) { p.AddOval(Rect::MakeWH(40, 50)); },
      [](Path& p) { p.AddRoundRect(Rect::MakeWH(40, 50), 5, 10); },
      [](Path& p) { p.AddPath(MakePath()); },
      [](Path& p) { p.ReverseAddPath(MakePath()); },
      [](Path& p) { p.ReversePathTo(MakePath()); },
  };
  const Path expected_source = MakePath();
  for (size_t i = 0; i < std::size(edits); ++i) {
    SCOPED_TRACE(i);
    Path original = MakePath();
    const auto bounds = original.GetBounds();
    const auto convexity = original.GetConvexityType();
    Path copy = original;
    Path expected = MakePath();
    edits[i](expected);
    edits[i](copy);
    EXPECT_EQ(copy, expected);
    EXPECT_NE(copy.Points(), original.Points());
    EXPECT_EQ(original, expected_source);
    EXPECT_EQ(original.GetBounds(), bounds);
    EXPECT_EQ(original.GetConvexityType(), convexity);
    EXPECT_EQ(copy.GetBounds(), expected.GetBounds());
    EXPECT_EQ(copy.GetConvexityType(), expected.GetConvexityType());
  }
  Path move;
  move.MoveTo(1, 2);
  Path copy = move;
  copy.MoveTo(3, 4);
  EXPECT_EQ(move.GetPoint(0), Point(1, 2, 0, 1));
}

TEST(PathSharing, SelfAndSharedAppend) {
  const auto source = MakePath();
  const auto matrix = skity::Matrix::Translate(3, 4);
  for (auto mode : {Path::AddMode::kAppend, Path::AddMode::kExtend}) {
    Path expected = MakePath();
    expected.AddPath(source, matrix, mode);
    Path self = MakePath();
    self.AddPath(self, matrix, mode);
    EXPECT_EQ(self, expected);
    Path shared = source;
    shared.AddPath(source, matrix, mode);
    EXPECT_EQ(shared, expected);
    EXPECT_EQ(source, MakePath());
  }
  Path reverse = source;
  reverse.ReverseAddPath(reverse);
  Path expected = MakePath();
  expected.ReverseAddPath(source);
  EXPECT_EQ(reverse, expected);
  reverse = source;
  reverse.ReversePathTo(reverse);
  expected = MakePath();
  expected.ReversePathTo(source);
  EXPECT_EQ(reverse, expected);
}

TEST(PathSharing, ClosedAppendKeepsLastMovePoint) {
  Path source = MakePath();
  source.Close();
  Path dst = MakePath();
  dst.AddPath(source);
  EXPECT_EQ(dst.GetLastMovePt(), source.GetLastMovePt());
  dst.LineTo(100, 200);
  EXPECT_EQ(dst.GetPoint(dst.CountPoints() - 2), source.GetLastMovePt());
}

TEST(PathSharing, ResetAndMovePreserveValues) {
  Path original = MakePath();
  original.SetFillType(Path::PathFillType::kEvenOdd);
  Path copy = original;
  copy.Reset();
  EXPECT_TRUE(copy.IsEmpty());
  EXPECT_EQ(copy.GetFillType(), Path::PathFillType::kWinding);
  EXPECT_FALSE(original.IsEmpty());
  const auto* points = original.Points();
  Path moved(std::move(original));
  EXPECT_EQ(moved.Points(), points);
  EXPECT_TRUE(original.IsEmpty());
  original.LineTo(4, 5);
  EXPECT_EQ(original.GetPoint(0), Point(0, 0, 0, 1));
  original = std::move(moved);
  EXPECT_EQ(original.Points(), points);
  EXPECT_EQ(original.GetFillType(), Path::PathFillType::kEvenOdd);
  EXPECT_TRUE(moved.IsEmpty());
  moved.LineTo(6, 7);
  EXPECT_EQ(moved.GetPoint(0), Point(0, 0, 0, 1));
}

TEST(PathSharing, SwapIncludesFillAndDirection) {
  Path a = MakePath();
  a.SetFillType(Path::PathFillType::kEvenOdd);
  a.SetFirstDirection(Path::Direction::kCW);
  Path b;
  a.Swap(b);
  EXPECT_EQ(b.GetFillType(), Path::PathFillType::kEvenOdd);
  EXPECT_EQ(b.GetFirstDirection(), Path::Direction::kCW);
  EXPECT_EQ(a.GetFillType(), Path::PathFillType::kWinding);
  EXPECT_EQ(a.GetFirstDirection(), Path::Direction::kCCW);
}

TEST(PathSharing, ConcurrentConstQueriesAndIndependentEdits) {
  Path path;
  path.MoveTo(0, 0).LineTo(40, 0).LineTo(20, 30).Close();
  const Path source = path;
  std::atomic<bool> start{false};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&] {
      while (!start.load()) {
        std::this_thread::yield();
      }
      for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(source.GetBounds(), Rect::MakeLTRB(0, 0, 40, 30));
        EXPECT_TRUE(source.IsFinite());
        EXPECT_TRUE(source.IsConvex());
        EXPECT_TRUE(source.Contains(20, 10));
        Path edited = source;
        edited.SetLastPt(20, 60);
        EXPECT_EQ(edited.GetBounds(), Rect::MakeLTRB(0, 0, 40, 60));
      }
    });
  }
  start = true;
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(source, path);
}

TEST(PathSharing, LastReaderReleasesBeforeEditOrReset) {
  for (bool reset : {false, true}) {
    Path path;
    path.MoveTo(1, 2).LineTo(3, 4);
    std::atomic<bool> released{false};
    float observed = 0;
    std::thread reader([copy = path, &released, &observed]() mutable {
      observed = copy.Points()[1].x;
      copy.Reset();
      // Scheduling only: Path ownership must supply the synchronization.
      released.store(true, std::memory_order_relaxed);
    });
    while (!released.load(std::memory_order_relaxed)) {
      std::this_thread::yield();
    }
    if (reset) {
      path.Reset().MoveTo(5, 6);
    } else {
      path.SetLastPt(5, 6);
    }
    reader.join();
    EXPECT_EQ(observed, 3);
    EXPECT_EQ(path.GetPoint(path.CountPoints() - 1), Point(5, 6, 0, 1));
  }
}

TEST(PathBuilder, SnapshotDetachAndReuse) {
  PathBuilder builder;
  builder.MoveTo(1, 2).ConicTo(10, 20, 30, 40, 0.5f).Close();
  builder.SetFillType(Path::PathFillType::kEvenOdd);
  Path snapshot = builder.Snapshot();
  Path detached = builder.Detach();
  EXPECT_EQ(snapshot, detached);
  EXPECT_NE(snapshot.Points(), detached.Points());
  EXPECT_NE(snapshot.VerbsBegin(), detached.VerbsBegin());
  EXPECT_NE(snapshot.ConicWeights(), detached.ConicWeights());
  EXPECT_TRUE(builder.IsEmpty());
  EXPECT_EQ(builder.GetFillType(), Path::PathFillType::kWinding);
  builder.LineTo(100, 200);
  EXPECT_EQ(builder.GetPoint(0), Point(0, 0, 0, 1));
  EXPECT_EQ(snapshot, detached);
  EXPECT_EQ(snapshot.GetFillType(), Path::PathFillType::kEvenOdd);
  detached.SetLastPt(50, 60);
  EXPECT_NE(snapshot, detached);
}

TEST(PathBuilder, EditingAndCopyingOwnGeometry) {
  const auto original = MakePath();
  PathBuilder builder(original);
  PathBuilder copy(builder);
  PathBuilder assigned;
  assigned = builder;
  auto a = copy.Detach();
  auto b = assigned.Detach();
  auto c = builder.Detach();
  EXPECT_EQ(a, original);
  EXPECT_EQ(b, original);
  EXPECT_EQ(c, original);
  EXPECT_NE(a.Points(), original.Points());
  EXPECT_NE(a.Points(), b.Points());
  EXPECT_NE(a.Points(), c.Points());
  EXPECT_NE(b.Points(), c.Points());
}

TEST(PathBuilder, ResetAndMove) {
  PathBuilder builder(MakePath());
  const Path before = builder.Snapshot();
  builder.Reset().LineTo(4, 5);
  EXPECT_EQ(builder.GetPoint(0), Point(0, 0, 0, 1));
  PathBuilder moved(std::move(builder));
  EXPECT_TRUE(builder.IsEmpty());
  builder = std::move(moved);
  EXPECT_TRUE(moved.IsEmpty());
  EXPECT_EQ(builder.CountPoints(), 2u);
  EXPECT_EQ(before, MakePath());
  const auto result = builder.Detach();
  builder.MoveTo(1, 2);
  EXPECT_EQ(result.CountPoints(), 2u);
}

TEST(PathBuilder, EmptySnapshotsPreserveFillType) {
  Path empty = MakePath();
  empty.Reset().SetFillType(Path::PathFillType::kEvenOdd);
  PathBuilder builder(empty);
  const auto snapshot = builder.Snapshot();
  const auto detached = builder.Detach();
  EXPECT_TRUE(snapshot.IsEmpty());
  EXPECT_TRUE(detached.IsEmpty());
  EXPECT_EQ(snapshot.GetFillType(), Path::PathFillType::kEvenOdd);
  EXPECT_EQ(detached.GetFillType(), Path::PathFillType::kEvenOdd);
  EXPECT_EQ(builder.GetFillType(), Path::PathFillType::kWinding);
  empty.LineTo(10, 20);
  EXPECT_TRUE(snapshot.IsEmpty());
  EXPECT_TRUE(detached.IsEmpty());
}

TEST(PathBuilder, PreservesShapeMetadataAndTransforms) {
  const Rect rect = Rect::MakeLTRB(1, 2, 40, 60);
  PathBuilder builder;
  builder.AddRoundRect(rect, 5, 10, Path::Direction::kCCW)
      .SetFillType(Path::PathFillType::kEvenOdd);
  Path legacy;
  legacy.AddRoundRect(rect, 5, 10, Path::Direction::kCCW);
  legacy.SetFillType(Path::PathFillType::kEvenOdd);
  const auto path = builder.Detach();
  EXPECT_EQ(path, legacy);
  EXPECT_EQ(path.GetIsAType(), legacy.GetIsAType());
  EXPECT_EQ(path.GetFirstDirection(), legacy.GetFirstDirection());
  skity::RRect actual, expected;
  EXPECT_TRUE(path.IsSimpleRRect(&actual));
  EXPECT_TRUE(legacy.IsSimpleRRect(&expected));
  EXPECT_EQ(actual, expected);
  const auto matrix = skity::Matrix::Scale(2, 3);
  EXPECT_EQ(path.CopyWithMatrix(matrix), legacy.CopyWithMatrix(matrix));
  EXPECT_EQ(path.CopyWithScale(2), legacy.CopyWithScale(2));
  builder.LineTo(1, 1).MoveTo(std::numeric_limits<float>::infinity(), 0);
  EXPECT_FALSE(builder.Snapshot().IsFinite());
  builder.Reset();
  EXPECT_TRUE(builder.Detach().IsFinite());
}

}  // namespace
