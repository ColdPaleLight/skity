// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <random>
#include <skity/geometry/stroke.hpp>
#include <skity/graphic/path_builder.hpp>
#include <vector>

#include "gtest/gtest.h"
#include "src/geometry/geometry.hpp"
#include "src/geometry/math.hpp"
#include "src/geometry/point_priv.hpp"
#include "src/graphic/path_priv.hpp"

TEST(Path, test_iter) {
  skity::Path p;
  skity::Point pts[4];

  skity::Path::Iter no_path_iter;
  EXPECT_EQ(no_path_iter.Next(pts), skity::Path::Verb::kDone);

  no_path_iter.SetPath(p, false);
  EXPECT_EQ(no_path_iter.Next(pts), skity::Path::Verb::kDone);

  no_path_iter.SetPath(p, true);
  EXPECT_EQ(no_path_iter.Next(pts), skity::Path::Verb::kDone);

  skity::Path::Iter iter{p, false};
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kDone);

  p = {};
  iter.SetPath(p, false);
  EXPECT_FALSE(iter.IsClosedContour());

  p = skity::PathBuilder(std::move(p)).LineTo(1, 1).Close().Detach();
  iter.SetPath(p, false);
  EXPECT_TRUE(iter.IsClosedContour());

  p = {};
  iter.SetPath(p, true);
  EXPECT_FALSE(iter.IsClosedContour());
  p = skity::PathBuilder(std::move(p)).LineTo(1, 1).Detach();
  iter.SetPath(p, true);
  EXPECT_TRUE(iter.IsClosedContour());
  p = skity::PathBuilder(std::move(p)).MoveTo(0, 0).LineTo(2, 2).Detach();
  iter.SetPath(p, false);
  EXPECT_FALSE(iter.IsClosedContour());

  p = {};
  p = skity::PathBuilder(std::move(p)).QuadTo(0, 0, 0, 0).Detach();
  iter.SetPath(p, false);
  iter.Next(pts);
  EXPECT_EQ(skity::Path::Verb::kQuad, iter.Next(pts));

  p = {};
  p = skity::PathBuilder(std::move(p)).ConicTo(0, 0, 0, 0, 0.5f).Detach();
  iter.SetPath(p, false);
  iter.Next(pts);
  EXPECT_EQ(skity::Path::Verb::kConic, iter.Next(pts));
}

static void check_move(skity::Path::RawIter* iter, float x0, float y0) {
  skity::Point pts[4];
  auto v = iter->Next(pts);
  EXPECT_EQ(v, skity::Path::Verb::kMove);
  EXPECT_FLOAT_EQ(pts[0].x, x0);
  EXPECT_FLOAT_EQ(pts[0].y, y0);
}

static void check_line(skity::Path::RawIter* iter, float x1, float y1) {
  skity::Point pts[4];
  auto v = iter->Next(pts);
  EXPECT_EQ(v, skity::Path::Verb::kLine);
  EXPECT_FLOAT_EQ(pts[1].x, x1);
  EXPECT_FLOAT_EQ(pts[1].y, y1);
}

static void check_quad(skity::Path::RawIter* iter, float x1, float y1, float x2,
                       float y2) {
  skity::Point pts[4];
  auto v = iter->Next(pts);
  EXPECT_EQ(v, skity::Path::Verb::kQuad);
  EXPECT_FLOAT_EQ(pts[1].x, x1);
  EXPECT_FLOAT_EQ(pts[1].y, y1);
  EXPECT_FLOAT_EQ(pts[2].x, x2);
  EXPECT_FLOAT_EQ(pts[2].y, y2);
}

static void check_done(skity::Path*, skity::Path::RawIter* iter) {
  skity::Point pts[4];
  auto v = iter->Next(pts);
  EXPECT_EQ(v, skity::Path::Verb::kDone);
}

static void check_done_and_reset(skity::Path* path,
                                 skity::Path::RawIter* iter) {
  check_done(path, iter);
  *path = {};
}

static void check_path_is_line_and_reset(skity::Path* path, float x1,
                                         float y1) {
  skity::Path::RawIter iter(*path);
  check_move(std::addressof(iter), 0, 0);
  check_line(std::addressof(iter), x1, y1);
  check_done_and_reset(path, std::addressof(iter));
}

#if 0
static void check_path_is_line(skity::Path *path, float x1, float y1) {
  skity::Path::RawIter iter(*path);
  check_move(std::addressof(iter), 0, 0);
  check_line(std::addressof(iter), x1, y1);
  check_done(path, std::addressof(iter));
}
#endif

static void check_path_is_line_pair_and_reset(skity::Path* path, float x1,
                                              float y1, float x2, float y2) {
  skity::Path::RawIter iter(*path);
  check_move(std::addressof(iter), 0, 0);
  check_line(std::addressof(iter), x1, y1);
  check_line(std::addressof(iter), x2, y2);
  check_done_and_reset(path, std::addressof(iter));
}

static void check_path_is_quad_and_reset(skity::Path* path, float x1, float y1,
                                         float x2, float y2) {
  skity::Path::RawIter iter(*path);
  check_move(std::addressof(iter), 0, 0);
  check_quad(std::addressof(iter), x1, y1, x2, y2);
  check_done_and_reset(path, std::addressof(iter));
}

static void check_Close(const skity::Path& path) {
  for (int i = 0; i < 2; i++) {
    skity::Path::Iter iter(path, static_cast<bool>(i));
    skity::Point mv;
    skity::Point pts[4];
    skity::Path::Verb v;
    int nMT = 0;
    int nCL = 0;
    skity::PointSet(mv, 0, 0);
    while ((v = iter.Next(pts)) != skity::Path::Verb::kDone) {
      switch (v) {
        case skity::Path::Verb::kMove:
          mv = pts[0];
          ++nMT;
          break;
        case skity::Path::Verb::kClose:
          EXPECT_EQ(mv, pts[0]);
          ++nCL;
          break;
        default:
          break;
      }
    }
    EXPECT_TRUE(!i || nMT == nCL);
  }
}

TEST(Path, test_Close) {
  skity::Path ClosePt = skity::PathBuilder().MoveTo(0, 0).Close().Detach();
  check_Close(ClosePt);

  skity::Path openPt = skity::PathBuilder().MoveTo(0, 0).Detach();
  check_Close(openPt);

  skity::Path empty;
  check_Close(empty);
  empty = skity::PathBuilder(std::move(empty)).Close().Detach();
  check_Close(empty);

  skity::Path quad = skity::PathBuilder()
                         .QuadTo(Float1, Float1, 10 * Float1, 10 * Float1)
                         .Detach();
  check_Close(quad);
  quad = skity::PathBuilder(std::move(quad)).Close().Detach();
  check_Close(quad);

  skity::Path cubic = skity::PathBuilder()
                          .CubicTo(Float1, Float1, 10 * Float1, 10 * Float1,
                                   20 * Float1, 20 * Float1)
                          .Detach();
  check_Close(cubic);
  cubic = skity::PathBuilder(std::move(cubic)).Close().Detach();
  check_Close(cubic);

  skity::Path line = skity::PathBuilder()
                         .MoveTo(Float1, Float1)
                         .LineTo(10 * Float1, 10 * Float1)
                         .Detach();
  check_Close(line);
  line = skity::PathBuilder(std::move(line)).Close().Detach();
  check_Close(line);

  skity::Path moves = skity::PathBuilder()
                          .MoveTo(Float1, Float1)
                          .MoveTo(5 * Float1, 5 * Float1)
                          .MoveTo(Float1, 10 * Float1)
                          .MoveTo(10 * Float1, Float1)
                          .Detach();
  check_Close(moves);
}

TEST(Path, test_ArcTo) {
  skity::Path p;

  p = skity::PathBuilder(std::move(p)).ArcTo(0, 0, 1, 2, 1).Detach();
  check_path_is_line_and_reset(std::addressof(p), 0, 0);
  p = skity::PathBuilder(std::move(p)).ArcTo(1, 2, 1, 2, 1).Detach();
  check_path_is_line_and_reset(std::addressof(p), 1, 2);
  p = skity::PathBuilder(std::move(p)).ArcTo(1, 2, 3, 4, 0).Detach();
  check_path_is_line_and_reset(std::addressof(p), 1, 2);
  p = skity::PathBuilder(std::move(p)).ArcTo(1, 2, 0, 0, 1).Detach();
  check_path_is_line_and_reset(std::addressof(p), 1, 2);
  p = skity::PathBuilder(std::move(p)).ArcTo(1, 0, 1, 1, 1).Detach();
  skity::Point pt;
  EXPECT_TRUE(p.GetLastPt(std::addressof(pt)));
  EXPECT_FLOAT_EQ(pt.x, 1.f);
  EXPECT_FLOAT_EQ(pt.y, 1.f);
  p = {};
  p = skity::PathBuilder(std::move(p)).ArcTo(1, 0, 1, -1, 1).Detach();
  EXPECT_TRUE(p.GetLastPt(std::addressof(pt)));
  EXPECT_FLOAT_EQ(pt.x, 1.f);
  EXPECT_FLOAT_EQ(pt.y, -1.f);
  {
    p = {};
    p = skity::PathBuilder(std::move(p)).MoveTo(216, 216).Detach();
    // FIXME ArcTo is not correct
    // p.ArcTo(216, 108, 0, skity::Path::ArcSize::kLarge,
    //         skity::Path::Direction::kCW, 216, 0);
    // p.ArcTo(270, 135, 0, skity::Path::ArcSize::kLarge,
    //         skity::Path::Direction::kCCW, 216, 216);
    int n = p.CountPoints();
    EXPECT_EQ(p.GetPoint(0), p.GetPoint(n - 1));
  }
}

TEST(Path, test_quad) {
  skity::Path p = skity::PathBuilder().ConicTo(1, 2, 3, 4, -1).Detach();
  check_path_is_line_and_reset(std::addressof(p), 3, 4);
  p = skity::PathBuilder(std::move(p))
          .ConicTo(1.f, 2.f, 3.f, 4.f, FloatInfinity)
          .Detach();
  check_path_is_line_pair_and_reset(std::addressof(p), 1, 2, 3, 4);
  p = skity::PathBuilder(std::move(p)).ConicTo(1, 2, 3, 4, 1).Detach();
  check_path_is_quad_and_reset(std::addressof(p), 1, 2, 3, 4);
}

