// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef SRC_GRAPHIC_PATH_DATA_HPP
#define SRC_GRAPHIC_PATH_DATA_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <skity/graphic/path_builder.hpp>
#include <vector>

namespace skity {

// Typical lines, rectangles, and ovals fit in the same allocation as Data.
template <typename T, size_t N>
class PathArray {
 public:
  PathArray() = default;
  PathArray(const PathArray& other) {
    reserve(other.size_);
    Append(other);
  }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  T* data() { return heap_ ? heap_.get() : inline_.data(); }
  const T* data() const { return heap_ ? heap_.get() : inline_.data(); }
  T* begin() { return data(); }
  T* end() { return data() + size_; }
  const T* begin() const { return data(); }
  const T* end() const { return data() + size_; }

  T& operator[](size_t index) { return data()[index]; }
  const T& operator[](size_t index) const { return data()[index]; }
  const T& back() const { return data()[size_ - 1]; }

  void reserve(size_t count) {
    if (count > capacity_) {
      std::unique_ptr<T[]> next(new T[count]);
      std::copy_n(data(), size_, next.get());
      heap_ = std::move(next);
      capacity_ = count;
    }
  }

  void push_back(T value) {
    EnsureSpace(1);
    data()[size_++] = value;
  }

  void Append(const PathArray& src) { Append(src.data(), src.size_); }

  void Append(const T* src, size_t count) {
    EnsureSpace(count);
    std::copy_n(src, count, data() + size_);
    size_ += count;
  }

  void clear() { size_ = 0; }

  bool operator==(const PathArray& other) const {
    return std::equal(begin(), end(), other.begin(), other.end());
  }

 private:
  void EnsureSpace(size_t count) {
    if (count > capacity_ - size_) {
      reserve(std::max(capacity_ * 2, size_ + count));
    }
  }

  std::unique_ptr<T[]> heap_;
  std::array<T, N> inline_{};
  size_t size_ = 0;
  size_t capacity_ = N;
};

struct Path::Data : Storage {
  static constexpr Vec2 kDefaultZW{0.f, 1.f};

  Data() = default;
  Data(const Data& other)
      : points_(other.points_),
        verbs_(other.verbs_),
        conic_weights_(other.conic_weights_),
        zw_(other.zw_),
        last_move_to_index_(other.last_move_to_index_),
        first_direction_(other.first_direction_),
        segment_masks_(other.segment_masks_),
        type_(other.type_),
        radii_(other.radii_) {
    if (other.bounds_state_.load(std::memory_order_acquire) == kReady) {
      bounds_ = other.bounds_;
    } else {
      bounds_state_.store(kDirty, std::memory_order_relaxed);
    }
  }

  Point Point4(size_t index) const {
    return {points_[index], zw_.empty() ? kDefaultZW : zw_[index]};
  }

  void AddPoint(Vec2 point) {
    points_.push_back(point);
    if (!zw_.empty()) {
      zw_.push_back(kDefaultZW);
    }
  }

  template <size_t N>
  void AddPoints(const Vec2 (&points)[N]) {
    points_.Append(points, N);
    if (!zw_.empty()) {
      zw_.resize(points_.size(), kDefaultZW);
    }
  }

  void SetPoint(size_t index, const Point& point) {
    Vec2 zw{point.z, point.w};
    if (zw_.empty() && zw != kDefaultZW) {
      zw_.resize(points_.size(), kDefaultZW);
    }
    points_[index] = Vec2{point};
    if (!zw_.empty()) {
      zw_[index] = zw;
    }
  }

  void SetPoint2D(size_t index, Vec2 point) {
    points_[index] = point;
    if (!zw_.empty()) {
      zw_[index] = kDefaultZW;
    }
  }

  void MarkBoundsDirty() {
    bounds_state_.store(kDirty, std::memory_order_relaxed);
  }

  void CanonicalizeZW() {
    if (!zw_.empty() && std::all_of(zw_.begin(), zw_.end(),
                                    [](Vec2 zw) { return zw == kDefaultZW; })) {
      zw_.clear();
    }
  }

  struct Bounds {
    Rect rect;
    bool finite = true;
  };

  Bounds GetBounds() const;
  Bounds ComputeBounds() const;

  void Reset() {
    points_.clear();
    verbs_.clear();
    conic_weights_.clear();
    zw_.clear();
    last_move_to_index_ = ~0;
    first_direction_ = Direction::kCCW;
    bounds_ = {};
    bounds_state_.store(kReady, std::memory_order_relaxed);
    segment_masks_ = 0;
    type_ = IsAType::kGeneral;
    radii_ = {};
    convexity_cache_.store(0, std::memory_order_relaxed);
  }

  PathArray<Vec2, 9> points_;
  PathArray<Verb, 8> verbs_;
  PathArray<float, 4> conic_weights_;
  // The paired z/w values are allocated only after a non-planar transform.
  std::vector<Vec2> zw_;

  int32_t last_move_to_index_ = ~0;
  Direction first_direction_ = Direction::kCCW;

  uint32_t segment_masks_ = 0;
  IsAType type_ = IsAType::kGeneral;
  Vec2 radii_;

  // Zero means uncomputed. Both results are published in one atomic store.
  mutable std::atomic<uint8_t> convexity_cache_{0};

 private:
  enum BoundsState : uint8_t { kDirty, kPublishing, kReady };
  mutable Bounds bounds_;
  mutable std::atomic<BoundsState> bounds_state_{kReady};
};

inline Path::Data* Path::StorageRef::operator->() const {
  return static_cast<Data*>(storage_);
}

inline Path::Data& Path::StorageRef::operator*() const { return *operator->(); }

inline Path::Data& PathBuilder::Edit() {
  if (!data_) {
    data_ = Path::StorageRef(new Path::Data);
  }
  return *data_;
}

}  // namespace skity

#endif  // SRC_GRAPHIC_PATH_DATA_HPP
