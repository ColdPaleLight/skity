// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <skity/c/skity_path_op.h>

#include <skity/graphic/path.hpp>
#include <skity/graphic/path_op.hpp>

#include "path_handle.hpp"

namespace {

const skity::Path* path_of(skity_path handle) {
  return skity::capi::read_path_handle(handle);
}

}  // namespace

extern "C" {

uint32_t skity_path_op_execute(skity_path one, skity_path two, skity_path_op op,
                               skity_path result) {
  auto* a = path_of(one);
  auto* b = path_of(two);
  auto* r = skity::capi::writable_path(skity::capi::resolve_path(result));
  if (a == nullptr || b == nullptr || r == nullptr) {
    return 0u;
  }
  return skity::PathOp::Execute(*a, *b, static_cast<skity::PathOp::Op>(op), r)
             ? 1u
             : 0u;
}

}  // extern "C"