TEST(Path_RawIter, test_RawIter) {
  skity::Path p;
  skity::Point pts[4];

  skity::Path::RawIter noPathIter;
  EXPECT_EQ(noPathIter.Next(pts), skity::Path::Verb::kDone);
  noPathIter.SetPath(p);
  EXPECT_EQ(noPathIter.Next(pts), skity::Path::Verb::kDone);

  skity::Path::RawIter iter(p);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kDone);

  p = skity::PathBuilder(std::move(p)).MoveTo(Float1, 0).Detach();
  iter.SetPath(p);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kMove);
  EXPECT_FLOAT_EQ(pts[0].x, Float1);
  EXPECT_FLOAT_EQ(pts[0].y, 0);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kDone);

  // Only the last MoveTo is valid
  p = skity::PathBuilder(std::move(p))
          .MoveTo(Float1 * 2, Float1)
          .MoveTo(Float1 * 3, Float1 * 2)
          .Detach();
  iter.SetPath(p);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kMove);
  EXPECT_FLOAT_EQ(pts[0].x, Float1 * 3);
  EXPECT_FLOAT_EQ(pts[0].y, Float1 * 2);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kDone);

  p = {};
  p = skity::PathBuilder(std::move(p)).Close().Detach();
  iter.SetPath(p);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kDone);

  // Move/Close sequences
  p = {};
  p = skity::PathBuilder(std::move(p))
          .Close()
          .MoveTo(Float1, 0)
          .Close()
          .Close()
          .MoveTo(Float1 * 2, Float1)
          .Close()
          .MoveTo(Float1 * 3, Float1 * 2)
          .MoveTo(Float1 * 4, Float1 * 3)
          .Close()
          .Detach();
  iter.SetPath(p);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kMove);
  EXPECT_FLOAT_EQ(pts[0].x, Float1);
  EXPECT_FLOAT_EQ(pts[0].y, 0);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kClose);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kMove);
  EXPECT_FLOAT_EQ(pts[0].x, Float1 * 2);
  EXPECT_FLOAT_EQ(pts[0].y, Float1);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kClose);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kMove);
  EXPECT_FLOAT_EQ(pts[0].x, Float1 * 4);
  EXPECT_FLOAT_EQ(pts[0].y, Float1 * 3);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kClose);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kDone);

  // Generate random paths and verify
  skity::Point randomPts[25];
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 5; j++) {
      skity::PointSet(randomPts[i * 5 + j], Float1 * float(i),
                      Float1 * float(j));
    }
  }

  std::vector<skity::Path::Verb> verbs = {
      skity::Path::Verb::kMove,  skity::Path::Verb::kLine,
      skity::Path::Verb::kQuad,  skity::Path::Verb::kConic,
      skity::Path::Verb::kCubic, skity::Path::Verb::kClose};
  std::vector<skity::Point> expectedPts(31);
  std::vector<skity::Path::Verb> expectedVerbs(22);
  skity::Path::Verb nextVerb;

  for (int i = 0; i < 2; i++) {
    p = {};
    bool lastWasClose = true;
    bool haveMoteTo = false;
    bool lastWasMove = false;
    skity::Point lastMoveToPt = {0, 0, 0, 1};
    int numPoints = 0;
    std::random_device
        rd;  // Will be used to obtain a seed for the random number engine
    std::mt19937 gen(rd());  // Standard mersenne_twister_engine seeded with
                             // rd()
    int numVerbs = std::uniform_int_distribution<int>{0, 9}(gen);
    int numIterVerbs = 0;
    for (int j = 0; j < numVerbs; j++) {
      do {
        nextVerb =
            verbs[std::uniform_int_distribution<int>(0, verbs.size() - 1)(gen)];
      } while ((lastWasClose && nextVerb == skity::Path::Verb::kClose) ||
               (lastWasMove && nextVerb == skity::Path::Verb::kMove));
      switch (nextVerb) {
        case skity::Path::Verb::kMove:
          lastMoveToPt = expectedPts[numPoints] =
              randomPts[std::uniform_int_distribution<int>(0, 24)(gen)];
          p = skity::PathBuilder(std::move(p))
                  .MoveTo(expectedPts[numPoints].x, expectedPts[numPoints].y)
                  .Detach();
          numPoints += 1;
          lastWasClose = false;
          lastWasMove = true;
          haveMoteTo = true;
          break;
        case skity::Path::Verb::kLine:
          if (!haveMoteTo) {
            expectedPts[numPoints++] = lastMoveToPt;
            expectedVerbs[numIterVerbs++] = skity::Path::Verb::kMove;
            haveMoteTo = true;
          }
          expectedPts[numPoints] =
              randomPts[std::uniform_int_distribution<int>(0, 24)(gen)];
          p = skity::PathBuilder(std::move(p))
                  .LineTo(expectedPts[numPoints].x, expectedPts[numPoints].y)
                  .Detach();
          numPoints += 1;
          lastWasClose = false;
          lastWasMove = false;
          break;
        case skity::Path::Verb::kQuad:
          if (!haveMoteTo) {
            expectedPts[numPoints++] = lastMoveToPt;
            expectedVerbs[numIterVerbs++] = skity::Path::Verb::kMove;
            haveMoteTo = true;
          }
          expectedPts[numPoints] =
              randomPts[std::uniform_int_distribution<int>(0, 24)(gen)];
          expectedPts[numPoints + 1] =
              randomPts[std::uniform_int_distribution<int>(0, 24)(gen)];
          p = skity::PathBuilder(std::move(p))
                  .QuadTo(expectedPts[numPoints].x, expectedPts[numPoints].y,
                          expectedPts[numPoints + 1].x,
                          expectedPts[numPoints + 1].y)
                  .Detach();
          numPoints += 2;
          lastWasClose = false;
          lastWasMove = false;
          break;
        case skity::Path::Verb::kConic:
          if (!haveMoteTo) {
            expectedPts[numPoints++] = lastMoveToPt;
            expectedVerbs[numIterVerbs++] = skity::Path::Verb::kMove;
            haveMoteTo = true;
          }
          expectedPts[numPoints] =
              randomPts[std::uniform_int_distribution<int>(0, 24)(gen)];
          expectedPts[numPoints + 1] =
              randomPts[std::uniform_int_distribution<int>(0, 24)(gen)];
          p = skity::PathBuilder(std::move(p))
                  .ConicTo(
                      expectedPts[numPoints].x, expectedPts[numPoints].y,
                      expectedPts[numPoints + 1].x,
                      expectedPts[numPoints + 1].y,
                      std::uniform_real_distribution<float>(0.f, 0.999f)(gen) *
                          4)
                  .Detach();
          numPoints += 2;
          lastWasClose = false;
          lastWasMove = false;
          break;
        case skity::Path::Verb::kCubic:
          if (!haveMoteTo) {
            expectedPts[numPoints++] = lastMoveToPt;
            expectedVerbs[numIterVerbs++] = skity::Path::Verb::kMove;
            haveMoteTo = true;
          }
          expectedPts[numPoints] =
              randomPts[std::uniform_int_distribution<int>(0, 24)(gen)];
          expectedPts[numPoints + 1] =
              randomPts[std::uniform_int_distribution<int>(0, 24)(gen)];
          expectedPts[numPoints + 2] =
              randomPts[std::uniform_int_distribution<int>(0, 24)(gen)];
          p = skity::PathBuilder(std::move(p))
                  .CubicTo(expectedPts[numPoints].x, expectedPts[numPoints].y,
                           expectedPts[numPoints + 1].x,
                           expectedPts[numPoints + 1].y,
                           expectedPts[numPoints + 2].x,
                           expectedPts[numPoints + 2].y)
                  .Detach();
          numPoints += 3;
          lastWasClose = false;
          lastWasMove = false;
          break;
        case skity::Path::Verb::kClose:
          p = skity::PathBuilder(std::move(p)).Close().Detach();
          haveMoteTo = false;
          lastWasClose = true;
          lastWasMove = false;
          break;
        default:
          break;
      }
      expectedVerbs[numIterVerbs++] = nextVerb;
    }

    iter.SetPath(p);
    numVerbs = numIterVerbs;
    numIterVerbs = 0;
    int numIterPts = 0;

    skity::Point lastMoveTo;
    skity::Point lastPt;
    skity::PointSet(lastMoveTo, 0, 0);
    skity::PointSet(lastPt, 0, 0);
    while ((nextVerb = iter.Next(pts)) != skity::Path::Verb::kDone) {
      EXPECT_EQ(nextVerb, expectedVerbs[numIterVerbs]);
      numIterVerbs++;
      switch (nextVerb) {
        case skity::Path::Verb::kMove:
          EXPECT_TRUE(numIterPts < numPoints);
          EXPECT_FLOAT_EQ(pts[0].x, expectedPts[numIterPts].x);
          EXPECT_FLOAT_EQ(pts[0].y, expectedPts[numIterPts].y);
          lastPt = lastMoveTo = pts[0];
          numIterPts += 1;
          break;
        case skity::Path::Verb::kLine:
          EXPECT_LT(numIterPts, numPoints + 1);
          EXPECT_EQ(pts[0], lastPt);
          EXPECT_EQ(pts[1], expectedPts[numIterPts]);
          lastPt = pts[1];
          numIterPts += 1;
          break;
        case skity::Path::Verb::kQuad:
        case skity::Path::Verb::kConic:
          EXPECT_LT(numIterPts, numPoints + 2);
          EXPECT_EQ(pts[0], lastPt);
          EXPECT_EQ(pts[1], expectedPts[numIterPts]);
          EXPECT_EQ(pts[2], expectedPts[numIterPts + 1]);
          lastPt = pts[2];
          numIterPts += 2;
          break;
        case skity::Path::Verb::kCubic:
          EXPECT_LT(numIterPts, numPoints + 3);
          EXPECT_EQ(pts[0], lastPt);
          EXPECT_EQ(pts[1], expectedPts[numIterPts]);
          EXPECT_EQ(pts[2], expectedPts[numIterPts + 1]);
          EXPECT_EQ(pts[3], expectedPts[numIterPts + 2]);
          lastPt = pts[3];
          numIterPts += 3;
          break;
        case skity::Path::Verb::kClose:
          lastPt = lastMoveTo;
          break;
        default:
          break;
      }
    }
    EXPECT_EQ(numIterPts, numPoints);
    EXPECT_EQ(numIterVerbs, numVerbs);
  }
}

