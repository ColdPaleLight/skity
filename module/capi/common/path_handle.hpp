// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef MODULE_CAPI_COMMON_PATH_HANDLE_HPP
#define MODULE_CAPI_COMMON_PATH_HANDLE_HPP

#include <skity/c/skity_path.h>

#include <skity/graphic/path_builder.hpp>
#include <utility>

#include "handle.hpp"
#include "src/graphic/path_priv.hpp"

namespace skity::capi {

inline Matrix path_matrix(const skity_matrix& matrix) {
  const float* m = matrix.m;
  return {m[0], m[1], m[2],  m[3],  m[4],  m[5],  m[6],  m[7],
          m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]};
}

inline skity_path_s* resolve_path(skity_path path) {
  return resolve<skity_path_s>(path, SKITY_OBJECT_TYPE_PATH);
}

inline Path* read_path(skity_path_s* path) {
  if (!path || !path->impl) {
    return nullptr;
  }
  if (path->editing) {
    path->editor->DetachTo(path->impl.get());
    path->editing = false;
  }
  return path->impl.get();
}

inline Path* read_path_handle(skity_path path) {
  return read_path(resolve_path(path));
}

inline Path* writable_path(skity_path_s* path) {
  auto* value = read_path(path);
  if (value && !(path->header.flags & SKITY_HANDLE_OWNING) &&
      !path->private_view) {
    // A borrowed C handle may edit its own view of an immutable native Path.
    path->impl = std::make_shared<Path>(*value);
    path->private_view = true;
    return path->impl.get();
  }
  return value;
}

inline PathBuilder* edit_path(skity_path_s* path) {
  if (!path || !path->impl) {
    return nullptr;
  }
  if (!path->editing) {
    auto* value = writable_path(path);
    auto hint = PathPriv::GetConvexityHint(*value);
    if (!path->editor) {
      path->editor = std::make_shared<PathBuilder>(std::move(*value));
    } else if (value->IsEmpty()) {
      path->editor->Reset().SetFillType(value->GetFillType());
      *value = {};
    } else {
      *path->editor = PathBuilder(std::move(*value));
    }
    PathPriv::SetConvexityType(*path->editor, hint);
    path->editing = true;
  }
  return path->editor.get();
}

inline PathBuilder* edit_path_handle(skity_path path) {
  return edit_path(resolve_path(path));
}

}  // namespace skity::capi

#endif  // MODULE_CAPI_COMMON_PATH_HANDLE_HPP
