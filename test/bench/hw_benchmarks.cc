// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <benchmark/benchmark.h>

#include <skity/skity.hpp>

#include "case/basic/example.hpp"
#include "test/bench/common/context.hpp"
#include "test/bench/common/target.hpp"

static void BM_HWExamplePremulAlpha(benchmark::State& state) {
  auto context =
      skity::bench::Context::Create(skity::bench::RenderingType::kOpenGL);
  skity::bench::Target::Options options;
  options.width = 1024;
  options.height = 1024;
  options.msaa = false;

  auto target = context->CreateTarget(options);
  for (auto _ : state) {
    auto canvas = target->LockCanvas();
    skity::example::basic::draw_canvas(canvas);
    target->Flush();
  }
  context->WriteToFile(target, "test1.png");
}
BENCHMARK(BM_HWExamplePremulAlpha)->Unit(benchmark::kMicrosecond);