TEST(Path, bad_case) {
  skity::Path path;
  skity::Point randomPts[25];
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 5; j++) {
      skity::PointSet(randomPts[i * 5 + j], Float1 * float(i),
                      Float1 * float(j));
    }
  }

  path = skity::PathBuilder(std::move(path))
             .MoveTo(randomPts[0].x, randomPts[0].y)
             .CubicTo(randomPts[1].x, randomPts[1].y, randomPts[2].x,
                      randomPts[2].y, randomPts[3].x, randomPts[3].y)
             .CubicTo(randomPts[3].x, randomPts[3].y, randomPts[5].x,
                      randomPts[5].y, randomPts[6].x, randomPts[6].y)
             .Close()
             .MoveTo(randomPts[7].x, randomPts[7].y)
             .Detach();
  skity::Path::RawIter iter(path);

  skity::Point pts[4];
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kMove);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kCubic);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kCubic);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kClose);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kMove);
  EXPECT_EQ(pts[0], randomPts[7]);
}

TEST(path, RangeIterReadsPlanarPointsWithoutAdvancing) {
  skity::Path path = skity::PathBuilder()
                         .MoveTo(1, 2)
                         .LineTo(3, 4)
                         .QuadTo(5, 6, 7, 8)
                         .ConicTo(9, 10, 11, 12, 0.5f)
                         .CubicTo(13, 14, 15, 16, 17, 18)
                         .Close()
                         .Detach();
  skity::Matrix matrix;
  matrix.Set(2, 3, 7.f);
  matrix.Set(3, 3, 2.f);
  path = path.CopyWithMatrix(matrix);
  skity::PathPriv::Iterate range(path);
  constexpr int point_counts[] = {1, 2, 3, 3, 4, 0};
  int count = 0;
  for (auto iter = range.begin(); iter != range.end(); ++iter) {
    const auto before = iter;
    const auto [verb, points, weight] = *iter;
    EXPECT_EQ(iter.PeekVerb(), verb);
    for (int i = 0; i < point_counts[static_cast<int>(verb)]; ++i) {
      EXPECT_EQ(iter.GetPoint2D(i), (skity::Vec2{points[i].x, points[i].y}));
    }
    if (verb == skity::Path::Verb::kConic) {
      EXPECT_EQ(iter.ConicWeight(), *weight);
      EXPECT_EQ(iter.ConicWeight(), 0.5f);
    }
    EXPECT_EQ(iter, before);
    ++count;
  }
  EXPECT_EQ(count, 6);
}

TEST(path, test_range_iter) {
  skity::Path path;
  skity::PathPriv::Iterate iterate{path};

  EXPECT_TRUE(iterate.begin() == iterate.end());

  path = skity::PathBuilder(std::move(path)).MoveTo(Float1, 0).Detach();
  iterate = skity::PathPriv::Iterate(path);

  auto iter = iterate.begin();
  {
    auto ret = *iter++;
    auto verb = std::get<0>(ret);
    auto pts = std::get<1>(ret);
    EXPECT_EQ(verb, skity::Path::Verb::kMove);
    EXPECT_EQ(pts[0].x, Float1);
    EXPECT_EQ(pts[0].y, 0.f);
  }
  EXPECT_TRUE(iter == iterate.end());

  path = skity::PathBuilder(std::move(path))
             .LineTo(Float1 * 2, Float1)
             .LineTo(Float1 * 3, Float1 * 2)
             .Detach();
  iterate = skity::PathPriv::Iterate(path);
  iter = iterate.begin();
  {
    auto ret = *iter++;
    auto verb = std::get<0>(ret);
    auto pts = std::get<1>(ret);
    EXPECT_EQ(verb, skity::Path::Verb::kMove);
    EXPECT_EQ(pts[0].x, Float1);
    EXPECT_EQ(pts[0].y, 0.f);
  }
  {
    auto ret = *iter++;
    auto verb = std::get<0>(ret);
    auto pts = std::get<1>(ret);
    EXPECT_EQ(verb, skity::Path::Verb::kLine);
    EXPECT_EQ(pts[1].x, Float1 * 2);
    EXPECT_EQ(pts[1].y, Float1);
  }
  {
    auto ret = *iter++;
    auto verb = std::get<0>(ret);
    auto pts = std::get<1>(ret);
    EXPECT_EQ(verb, skity::Path::Verb::kLine);
    EXPECT_EQ(pts[1].x, Float1 * 3);
    EXPECT_EQ(pts[1].y, Float1 * 2);
  }

  EXPECT_TRUE(iter == iterate.end());

  path = {};
  path = skity::PathBuilder(std::move(path)).Close().Detach();
  iterate = skity::PathPriv::Iterate(path);
  EXPECT_TRUE(iterate.begin() == iterate.end());

  path = {};
  path = skity::PathBuilder(std::move(path))
             .Close()
             .Detach();  // Not stored, no purpose
  path = skity::PathBuilder(std::move(path))
             .MoveTo(Float1, 0)
             .Close()
             .Close()
             .Detach();  // Not stored, no purpose
  path = skity::PathBuilder(std::move(path))
             .MoveTo(Float1 * 2, Float1)
             .Close()
             .MoveTo(Float1 * 3, Float1 * 2)
             .MoveTo(Float1 * 4, Float1 * 3)
             .Close()
             .Detach();

  iterate = skity::PathPriv::Iterate(path);
  iter = iterate.begin();
  {
    auto ret = *iter++;
    auto verb = std::get<0>(ret);
    auto pts = std::get<1>(ret);
    EXPECT_EQ(verb, skity::Path::Verb::kMove);
    EXPECT_EQ(pts[0].x, Float1);
    EXPECT_EQ(pts[0].y, 0);
  }
  {
    auto ret = *iter++;
    auto verb = std::get<0>(ret);
    EXPECT_EQ(verb, skity::Path::Verb::kClose);
  }
  {
    auto ret = *iter++;
    auto verb = std::get<0>(ret);
    auto pts = std::get<1>(ret);
    EXPECT_EQ(verb, skity::Path::Verb::kMove);
    EXPECT_EQ(pts[0].x, Float1 * 2);
    EXPECT_EQ(pts[0].y, Float1);
  }
  {
    auto ret = *iter++;
    auto verb = std::get<0>(ret);
    EXPECT_EQ(verb, skity::Path::Verb::kClose);
  }
  {
    auto ret = *iter++;
    auto verb = std::get<0>(ret);
    auto pts = std::get<1>(ret);
    EXPECT_EQ(verb, skity::Path::Verb::kMove);
    EXPECT_EQ(pts[0].x, Float1 * 4);
    EXPECT_EQ(pts[0].y, Float1 * 3);
  }
  {
    auto ret = *iter++;
    auto verb = std::get<0>(ret);
    EXPECT_EQ(verb, skity::Path::Verb::kClose);
  }
  EXPECT_TRUE(iter == iterate.end());
}

struct IsRectTest {
  skity::Vec2* points;
  size_t count;
  bool Close;
  bool is_rect;
};

