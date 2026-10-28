// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <skity/graphic/path_builder.hpp>

namespace skity {

PathBuilder::PathBuilder(const Path& path) : path_(path) {
  if (path_.IsEmpty()) {
    path_.ReplaceData(Path::EmptyData());
  } else {
    path_.EnsureUnique();
  }
}

PathBuilder::PathBuilder(const PathBuilder& other) : PathBuilder(other.path_) {}

PathBuilder& PathBuilder::operator=(const PathBuilder& other) {
  if (this != &other) {
    PathBuilder copy(other);
    path_.Swap(copy.path_);
  }
  return *this;
}

Path PathBuilder::Detach() {
  Path result;
  result.Swap(path_);
  return result;
}

Path PathBuilder::Snapshot() const {
  Path result(path_);
  if (result.IsEmpty()) {
    result.ReplaceData(Path::EmptyData());
  } else {
    result.EnsureUnique();
  }
  return result;
}

}  // namespace skity
