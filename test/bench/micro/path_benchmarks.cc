// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <benchmark/benchmark.h>

#include <skity/graphic/path_builder.hpp>

namespace {

template <typename Builder>
void BuildPath(Builder& builder, int count) {
  builder.MoveTo(0, 0);
  for (int i = 1; i < count; ++i) {
    builder.LineTo(i, i % 7);
  }
}

void BM_PathEmpty(benchmark::State& state) {
  for (auto _ : state) {
    skity::Path path;
    benchmark::DoNotOptimize(path);
  }
}
BENCHMARK(BM_PathEmpty)->Threads(1)->Threads(8);

void BM_PathBuild(benchmark::State& state) {
  for (auto _ : state) {
    skity::Path path;
    BuildPath(path, state.range(0));
    benchmark::DoNotOptimize(path);
  }
}
BENCHMARK(BM_PathBuild)->Arg(4)->Arg(64)->Threads(1)->Threads(8);

void BM_PathCopy(benchmark::State& state) {
  skity::Path source;
  BuildPath(source, state.range(0));
  for (auto _ : state) {
    skity::Path copy(source);
    benchmark::DoNotOptimize(copy);
  }
}
BENCHMARK(BM_PathCopy)->Arg(4)->Arg(64)->Threads(1)->Threads(8);

void BM_PathResetBuildAndCopy(benchmark::State& state) {
  skity::Path source;
  BuildPath(source, state.range(0));
  for (auto _ : state) {
    source.Reset();
    BuildPath(source, state.range(0));
    skity::Path copy(source);
    benchmark::DoNotOptimize(copy);
  }
}
BENCHMARK(BM_PathResetBuildAndCopy)->Arg(4)->Arg(64)->Threads(1)->Threads(8);

void BM_PathBuilderDetach(benchmark::State& state) {
  skity::PathBuilder builder;
  for (auto _ : state) {
    BuildPath(builder, state.range(0));
    auto path = builder.Detach();
    benchmark::DoNotOptimize(path);
  }
}
BENCHMARK(BM_PathBuilderDetach)->Arg(4)->Arg(64)->Threads(1)->Threads(8);

void BM_PathBuilderSnapshot(benchmark::State& state) {
  skity::PathBuilder builder;
  BuildPath(builder, state.range(0));
  for (auto _ : state) {
    builder.Reset();
    BuildPath(builder, state.range(0));
    auto path = builder.Snapshot();
    benchmark::DoNotOptimize(path);
  }
}
BENCHMARK(BM_PathBuilderSnapshot)->Arg(4)->Arg(64)->Threads(1)->Threads(8);

void BM_PathAppendRetainedCapacity(benchmark::State& state) {
  skity::Path source;
  BuildPath(source, state.range(0));
  source.Reset();
  BuildPath(source, 8);
  const skity::Path copy(source);
  for (auto _ : state) {
    skity::Path path;
    path.AddPath(copy);
    benchmark::DoNotOptimize(path);
  }
}
BENCHMARK(BM_PathAppendRetainedCapacity)->Arg(8)->Arg(65536);

}  // namespace