TEST(path, IsRect) {
  // passing tests
  std::vector<skity::Vec2> r1{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  std::vector<skity::Vec2> r2{{1, 0}, {1, 1}, {0, 1}, {0, 0}};
  std::vector<skity::Vec2> r3{{1, 1}, {0, 1}, {0, 0}, {1, 0}};
  std::vector<skity::Vec2> r4{{0, 1}, {0, 0}, {1, 0}, {1, 1}};
  std::vector<skity::Vec2> r5{{0, 0}, {0, 1}, {1, 1}, {1, 0}};
  std::vector<skity::Vec2> r6{{0, 1}, {1, 1}, {1, 0}, {0, 0}};
  std::vector<skity::Vec2> r7{{1, 1}, {1, 0}, {0, 0}, {0, 1}};
  std::vector<skity::Vec2> r8{{1, 0}, {0, 0}, {0, 1}, {1, 1}};
  std::vector<skity::Vec2> r9{{0, 1}, {1, 1}, {1, 0}, {0, 0}};
  std::vector<skity::Vec2> ra{{0, 0}, {0, .5f}, {0, 1}, {.5f, 1},
                              {1, 1}, {1, .5f}, {1, 0}, {.5f, 0}};
  std::vector<skity::Vec2> rb{{0, 0}, {.5f, 0}, {1, 0}, {1, .5f},
                              {1, 1}, {.5f, 1}, {0, 1}, {0, .5f}};
  std::vector<skity::Vec2> rc{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}};
  std::vector<skity::Vec2> rd{{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}};
  std::vector<skity::Vec2> re{{0, 0}, {1, 0}, {1, 0}, {1, 1}, {0, 1}};
  std::vector<skity::Vec2> rf{{1, 0}, {8, 0}, {8, 8}, {0, 8}, {0, 0}};

  // failing tests
  std::vector<skity::Vec2> f1{{0, 0}, {1, 0}, {1, 1}};  // too few points
  std::vector<skity::Vec2> f2{{0, 0}, {1, 1}, {0, 1}, {1, 0}};  // diagonal
  std::vector<skity::Vec2> f3{{0, 0}, {1, 0}, {1, 1},
                              {0, 1}, {0, 0}, {1, 0}};  // wraps
  std::vector<skity::Vec2> f4{{0, 0}, {1, 0}, {0, 0},
                              {1, 0}, {1, 1}, {0, 1}};  // backs up
  std::vector<skity::Vec2> f5{
      {0, 0}, {1, 0}, {1, 1}, {2, 0}};  // end overshoots
  std::vector<skity::Vec2> f6{
      {0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 2}};  // end overshoots
  std::vector<skity::Vec2> f7{
      {0, 0}, {1, 0}, {1, 1}, {0, 2}};  // end overshoots
  std::vector<skity::Vec2> f8{{0, 0}, {1, 0}, {1, 1}, {1, 0}};  // 'L'
  std::vector<skity::Vec2> f9{{1, 0}, {8, 0}, {8, 8},
                              {0, 8}, {0, 0}, {2, 0}};  // overlaps
  std::vector<skity::Vec2> fa{{1, 0}, {8, 0},  {8, 8},
                              {0, 8}, {0, -1}, {1, -1}};  // non colinear gap
  std::vector<skity::Vec2> fb{
      {1, 0}, {8, 0}, {8, 8}, {0, 8}, {0, 1}};  // falls short

  // no Close, but we should detect them as fillably the same as a rect
  std::vector<skity::Vec2> c1{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  std::vector<skity::Vec2> c2{{0, 0}, {1, 0}, {1, 2}, {0, 2}, {0, 1}};
  std::vector<skity::Vec2> c3{{0, 0}, {1, 0}, {1, 2},
                              {0, 2}, {0, 1}, {0, 0}};  // hit the start

  // like c2, but we double-back on ourselves
  std::vector<skity::Vec2> d1{{0, 0}, {1, 0}, {1, 2}, {0, 2}, {0, 1}, {0, 2}};
  // like c2, but we overshoot the start point
  std::vector<skity::Vec2> d2{{0, 0}, {1, 0}, {1, 2}, {0, 2}, {0, -1}};
  std::vector<skity::Vec2> d3{{0, 0}, {1, 0}, {1, 2}, {0, 2}, {0, -1}, {0, 0}};

  std::vector<IsRectTest> tests{
      {r1.data(), r1.size(), true, true},   {r2.data(), r2.size(), true, true},
      {r3.data(), r3.size(), true, true},   {r4.data(), r4.size(), true, true},
      {r5.data(), r5.size(), true, true},   {r6.data(), r6.size(), true, true},
      {r7.data(), r7.size(), true, true},   {r8.data(), r8.size(), true, true},
      {r9.data(), r9.size(), true, true},   {ra.data(), ra.size(), true, true},
      {rb.data(), rb.size(), true, true},   {rc.data(), rc.size(), true, true},
      {rd.data(), rd.size(), true, true},   {re.data(), re.size(), true, true},
      {rf.data(), rf.size(), true, true},

      {f1.data(), f1.size(), true, false},  {f2.data(), f2.size(), true, false},
      {f3.data(), f3.size(), true, false},  {f4.data(), f4.size(), true, false},
      {f5.data(), f5.size(), true, false},  {f6.data(), f6.size(), true, false},
      {f7.data(), f7.size(), true, false},  {f8.data(), f8.size(), true, false},
      {f9.data(), f9.size(), true, false},  {fa.data(), fa.size(), true, false},
      {fb.data(), fb.size(), true, false},

      {c1.data(), c1.size(), false, true},  {c2.data(), c2.size(), false, true},
      {c3.data(), c3.size(), false, true},

      {d1.data(), d1.size(), false, false}, {d2.data(), d2.size(), false, true},
      {d3.data(), d3.size(), false, false},
  };

  int32_t index = 0;
  std::for_each(tests.begin(), tests.end(), [&index](IsRectTest const& test) {
    skity::Path path = skity::PathBuilder()
                           .MoveTo(test.points[0].x, test.points[0].y)
                           .Detach();

    for (size_t i = 1; i < test.count; i++) {
      path = skity::PathBuilder(std::move(path))
                 .LineTo(test.points[i].x, test.points[i].y)
                 .Detach();
    }

    if (test.Close) {
      path = skity::PathBuilder(std::move(path)).Close().Detach();
    }

    EXPECT_TRUE(test.is_rect == path.IsRect(nullptr))
        << "failed index = " << index;
    index++;
  });

  // fail, Close then line
  skity::Path path1 = skity::PathBuilder().MoveTo(r1[0].x, r1[0].y).Detach();
  for (size_t i = 0; i < r1.size() - 1; i++) {
    path1 =
        skity::PathBuilder(std::move(path1)).LineTo(r1[i].x, r1[i].y).Detach();
  }
  path1 = skity::PathBuilder(std::move(path1)).Close().LineTo(1, 0).Detach();
  EXPECT_FALSE(path1.IsRect(nullptr));

  // fail, move in the middle
  path1 = {};
  path1 =
      skity::PathBuilder(std::move(path1)).MoveTo(r1[0].x, r1[0].y).Detach();
  for (size_t i = 0; i < r1.size(); i++) {
    if (i == 2) {
      path1 = skity::PathBuilder(std::move(path1)).MoveTo(1, .5f).Detach();
    }

    path1 =
        skity::PathBuilder(std::move(path1)).LineTo(r1[i].x, r1[i].y).Detach();
  }
  path1 = skity::PathBuilder(std::move(path1)).Close().Detach();
  EXPECT_FALSE(path1.IsRect(nullptr));

  // fail, move on the edge
  path1 = {};
  path1 =
      skity::PathBuilder(std::move(path1)).MoveTo(r1[0].x, r1[0].y).Detach();
  for (size_t i = 1; i < r1.size(); i++) {
    path1 = skity::PathBuilder(std::move(path1))
                .MoveTo(r1[i - 1].x, r1[i - 1].y)
                .LineTo(r1[i].x, r1[i].y)
                .Detach();
  }
  path1 = skity::PathBuilder(std::move(path1)).Close().Detach();
  EXPECT_FALSE(path1.IsRect(nullptr));

  // fail, quad
  path1 = {};
  path1 =
      skity::PathBuilder(std::move(path1)).MoveTo(r1[0].x, r1[0].y).Detach();
  for (size_t i = 0; i < r1.size(); i++) {
    if (i == 2) {
      path1 =
          skity::PathBuilder(std::move(path1)).QuadTo(1, .5f, 1, .5f).Detach();
    }
    path1 =
        skity::PathBuilder(std::move(path1)).LineTo(r1[i].x, r1[i].y).Detach();
  }
  path1 = skity::PathBuilder(std::move(path1)).Close().Detach();
  EXPECT_FALSE(path1.IsRect(nullptr));
}

TEST(Path, Contains) {
  skity::Path path;

  EXPECT_FALSE(path.Contains(0, 0));

  path = skity::PathBuilder(std::move(path))
             .MoveTo(4, 4)
             .LineTo(6, 8)
             .LineTo(6, 2)
             .Detach();
  path = path.WithFillType(skity::Path::PathFillType::kEvenOdd);
  EXPECT_TRUE(path.Contains(5, 4));
  EXPECT_TRUE(path.Contains(6, 8));
  EXPECT_FALSE(path.Contains(8, 8));

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(8, 6)
             .LineTo(7, 7)
             .LineTo(8, 8)
             .LineTo(9, 7)
             .Detach();
  path = path.WithFillType(skity::Path::PathFillType::kEvenOdd);
  EXPECT_TRUE(path.Contains(8, 7));
  EXPECT_TRUE(path.Contains(9, 7));
  EXPECT_FALSE(path.Contains(10, 7));

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(10, 6)
             .LineTo(6, 7)
             .LineTo(10, 8)
             .LineTo(8, 7)
             .Detach();
  path = path.WithFillType(skity::Path::PathFillType::kEvenOdd);
  EXPECT_TRUE(path.Contains(7, 7));
  EXPECT_FALSE(path.Contains(9, 7));

  skity::Path ring = skity::PathBuilder()
                         .MoveTo(8, 6)
                         .LineTo(7, 7)
                         .LineTo(8, 8)
                         .LineTo(9, 7)
                         .LineTo(8, 6)
                         .MoveTo(8, 4)
                         .LineTo(5, 7)
                         .LineTo(8, 12)
                         .LineTo(12, 7)
                         .Close()
                         .Detach();
  EXPECT_TRUE(ring.Contains(8, 7));
  ring = ring.WithFillType(skity::Path::PathFillType::kEvenOdd);
  EXPECT_FALSE(ring.Contains(8, 7));
  EXPECT_TRUE(ring.Contains(8, 6));
  EXPECT_TRUE(ring.Contains(9, 7));
  EXPECT_TRUE(ring.Contains(6, 7));
  EXPECT_TRUE(ring.Contains(10, 7));

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(4, 4)
             .LineTo(6, 8)
             .LineTo(8, 4)
             .Detach();

  // test on edge
  EXPECT_TRUE(path.Contains(6, 4));
  EXPECT_TRUE(path.Contains(5, 6));
  EXPECT_TRUE(path.Contains(7, 6));
  // quick reject
  EXPECT_FALSE(path.Contains(4, 0));
  EXPECT_FALSE(path.Contains(0, 4));
  EXPECT_FALSE(path.Contains(4, 10));
  EXPECT_FALSE(path.Contains(10, 4));
  // test various in x;
  EXPECT_FALSE(path.Contains(5, 7));
  EXPECT_TRUE(path.Contains(6, 7));
  EXPECT_FALSE(path.Contains(7, 7));

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(4, 4)
             .LineTo(8, 6)
             .LineTo(4, 8)
             .Detach();

  // test on edge
  EXPECT_TRUE(path.Contains(4, 6));
  EXPECT_TRUE(path.Contains(6, 5));
  EXPECT_TRUE(path.Contains(6, 7));
  // test various crossings in y
  EXPECT_FALSE(path.Contains(7, 5));
  EXPECT_TRUE(path.Contains(7, 6));
  EXPECT_FALSE(path.Contains(7, 7));

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(4, 4)
             .LineTo(8, 4)
             .LineTo(8, 8)
             .LineTo(4, 8)
             .Detach();

  // test on vertices
  EXPECT_TRUE(path.Contains(4, 4));
  EXPECT_TRUE(path.Contains(8, 4));
  EXPECT_TRUE(path.Contains(8, 8));
  EXPECT_TRUE(path.Contains(4, 8));

  // test quads
  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(4, 4)
             .QuadTo(6, 6, 8, 8)
             .QuadTo(6, 8, 4, 8)
             .QuadTo(4, 6, 4, 4)
             .Detach();
  EXPECT_TRUE(path.Contains(5, 6));
  EXPECT_FALSE(path.Contains(6, 5));
  // test quad edge
  EXPECT_TRUE(path.Contains(5, 5));
  EXPECT_TRUE(path.Contains(5, 8));
  EXPECT_TRUE(path.Contains(4, 5));
  // test quad endpoints
  EXPECT_TRUE(path.Contains(4, 4));
  EXPECT_TRUE(path.Contains(8, 8));
  EXPECT_TRUE(path.Contains(4, 8));

  path = {};

  std::array<skity::Vec2, 7> pts{
      skity::Vec2{6, 6}, skity::Vec2{8, 8}, skity::Vec2{6, 8},
      skity::Vec2{4, 8}, skity::Vec2{4, 6}, skity::Vec2{4, 4},
      skity::Vec2{6, 6},
  };
  path =
      skity::PathBuilder(std::move(path)).MoveTo(pts[0].x, pts[0].y).Detach();

  for (size_t i = 1; i < pts.size(); i += 2) {
    path = skity::PathBuilder(std::move(path))
               .QuadTo(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y)
               .Detach();
  }

  EXPECT_FALSE(path.Contains(4, 5));
  EXPECT_FALSE(path.Contains(5, 5));
  EXPECT_TRUE(path.Contains(4, 6));
  EXPECT_TRUE(path.Contains(5, 6));
  EXPECT_TRUE(path.Contains(6, 6));
  EXPECT_FALSE(path.Contains(7, 6));
  EXPECT_FALSE(path.Contains(4, 7));
  EXPECT_TRUE(path.Contains(5, 7));
  EXPECT_TRUE(path.Contains(6, 7));
  EXPECT_FALSE(path.Contains(7, 7));
  EXPECT_FALSE(path.Contains(4, 8));
  EXPECT_FALSE(path.Contains(5, 8));
  EXPECT_TRUE(path.Contains(6, 8));
  EXPECT_FALSE(path.Contains(7, 8));

  // test conics
  std::array<skity::Vec2, 10> c_pts{
      skity::Vec2{5, 4}, skity::Vec2{6, 5}, skity::Vec2{7, 6},
      skity::Vec2{6, 6}, skity::Vec2{4, 6}, skity::Vec2{5, 7},
      skity::Vec2{5, 5}, skity::Vec2{5, 4}, skity::Vec2{6, 5},
      skity::Vec2{7, 6},
  };

  for (int32_t i = 0; i < 3; i++) {
    path = {};
    path = path.WithFillType(skity::Path::PathFillType::kEvenOdd);

    path = skity::PathBuilder(std::move(path))
               .MoveTo(c_pts[i].x, c_pts[i].y)
               .CubicTo(c_pts[i + 1].x, c_pts[i + 1].y, c_pts[i + 2].x,
                        c_pts[i + 2].y, c_pts[i + 3].x, c_pts[i + 3].y)
               .CubicTo(c_pts[i + 4].x, c_pts[i + 4].y, c_pts[i + 5].x,
                        c_pts[i + 5].y, c_pts[i + 6].x, c_pts[i + 6].y)
               .Close()
               .Detach();

    EXPECT_TRUE(path.Contains(5.5f, 5.5f));
    EXPECT_FALSE(path.Contains(4.5f, 5.5f));

    // test cubic end points
    EXPECT_TRUE(path.Contains(c_pts[i].x, c_pts[i].y));
    EXPECT_TRUE(path.Contains(c_pts[i + 3].x, c_pts[i + 3].y));
    EXPECT_TRUE(path.Contains(c_pts[i + 6].x, c_pts[i + 6].y));
  }
}

union FloatIntUnion {
  float fFloat;
  int32_t fSignBitInt;
};

// Helper to see a bit pattern as a float (w/o aliasing warnings)
static inline float Bits2Float(int32_t floatAsBits) {
  FloatIntUnion data;
  data.fSignBitInt = floatAsBits;
  return data.fFloat;
}

TEST(Path, Convexity1) {
  skity::Path path;
  EXPECT_TRUE(path.IsConvex());

  path = {};
  path = skity::PathBuilder(std::move(path)).AddCircle(0, 0, 10).Detach();
  EXPECT_TRUE(path.IsConvex());

  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddCircle(0, 0, 10)
             .AddCircle(0, 0, 10)
             .Detach();
  EXPECT_FALSE(path.IsConvex());

  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddRect(0, 0, 10, 10, skity::Path::Direction::kCCW)
             .Detach();
  EXPECT_TRUE(path.IsConvex());

  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddRect(0, 0, 10, 10, skity::Path::Direction::kCW)
             .Detach();
  EXPECT_TRUE(path.IsConvex());

  path = {};
  path = skity::PathBuilder(std::move(path)).QuadTo(100, 100, 50, 50).Detach();
  EXPECT_TRUE(path.IsConvex());

  path = {};
  path = skity::PathBuilder(std::move(path)).MoveTo(0, 0).Detach();
  EXPECT_TRUE(path.IsConvex());

  path = {};
  path =
      skity::PathBuilder(std::move(path)).MoveTo(0, 0).LineTo(10, 10).Detach();
  EXPECT_TRUE(path.IsConvex());

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(0, 0)
             .LineTo(10, 10)
             .LineTo(20, 20)
             .LineTo(0, 0)
             .LineTo(10, 10)
             .Detach();
  EXPECT_FALSE(path.IsConvex());

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(0, 0)
             .LineTo(10, 10)
             .LineTo(10, 20)
             .Detach();
  EXPECT_TRUE(path.IsConvex());

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(0, 0)
             .LineTo(10, 10)
             .LineTo(10, 0)
             .Detach();
  EXPECT_TRUE(path.IsConvex());

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(0, 0)
             .LineTo(10, 10)
             .LineTo(10, 0)
             .LineTo(0, 10)
             .Detach();
  EXPECT_FALSE(path.IsConvex());

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(0, 0)
             .LineTo(10, 0)
             .LineTo(0, 10)
             .LineTo(-10, -10)
             .Detach();
  EXPECT_FALSE(path.IsConvex());

  static const float float_max = 3.402823466e+38f;
  static const float float_min = -float_max;
  static const skity::Vec2 axisAlignedPts[] = {
      {float_max, 0},
      {0, float_max},
      {float_min, 0},
      {0, float_min},
  };

  const size_t axisAlignedPtsCount =
      sizeof(axisAlignedPts) / sizeof(axisAlignedPts[0]);

  for (int index = 0; index < (int)(11 * axisAlignedPtsCount); ++index) {
    int f = (int)(index % axisAlignedPtsCount);
    int g = (int)((f + 1) % axisAlignedPtsCount);
    path = {};
    int curveSelect = index % 11;
    switch (curveSelect) {
      case 0:
        path = skity::PathBuilder(std::move(path))
                   .MoveTo(axisAlignedPts[f])
                   .Detach();
        break;
      case 1:
        path = skity::PathBuilder(std::move(path))
                   .LineTo(axisAlignedPts[f])
                   .Detach();
        break;
      case 2:
        path = skity::PathBuilder(std::move(path))
                   .QuadTo(axisAlignedPts[f], axisAlignedPts[f])
                   .Detach();
        break;
      case 3:
        path = skity::PathBuilder(std::move(path))
                   .QuadTo(axisAlignedPts[f], axisAlignedPts[g])
                   .Detach();
        break;
      case 4:
        path = skity::PathBuilder(std::move(path))
                   .QuadTo(axisAlignedPts[g], axisAlignedPts[f])
                   .Detach();
        break;
      case 5:
        path = skity::PathBuilder(std::move(path))
                   .CubicTo(axisAlignedPts[f], axisAlignedPts[f],
                            axisAlignedPts[f])
                   .Detach();
        break;
      case 6:
        path = skity::PathBuilder(std::move(path))
                   .CubicTo(axisAlignedPts[f], axisAlignedPts[f],
                            axisAlignedPts[g])
                   .Detach();
        break;
      case 7:
        path = skity::PathBuilder(std::move(path))
                   .CubicTo(axisAlignedPts[f], axisAlignedPts[g],
                            axisAlignedPts[f])
                   .Detach();
        break;
      case 8:
        path = skity::PathBuilder(std::move(path))
                   .CubicTo(axisAlignedPts[f], axisAlignedPts[g],
                            axisAlignedPts[g])
                   .Detach();
        break;
      case 9:
        path = skity::PathBuilder(std::move(path))
                   .CubicTo(axisAlignedPts[g], axisAlignedPts[f],
                            axisAlignedPts[f])
                   .Detach();
        break;
      case 10:
        path = skity::PathBuilder(std::move(path))
                   .CubicTo(axisAlignedPts[g], axisAlignedPts[f],
                            axisAlignedPts[g])
                   .Detach();
        break;
    }
    if (curveSelect == 0 || curveSelect == 1 || curveSelect == 2 ||
        curveSelect == 5) {
      EXPECT_TRUE(path.IsConvex());
    } else {
      EXPECT_FALSE(path.IsConvex());
    }
  }

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(Bits2Float(0xbe9171db),
                     Bits2Float(0xbd7eeb5d))
             .Detach();  // -0.284072f, -0.0622362f
  path = skity::PathBuilder(std::move(path))
             .LineTo(Bits2Float(0xbe9171db),
                     Bits2Float(0xbd7eea38))
             .Detach();  // -0.284072f, -0.0622351f
  path = skity::PathBuilder(std::move(path))
             .LineTo(Bits2Float(0xbe9171a0),
                     Bits2Float(0xbd7ee5a7))
             .Detach();  // -0.28407f, -0.0622307f
  path = skity::PathBuilder(std::move(path))
             .LineTo(Bits2Float(0xbe917147),
                     Bits2Float(0xbd7ed886))
             .Detach();  // -0.284067f, -0.0622182f
  path = skity::PathBuilder(std::move(path))
             .LineTo(Bits2Float(0xbe917378),
                     Bits2Float(0xbd7ee1a9))
             .Detach();  // -0.284084f, -0.0622269f
  path = skity::PathBuilder(std::move(path))
             .LineTo(Bits2Float(0xbe9171db),
                     Bits2Float(0xbd7eeb5d))
             .Detach();  // -0.284072f, -0.0622362f
  path = skity::PathBuilder(std::move(path)).Close().Detach();
  EXPECT_FALSE(path.IsConvex());
}

TEST(Path, Convexity2) {
  skity::Path pt = skity::PathBuilder().MoveTo(0, 0).Close().Detach();
  EXPECT_TRUE(pt.IsConvex());

  skity::Path line =
      skity::PathBuilder().MoveTo(12, 20).LineTo(-12, -20).Close().Detach();
  EXPECT_TRUE(line.IsConvex());

  skity::Path triLeft = skity::PathBuilder()
                            .MoveTo(0, 0)
                            .LineTo(1, 0)
                            .LineTo(1, 1)
                            .Close()
                            .Detach();
  EXPECT_TRUE(triLeft.IsConvex());

  skity::Path triRight = skity::PathBuilder()
                             .MoveTo(0, 0)
                             .LineTo(-1, 0)
                             .LineTo(1, 1)
                             .Close()
                             .Detach();
  EXPECT_TRUE(triRight.IsConvex());

  skity::Path square = skity::PathBuilder()
                           .MoveTo(0, 0)
                           .LineTo(1, 0)
                           .LineTo(1, 1)
                           .LineTo(0, 1)
                           .Close()
                           .Detach();
  EXPECT_TRUE(square.IsConvex());

  skity::Path redundantSquare = skity::PathBuilder()
                                    .MoveTo(0, 0)
                                    .LineTo(0, 0)
                                    .LineTo(0, 0)
                                    .LineTo(1, 0)
                                    .LineTo(1, 0)
                                    .LineTo(1, 0)
                                    .LineTo(1, 1)
                                    .LineTo(1, 1)
                                    .LineTo(1, 1)
                                    .LineTo(0, 1)
                                    .LineTo(0, 1)
                                    .LineTo(0, 1)
                                    .Close()
                                    .Detach();
  EXPECT_TRUE(redundantSquare.IsConvex());

  skity::Path bowTie = skity::PathBuilder()
                           .MoveTo(0, 0)
                           .LineTo(0, 0)
                           .LineTo(0, 0)
                           .LineTo(1, 1)
                           .LineTo(1, 1)
                           .LineTo(1, 1)
                           .LineTo(1, 0)
                           .LineTo(1, 0)
                           .LineTo(1, 0)
                           .LineTo(0, 1)
                           .LineTo(0, 1)
                           .LineTo(0, 1)
                           .Close()
                           .Detach();
  EXPECT_FALSE(bowTie.IsConvex());

  skity::Path spiral = skity::PathBuilder()
                           .MoveTo(0, 0)
                           .LineTo(100, 0)
                           .LineTo(100, 100)
                           .LineTo(0, 100)
                           .LineTo(0, 50)
                           .LineTo(50, 50)
                           .LineTo(50, 75)
                           .Close()
                           .Detach();
  EXPECT_FALSE(spiral.IsConvex());

  skity::Path dent = skity::PathBuilder()
                         .MoveTo(0, 0)
                         .LineTo(100, 100)
                         .LineTo(0, 100)
                         .LineTo(-50, 200)
                         .LineTo(-200, 100)
                         .Close()
                         .Detach();
  EXPECT_FALSE(dent.IsConvex());

  skity::Path strokedSin;
  for (int i = 0; i < 2000; i++) {
    float x = i / 2.0;
    float y = 500 - (x + std::sin(x / 100) * 40) / 3;
    if (0 == i) {
      strokedSin =
          skity::PathBuilder(std::move(strokedSin)).MoveTo(x, y).Detach();
    } else {
      strokedSin =
          skity::PathBuilder(std::move(strokedSin)).LineTo(x, y).Detach();
    }
  }
  skity::Paint paint;
  paint.SetStrokeWidth(2);
  skity::Stroke stroke(paint);
  skity::Path strokeDst;
  stroke.StrokePath(strokedSin, &strokeDst);
  EXPECT_FALSE(dent.IsConvex());

  skity::Path degenerateConcave = skity::PathBuilder()
                                      .MoveTo(148.67912f, 191.875f)
                                      .LineTo(470.37695f, 7.5f)
                                      .LineTo(148.67912f, 191.875f)
                                      .LineTo(41.446522f, 376.25f)
                                      .LineTo(-55.971577f, 460.0f)
                                      .LineTo(41.446522f, 376.25f)
                                      .Detach();
  EXPECT_FALSE(degenerateConcave.IsConvex());

  skity::Path badFirstVector =
      skity::PathBuilder()
          .MoveTo(501.087708f, 319.610352f)
          .LineTo(501.087708f, 319.610352f)
          .CubicTo(501.087677f, 319.610321f, 449.271606f, 258.078674f,
                   395.084564f, 198.711182f)
          .CubicTo(358.967072f, 159.140717f, 321.910553f, 120.650436f,
                   298.442322f, 101.955399f)
          .LineTo(301.557678f, 98.044601f)
          .CubicTo(325.283844f, 116.945084f, 362.615204f, 155.720825f,
                   398.777557f, 195.340454f)
          .CubicTo(453.031860f, 254.781662f, 504.912262f, 316.389618f,
                   504.912292f, 316.389648f)
          .LineTo(504.912292f, 316.389648f)
          .LineTo(501.087708f, 319.610352f)
          .Close()
          .Detach();
  EXPECT_FALSE(badFirstVector.IsConvex());

  skity::Path falseBackEdge =
      skity::PathBuilder()
          .MoveTo(-217.83430557928145f, -382.14948768484857f)
          .LineTo(-227.73867866614847f, -399.52485512718323f)
          .CubicTo(-158.3541047666846f, -439.0757140459542f, -79.8654464485281f,
                   -459.875f, -1.1368683772161603e-13f, -459.875f)
          .LineTo(-8.08037266162413e-14f, -439.875f)
          .LineTo(-8.526512829121202e-14f, -439.87499999999994f)
          .CubicTo(-76.39209188702645f, -439.87499999999994f,
                   -151.46727226799754f, -419.98027663161537f,
                   -217.83430557928145f, -382.14948768484857f)
          .Close()
          .Detach();
  EXPECT_FALSE(falseBackEdge.IsConvex());
}

TEST(Path, ConvexityDoubleBack) {
  skity::Path doubleback = skity::PathBuilder().LineTo(1, 1).Detach();
  EXPECT_TRUE(doubleback.IsConvex());
  doubleback = skity::PathBuilder(std::move(doubleback)).LineTo(2, 2).Detach();
  EXPECT_TRUE(doubleback.IsConvex());
  doubleback = {};
  doubleback = skity::PathBuilder(std::move(doubleback)).LineTo(1, 0).Detach();
  EXPECT_TRUE(doubleback.IsConvex());
  doubleback = skity::PathBuilder(std::move(doubleback)).LineTo(2, 0).Detach();
  EXPECT_TRUE(doubleback.IsConvex());
  doubleback = skity::PathBuilder(std::move(doubleback)).LineTo(1, 0).Detach();
  EXPECT_TRUE(doubleback.IsConvex());
  doubleback = {};
  doubleback =
      skity::PathBuilder(std::move(doubleback)).QuadTo(1, 1, 2, 2).Detach();
  EXPECT_TRUE(doubleback.IsConvex());
  doubleback = {};
  doubleback =
      skity::PathBuilder(std::move(doubleback)).QuadTo(1, 0, 2, 0).Detach();
  EXPECT_TRUE(doubleback.IsConvex());
  doubleback =
      skity::PathBuilder(std::move(doubleback)).QuadTo(1, 0, 0, 0).Detach();
  EXPECT_TRUE(doubleback.IsConvex());

  doubleback = {};
  doubleback = skity::PathBuilder(std::move(doubleback))
                   .LineTo(1, 0)
                   .LineTo(1, 0)
                   .LineTo(1, 1)
                   .LineTo(1, 1)
                   .LineTo(1, 0)
                   .Detach();
  EXPECT_FALSE(doubleback.IsConvex());

  doubleback = {};
  doubleback = skity::PathBuilder(std::move(doubleback))
                   .LineTo(-1, 0)
                   .LineTo(-1, 1)
                   .LineTo(-1, 0)
                   .Detach();
  EXPECT_FALSE(doubleback.IsConvex());
}

TEST(Path, AddPath) {
  skity::Path child = skity::PathBuilder()
                          .AddRect(skity::Rect::MakeLTRB(20, 20, 30, 40))
                          .Detach();
  skity::Path path = skity::PathBuilder()
                         .MoveTo(10, 10)
                         .LineTo(25, 10)
                         .AddPath(child)
                         .LineTo(100, 100)
                         .Detach();
  skity::Path::Iter iter;
  iter.SetPath(path, false);
  skity::Point pts[4];
  int move_count = 0;
  skity::Path::Verb v;
  while ((v = iter.Next(pts)) != skity::Path::Verb::kDone) {
    if (v == skity::Path::Verb::kMove) {
      move_count++;
    }
  }
  EXPECT_EQ(move_count, 3);
}

TEST(Path, AddClosedPathAfterCurveKeepsLastMovePoint) {
  skity::Path path =
      skity::PathBuilder().MoveTo(0, 0).CubicTo(1, 2, 3, 4, 5, 6).Detach();
  skity::Path child =
      skity::PathBuilder().MoveTo(10, 20).LineTo(30, 40).Close().Detach();
  path = skity::PathBuilder(std::move(path)).AddPath(child).Detach();

  EXPECT_EQ(path.GetLastMovePt(), (skity::Point{10, 20, 0, 1}));
  EXPECT_EQ(path.GetLastMovePt2D(), (skity::Vec2{10, 20}));

  skity::Path expected = path;
  expected = skity::PathBuilder(std::move(expected))
                 .MoveTo(10, 20)
                 .LineTo(50, 60)
                 .Detach();
  path = skity::PathBuilder(std::move(path)).LineTo(50, 60).Detach();
  EXPECT_EQ(path, expected);
}

TEST(Path, StrokePath) {
  skity::Path src =
      skity::PathBuilder().MoveTo(30, 30).QuadTo(40, 30, 120, 30).Detach();
  skity::Paint paint;
  paint.SetStrokeWidth(2);
  skity::Stroke stroke(paint);
  skity::Path dst;
  stroke.StrokePath(src, &dst);
  EXPECT_TRUE(dst.Contains(120, 30));
}

TEST(Path, SegmentMask) {
  skity::Path path;
  EXPECT_EQ(path.GetSegmentMasks(), 0);
  path = skity::PathBuilder(std::move(path)).MoveTo(0, 0).Detach();
  EXPECT_EQ(path.GetSegmentMasks(), 0);

  path = {};
  path = skity::PathBuilder(std::move(path)).MoveTo(0, 0).MoveTo(1, 1).Detach();
  EXPECT_EQ(path.GetSegmentMasks(), 0);

  path = {};
  path = skity::PathBuilder(std::move(path)).MoveTo(0, 0).LineTo(1, 1).Detach();
  EXPECT_EQ(path.GetSegmentMasks(), skity::Path::SegmentMask::kLine);

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(0, 0)
             .QuadTo(1, 1, 2, 2)
             .Detach();
  EXPECT_EQ(path.GetSegmentMasks(), skity::Path::SegmentMask::kQuad);

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(0, 0)
             .ConicTo(1, 1, 2, 2, 0.5f)
             .Detach();
  EXPECT_EQ(path.GetSegmentMasks(), skity::Path::SegmentMask::kConic);

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(0, 0)
             .CubicTo(1, 1, 2, 2, 3, 3)
             .Detach();
  EXPECT_EQ(path.GetSegmentMasks(), skity::Path::SegmentMask::kCubic);

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(0, 0)
             .QuadTo(1, 1, 2, 2)
             .CubicTo(3, 3, 4, 4, 5, 5)
             .Detach();
  EXPECT_EQ(path.GetSegmentMasks(),
            skity::Path::SegmentMask::kQuad | skity::Path::SegmentMask::kCubic);

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(0, 0)
             .LineTo(1, 1)
             .QuadTo(2, 2, 3, 3)
             .ConicTo(4, 4, 5, 5, 0.5f)
             .CubicTo(4, 4, 5, 5, 6, 6)
             .Detach();
  EXPECT_EQ(path.GetSegmentMasks(), skity::Path::SegmentMask::kLine |
                                        skity::Path::SegmentMask::kQuad |
                                        skity::Path::SegmentMask::kConic |
                                        skity::Path::SegmentMask::kCubic);

  path = {};
  EXPECT_EQ(path.GetSegmentMasks(), 0);

  // Swap
  {
    skity::Path a = skity::PathBuilder().MoveTo(0, 0).LineTo(1, 1).Detach();
    EXPECT_EQ(a.GetSegmentMasks(), skity::Path::SegmentMask::kLine);
    skity::Path b =
        skity::PathBuilder().MoveTo(0, 0).QuadTo(1, 1, 3, 3).Detach();
    EXPECT_EQ(b.GetSegmentMasks(), skity::Path::SegmentMask::kQuad);
    std::swap(a, b);
    EXPECT_EQ(a.GetSegmentMasks(), skity::Path::SegmentMask::kQuad);
    EXPECT_EQ(b.GetSegmentMasks(), skity::Path::SegmentMask::kLine);
  }

  // AddPath
  {
    skity::Path a = skity::PathBuilder().MoveTo(0, 0).LineTo(1, 1).Detach();
    EXPECT_EQ(a.GetSegmentMasks(), skity::Path::SegmentMask::kLine);
    skity::Path b =
        skity::PathBuilder().MoveTo(0, 0).QuadTo(1, 1, 3, 3).Detach();
    EXPECT_EQ(b.GetSegmentMasks(), skity::Path::SegmentMask::kQuad);
    a = skity::PathBuilder(std::move(a)).AddPath(b).Detach();
    EXPECT_EQ(a.GetSegmentMasks(), skity::Path::SegmentMask::kLine |
                                       skity::Path::SegmentMask::kQuad);
  }

  // CopyWithMatrix
  {
    skity::Path a = skity::PathBuilder()
                        .MoveTo(0, 0)
                        .LineTo(1, 1)
                        .CubicTo(2, 2, 3, 3, 4, 4)
                        .Detach();
    EXPECT_EQ(a.GetSegmentMasks(), skity::Path::SegmentMask::kLine |
                                       skity::Path::SegmentMask::kCubic);
    skity::Path b = a.CopyWithMatrix(skity::Matrix::Translate(10, 10));
    EXPECT_EQ(b.GetSegmentMasks(), skity::Path::SegmentMask::kLine |
                                       skity::Path::SegmentMask::kCubic);
  }

  // CopyWithScale
  {
    skity::Path a = skity::PathBuilder()
                        .MoveTo(0, 0)
                        .LineTo(1, 1)
                        .CubicTo(2, 2, 3, 3, 4, 4)
                        .Detach();
    EXPECT_EQ(a.GetSegmentMasks(), skity::Path::SegmentMask::kLine |
                                       skity::Path::SegmentMask::kCubic);
    skity::Path b = a.CopyWithScale(3);
    EXPECT_EQ(b.GetSegmentMasks(), skity::Path::SegmentMask::kLine |
                                       skity::Path::SegmentMask::kCubic);
  }
}

TEST(Path, GetBounds_EmptyPath) {
  skity::Path path;
  auto bounds = path.GetBounds();
  EXPECT_TRUE(bounds.IsEmpty());
}

TEST(Path, GetBounds_SinglePoint) {
  skity::Path path = skity::PathBuilder().MoveTo(10, 20).Detach();
  auto bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 10.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 20.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 10.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 20.f);
}

TEST(Path, GetBounds_Line) {
  skity::Path path = skity::PathBuilder().MoveTo(0, 0).LineTo(100, 50).Detach();
  auto bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 50.f);
}

TEST(Path, GetBounds_Rectangle) {
  skity::Path path = skity::PathBuilder()
                         .AddRect(skity::Rect::MakeLTRB(10, 20, 100, 200))
                         .Detach();
  auto bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 10.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 20.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 200.f);
}

TEST(Path, GetBounds_QuadCurve) {
  skity::Path path =
      skity::PathBuilder().MoveTo(0, 0).QuadTo(50, 100, 100, 0).Detach();
  auto bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 100.f);
}

TEST(Path, GetBounds_CubicCurve) {
  skity::Path path = skity::PathBuilder()
                         .MoveTo(0, 0)
                         .CubicTo(25, 100, 75, 100, 100, 0)
                         .Detach();
  auto bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 100.f);
}

TEST(Path, GetBounds_NegativeCoordinates) {
  skity::Path path =
      skity::PathBuilder().MoveTo(-50, -100).LineTo(-20, -50).Detach();
  auto bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), -50.f);
  EXPECT_FLOAT_EQ(bounds.Top(), -100.f);
  EXPECT_FLOAT_EQ(bounds.Right(), -20.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), -50.f);
}

TEST(Path, GetBounds_MixedContours) {
  skity::Path path = skity::PathBuilder()
                         .MoveTo(0, 0)
                         .LineTo(50, 50)
                         .Close()
                         .MoveTo(100, 100)
                         .LineTo(200, 150)
                         .Close()
                         .Detach();
  auto bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 200.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 150.f);
}

TEST(Path, GetBounds_ComplexPath) {
  skity::Path path = skity::PathBuilder()
                         .MoveTo(10, 10)
                         .LineTo(50, 10)
                         .QuadTo(100, 100, 150, 50)
                         .CubicTo(200, 0, 250, 100, 300, 50)
                         .Detach();
  auto bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 10.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 300.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 100.f);
}

TEST(Path, GetBounds_Reset) {
  skity::Path path = skity::PathBuilder().MoveTo(0, 0).LineTo(100, 50).Detach();
  path = {};
  auto bounds = path.GetBounds();
  EXPECT_TRUE(bounds.IsEmpty());
}

TEST(Path, GetBounds_CopyWithScale) {
  skity::Path path = skity::PathBuilder()
                         .MoveTo(100, 100)
                         .LineTo(200, 200)
                         .MoveTo(100, 200)
                         .Close()
                         .Detach();
  auto scaled_path = path.CopyWithScale(3);
  auto bounds = scaled_path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 300.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 300.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 600.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 600.f);
}

TEST(Path, GetBounds_CopyWithTransform) {
  skity::Path path = skity::PathBuilder()
                         .MoveTo(100, 100)
                         .LineTo(200, 200)
                         .MoveTo(100, 200)
                         .Close()
                         .Detach();
  auto transform = skity::Matrix::Translate(50, 50);
  auto path1 = path.CopyWithMatrix(transform);
  auto bounds = path1.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 150.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 150.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 250.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 250.f);
}

TEST(Path, GetBounds_ModifyBounds) {
  skity::Path path = skity::PathBuilder()
                         .MoveTo(100, 100)
                         .LineTo(200, 200)
                         .MoveTo(100, 200)
                         .Close()
                         .Detach();
  auto bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 200.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 200.f);

  path = skity::PathBuilder(std::move(path))
             .MoveTo(300, 300)
             .LineTo(400, 400)
             .Close()
             .Detach();
  bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 400.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 400.f);
}

TEST(Path, GetBounds_SetLastPoint) {
  skity::Path path = skity::PathBuilder()
                         .MoveTo(100, 100)
                         .LineTo(200, 200)
                         .LineTo(100, 200)
                         .Close()
                         .Detach();
  auto bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 200.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 200.f);

  path = skity::PathBuilder(std::move(path)).SetLastPt(0, 1).Detach();
  bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 1.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 200.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 200.f);
}

TEST(Path, GetBounds_Swap) {
  skity::Path a = skity::PathBuilder().MoveTo(0, 0).LineTo(100, 200).Detach();
  auto bounds_a = a.GetBounds();
  EXPECT_FLOAT_EQ(bounds_a.Left(), 0.f);
  EXPECT_FLOAT_EQ(bounds_a.Top(), 0.f);
  EXPECT_FLOAT_EQ(bounds_a.Right(), 100.f);
  EXPECT_FLOAT_EQ(bounds_a.Bottom(), 200.f);

  skity::Path b =
      skity::PathBuilder().MoveTo(-100, 50).QuadTo(50, 50, 300, 100).Detach();
  auto bounds_b = b.GetBounds();
  EXPECT_FLOAT_EQ(bounds_b.Left(), -100.f);
  EXPECT_FLOAT_EQ(bounds_b.Top(), 50.f);
  EXPECT_FLOAT_EQ(bounds_b.Right(), 300.f);
  EXPECT_FLOAT_EQ(bounds_b.Bottom(), 100.f);

  std::swap(a, b);
  bounds_a = a.GetBounds();
  EXPECT_FLOAT_EQ(bounds_a.Left(), -100.f);
  EXPECT_FLOAT_EQ(bounds_a.Top(), 50.f);
  EXPECT_FLOAT_EQ(bounds_a.Right(), 300.f);
  EXPECT_FLOAT_EQ(bounds_a.Bottom(), 100.f);

  bounds_b = b.GetBounds();
  EXPECT_FLOAT_EQ(bounds_b.Left(), 0.f);
  EXPECT_FLOAT_EQ(bounds_b.Top(), 0.f);
  EXPECT_FLOAT_EQ(bounds_b.Right(), 100.f);
  EXPECT_FLOAT_EQ(bounds_b.Bottom(), 200.f);
}

TEST(Path, GetBounds_AddPath) {
  skity::Path a = skity::PathBuilder().MoveTo(-1, -1).LineTo(2, 2).Detach();

  auto bounds = a.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), -1.f);
  EXPECT_FLOAT_EQ(bounds.Top(), -1.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 2.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 2.f);

  skity::Path b = skity::PathBuilder().MoveTo(0, 0).QuadTo(1, 1, 3, 3).Detach();

  a = skity::PathBuilder(std::move(a)).AddPath(b).Detach();
  bounds = a.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), -1.f);
  EXPECT_FLOAT_EQ(bounds.Top(), -1.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 3.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 3.f);
}

TEST(Path, GetBounds_MoveTos) {
  skity::Path path;
  // The first MoveTo will be ignored
  path = skity::PathBuilder(std::move(path))
             .MoveTo(30, 30)
             .MoveTo(100, 100)
             .Detach();
  EXPECT_EQ(path.CountPoints(), 1);
  EXPECT_EQ(path.CountVerbs(), 1);

  auto bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 100.f);

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(10, 10)
             .LineTo(20, 20)
             .LineTo(10, 30)
             .MoveTo(50, 60)
             .MoveTo(60, 50)
             .Detach();
  EXPECT_EQ(path.CountPoints(), 4);
  EXPECT_EQ(path.CountVerbs(), 4);
}

TEST(Path, GetBounds_IgnoreLastMoveTo) {
  skity::Path path = skity::PathBuilder().MoveTo(0, 0).LineTo(100, 50).Detach();
  auto bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 50.f);
  path = skity::PathBuilder(std::move(path)).MoveTo(300, 300).Detach();
  bounds = path.GetBounds();
  EXPECT_FLOAT_EQ(bounds.Left(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Top(), 0.f);
  EXPECT_FLOAT_EQ(bounds.Right(), 100.f);
  EXPECT_FLOAT_EQ(bounds.Bottom(), 50.f);
  EXPECT_TRUE(path.IsFinite());

  path = skity::PathBuilder(std::move(path))
             .MoveTo(std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::quiet_NaN())
             .Detach();
  bounds = path.GetBounds();
  EXPECT_TRUE(bounds.IsEmpty());
  EXPECT_FALSE(path.IsFinite());
}

TEST(Path, GetType) {
  skity::Path path;
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kGeneral);

  path =
      skity::PathBuilder(std::move(path)).MoveTo(0, 0).LineTo(10, 10).Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kGeneral);

  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddRect(skity::Rect::MakeLTRB(10, 20, 100, 200))
             .Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kRect);

  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddOval(skity::Rect::MakeLTRB(10, 20, 100, 200))
             .Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kOval);

  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddRoundRect(skity::Rect::MakeLTRB(10, 20, 100, 200), 10, 10)
             .Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kSimpleRRect);

  path = skity::PathBuilder(std::move(path)).SetLastPt(10, 5).Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kGeneral);
}

TEST(Path, Oval) {
  skity::Path path;
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kGeneral);

  path =
      skity::PathBuilder(std::move(path)).MoveTo(0, 0).LineTo(10, 10).Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kGeneral);

  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddRect(skity::Rect::MakeLTRB(10, 20, 100, 200))
             .Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kRect);

  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddOval(skity::Rect::MakeLTRB(10, 20, 100, 200))
             .Detach();

  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kOval);
  skity::Rect oval_rect = path.GetBounds();
  EXPECT_FLOAT_EQ(oval_rect.Left(), 10.f);
  EXPECT_FLOAT_EQ(oval_rect.Top(), 20.f);
  EXPECT_FLOAT_EQ(oval_rect.Right(), 100.f);
  EXPECT_FLOAT_EQ(oval_rect.Bottom(), 200.f);

  path = {};
  path = skity::PathBuilder(std::move(path)).AddCircle(50, 50, 30).Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kOval);
  skity::Rect circle_rect = path.GetBounds();
  EXPECT_FLOAT_EQ(circle_rect.Left(), 20.f);
  EXPECT_FLOAT_EQ(circle_rect.Top(), 20.f);
  EXPECT_FLOAT_EQ(circle_rect.Right(), 80.f);
  EXPECT_FLOAT_EQ(circle_rect.Bottom(), 80.f);

  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(0, 0)
             .LineTo(10, 10)
             .QuadTo(20, 20, 30, 30)
             .Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kGeneral);
}

TEST(Path, IsSimpleRRect) {
  skity::Path path;

  // Empty path should not be RRect
  EXPECT_FALSE(path.IsSimpleRRect(nullptr));

  // Line path should not be RRect
  path =
      skity::PathBuilder(std::move(path)).MoveTo(0, 0).LineTo(10, 10).Detach();
  EXPECT_FALSE(path.IsSimpleRRect(nullptr));

  // Rect path should not be RRect
  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddRect(skity::Rect::MakeLTRB(10, 20, 100, 200))
             .Detach();
  EXPECT_FALSE(path.IsSimpleRRect(nullptr));

  // Oval path should not be RRect
  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddOval(skity::Rect::MakeLTRB(10, 20, 100, 200))
             .Detach();
  EXPECT_FALSE(path.IsSimpleRRect(nullptr));

  // RoundRect path should be RRect
  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddRoundRect(skity::Rect::MakeLTRB(10, 20, 100, 200), 10, 10)
             .Detach();
  skity::RRect rrect;
  EXPECT_TRUE(path.IsSimpleRRect(&rrect));
  EXPECT_FLOAT_EQ(rrect.GetRect().Left(), 10.f);
  EXPECT_FLOAT_EQ(rrect.GetRect().Top(), 20.f);
  EXPECT_FLOAT_EQ(rrect.GetRect().Right(), 100.f);
  EXPECT_FLOAT_EQ(rrect.GetRect().Bottom(), 200.f);
  EXPECT_FLOAT_EQ(rrect.GetSimpleRadii().x, 10.f);
  EXPECT_FLOAT_EQ(rrect.GetSimpleRadii().y, 10.f);

  // Complex path should not be RRect
  path = {};
  path = skity::PathBuilder(std::move(path))
             .MoveTo(0, 0)
             .LineTo(10, 10)
             .QuadTo(20, 20, 30, 30)
             .Detach();
  EXPECT_FALSE(path.IsSimpleRRect(nullptr));
}

TEST(Path, GetType_AfterModifications) {
  skity::Path path;

  // Start with rect
  path = skity::PathBuilder(std::move(path))
             .AddRect(skity::Rect::MakeLTRB(10, 20, 100, 200))
             .Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kRect);

  // Add more operations - should become general
  path = skity::PathBuilder(std::move(path)).LineTo(150, 250).Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kGeneral);

  // Reset and create oval
  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddOval(skity::Rect::MakeLTRB(10, 20, 100, 200))
             .Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kOval);

  // Reset and create rrect
  path = {};
  path = skity::PathBuilder(std::move(path))
             .AddRoundRect(skity::Rect::MakeLTRB(10, 20, 100, 200), 10, 10)
             .Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kSimpleRRect);

  // After reset, should be general
  path = {};
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kGeneral);

  path =
      skity::PathBuilder(std::move(path)).MoveTo(5, 5).LineTo(10, 10).Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kGeneral);
  path = skity::PathBuilder(std::move(path))
             .AddRoundRect(skity::Rect::MakeLTRB(10, 20, 100, 200), 10, 10)
             .Detach();
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kGeneral);
}

TEST(PathBuilder, ResetClearsStateAndRetainsStorage) {
  skity::PathBuilder builder;
  builder.MoveTo(10, 20)
      .LineTo(30, 40)
      .QuadTo(50, 60, 70, 80)
      .ConicTo(90, 100, 110, 120, 0.5f)
      .CubicTo(130, 140, 150, 160, 170, 180)
      .Close()
      .MoveTo(200, 210)
      .SetFillType(skity::Path::PathFillType::kEvenOdd);
  EXPECT_FALSE(builder.GetBounds().IsEmpty());
  skity::PathPriv::SetConvexityType(builder,
                                    skity::Path::ConvexityType::kConcave);
  skity::Path path = builder.Detach();
  const auto* points = path.Points();
  const auto* verbs = path.VerbsBegin();
  const auto* weights = path.ConicWeights();

  builder = skity::PathBuilder(std::move(path));
  builder.Reset();
  path = builder.Snapshot();
  skity::Path empty;
  EXPECT_TRUE(path.IsEmpty());
  EXPECT_EQ(path.CountPoints(), 0u);
  EXPECT_EQ(path.CountVerbs(), 0u);
  EXPECT_EQ(path.GetFillType(), empty.GetFillType());
  EXPECT_EQ(path.GetFirstDirection(), empty.GetFirstDirection());
  EXPECT_EQ(path.GetConvexityType(), empty.GetConvexityType());
  EXPECT_EQ(path.IsFinite(), empty.IsFinite());
  EXPECT_EQ(path.GetBounds(), empty.GetBounds());
  EXPECT_EQ(path.GetSegmentMasks(), 0u);
  EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kGeneral);
  EXPECT_FALSE(path.Contains(20, 30));
  skity::Point pts[4];
  EXPECT_EQ(skity::Path::RawIter(path).Next(pts), skity::Path::Verb::kDone);

  // An implicit move must start at the origin, not the previous contour.
  path = builder.ConicTo(2, 4, 6, 8, 0.75f).Close().Detach();
  skity::Path::RawIter iter(path);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kMove);
  EXPECT_EQ(pts[0], skity::Point(0, 0, 0, 1));
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kConic);
  EXPECT_FLOAT_EQ(iter.ConicWeight(), 0.75f);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kClose);
  EXPECT_EQ(iter.Next(pts), skity::Path::Verb::kDone);
  EXPECT_EQ(path.GetBounds(), skity::Rect::MakeLTRB(0, 0, 6, 8));
  EXPECT_EQ(path.GetSegmentMasks(), skity::Path::SegmentMask::kConic);
  EXPECT_EQ(path.Points(), points);
  EXPECT_EQ(path.VerbsBegin(), verbs);
  EXPECT_EQ(path.ConicWeights(), weights);
}

TEST(PathBuilder, ResetClearsNonFiniteAndSimpleShapeState) {
  skity::PathBuilder builder;
  skity::Path path = builder.LineTo(1, 1)
                         .MoveTo(std::numeric_limits<float>::infinity(), 1)
                         .Detach();
  EXPECT_FALSE(path.IsFinite());
  path = builder.Reset().Snapshot();
  EXPECT_TRUE(path.IsFinite());
  EXPECT_TRUE(path.GetBounds().IsEmpty());

  for (auto type : {skity::Path::IsAType::kRect, skity::Path::IsAType::kOval,
                    skity::Path::IsAType::kSimpleRRect}) {
    const auto bounds = skity::Rect::MakeLTRB(10, 20, 110, 220);
    if (type == skity::Path::IsAType::kRect) {
      path = builder.AddRect(bounds).Detach();
    } else if (type == skity::Path::IsAType::kOval) {
      path = builder.AddOval(bounds).Detach();
    } else {
      path = builder.AddRoundRect(bounds, 5, 10).Detach();
    }
    ASSERT_EQ(path.GetIsAType(), type);
    EXPECT_EQ(path.GetBounds(), bounds);
    path = builder.Reset().Snapshot();
    EXPECT_EQ(path.GetIsAType(), skity::Path::IsAType::kGeneral);
    EXPECT_FALSE(path.IsSimpleRRect(nullptr));
    EXPECT_FALSE(path.IsRect(nullptr));
    EXPECT_TRUE(path.GetBounds().IsEmpty());
  }
  path =
      builder.AddRoundRect(skity::Rect::MakeLTRB(0, 0, 40, 60), 2, 3).Detach();
  skity::RRect rrect;
  ASSERT_TRUE(path.IsSimpleRRect(&rrect));
  EXPECT_EQ(rrect.GetSimpleRadii(), skity::Vec2(2, 3));
  EXPECT_TRUE(path.Contains(20, 30));
}
