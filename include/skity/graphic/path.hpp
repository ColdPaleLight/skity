// Copyright 2006 The Android Open Source Project

// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef INCLUDE_SKITY_GRAPHIC_PATH_HPP
#define INCLUDE_SKITY_GRAPHIC_PATH_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <skity/geometry/matrix.hpp>
#include <skity/geometry/point.hpp>
#include <skity/geometry/rect.hpp>
#include <skity/geometry/rrect.hpp>
#include <skity/macros.hpp>
#include <tuple>
#include <utility>

namespace skity {

/** @brief An immutable path value with shared geometry. */
class SKITY_API Path {
 public:
  enum class AddMode {
    // append to destination unaltered
    kAppend,
    // add line if prior contour is not closed
    kExtend,
  };

  enum class ConvexityType {
    kUnknown,
    kConvex,
    kConcave,
  };

  enum class Direction {
    // clockwise direction for adding closed contours
    kCW,
    // counter-clockwise direction for adding closed contours
    kCCW,
    kUnknown,
  };

  enum class Verb : uint8_t {
    // Path move command, iter.next returns 1 point
    kMove,
    // Path LineTo command, iter.next returns 2 points
    kLine,
    // Path QuadTo command, iter.next retruns 3 points
    kQuad,
    // Path ConicTo command, iter.next return 3 points + iter.conicWeight()
    kConic,
    // Path CubicTo command, iter.next return 4 points
    kCubic,
    // Path close command, iter.next return 1 points
    kClose,
    // iter.next return 0 points
    kDone,
  };

  enum class PathFillType {
    // "inside" is computed by a non-zero sum of signed edge crossings
    kWinding,
    // "inside" is computed by an odd number of edge crossings
    kEvenOdd,
  };

  enum class IsAType {
    kGeneral,
    kRect,
    kOval,
    kSimpleRRect,
  };

  class Iter {
   public:
    /**
     * @brief Create empty Path::Iter
     *        Call setPath to initialize Path::iter at a later time.
     */
    Iter();
    /**
     * @brief             Create Path::Iter with given path object. And
     *                    indicate whether force to close this path.
     *
     * @param path        path tobe iterated
     * @param forceClose  insert close command if need
     */
    Iter(Path const& path, bool forceClose);

    ~Iter() = default;

    void SetPath(Path const& path, bool forceClose);

    /**
     * @brief         Returns next Path::Verb in verb array, and advances
     *                Path::Iter
     *
     * @param pts     storage for Point data describing returned Path::Verb
     * @return Verb   next Path::Verb from verb array
     */
    Verb Next(Point pts[4]);

    /**
     * @brief Returns the next Path::Verb and advances the iterator, copying
     *        x/y without perspective division. The Path must remain alive
     *        and unmodified while iterating.
     *
     * @param pts   storage for 4 Vec2s. Writes 1/2/3/3/4 points for
     *              Move/Line/Quad/Conic/Cubic, including the start point.
     *              Close and Done leave pts unchanged.
     * @return      next Path::Verb from verb array
     */
    Verb Next(Vec2 pts[4]);

    /**
     * @brief         Retruns conic weight if next() returned Verb::kConic
     *
     * @return float  conic weight for conic Point returned by next()
     */
    float ConicWeight() const;

    /**
     * @brief         Returns true if last kLine returned by next() was
     *                genearted by kClose.
     *
     * @return true   last kLine was gererated by kClose.
     * @return false  otherwise
     */
    bool IsCloseLine() const;

    bool IsClosedContour() const;

   private:
    friend class PathPriv;

    template <typename P>
    Verb NextImpl(P pts[4]);
    template <typename P>
    Verb AutoClose(P pts[2]);
    const Vec2* ConsMoveTo();

   private:
    const Vec2* pts_;
    const Vec2* points_begin_;
    const Vec2* zw_;
    const Verb* verbs_;
    const Verb* verb_stop_;
    const float* conic_weights_;
    bool force_close_;
    bool need_close_;
    bool close_line_;
    const Vec2* move_to_;
    const Vec2* last_pt_;
    enum class SegmentState {
      /**
       * @brief The current contour is empty. Starting processing or have just
       * closed a contour.
       */
      kEmptyContour,
      /**
       * @brief Have seen a move, but nothing else
       */
      kAfterMove,
      /**
       * @brief Have seen a primitive but not yet closed the path. Also the
       * initial state.
       */
      kAfterPrimitive,
    };
    SegmentState segment_state_;
  };

  class RawIter {
   public:
    RawIter();
    explicit RawIter(Path const& path) : RawIter() { SetPath(path); }
    ~RawIter() = default;

    void SetPath(Path const& path);

    Verb Next(Point pts[4]);
    /**
     * @brief Returns the next Path::Verb and advances the iterator without
     *        synthesizing closing lines. Follows Iter::Next(Vec2*) for
     *        output and Path lifetime requirements.
     *
     * @param pts   storage for 4 Vec2s describing the returned Path::Verb
     * @return      next Path::Verb from verb array
     */
    Verb Next(Vec2 pts[4]);
    Verb Peek() const;
    float ConicWeight() const;

   private:
    template <typename P>
    Verb NextImpl(P pts[4]);

    const Vec2* pts_;
    const Vec2* points_begin_;
    const Vec2* zw_;
    const Verb* verbs_;
    const Verb* verb_stop_;
    const float* conic_weights_;
  };

  class RangeIter final {
   public:
    RangeIter() = default;
    RangeIter(const Verb* verbs, const Vec2* points, const float* weights)
        : verb_(verbs), points_(points), weights_(weights) {}

    bool operator!=(RangeIter const& other) const {
      return verb_ != other.verb_;
    }

    bool operator==(RangeIter const& other) const {
      return verb_ == other.verb_;
    }

    RangeIter& operator++() {
      auto verb = *verb_++;
      points_ += pts_advance_after_verb(verb);
      if (verb == Verb::kConic) {
        ++weights_;
      }
      return *this;
    }

    RangeIter operator++(int) {
      RangeIter copy = *this;
      this->operator++();
      return copy;
    }

    std::tuple<Verb, const Vec2*, const float*> operator*() const {
      Verb verb = this->PeekVerb();
      int backset = pts_backset_for_verb(verb);
      return {verb, points_ + backset, weights_};
    }

    Verb PeekVerb() const { return *verb_; }

    /**
     * @brief Reads x/y from the current segment without advancing.
     *        Only valid for Move, Line, Quad, Conic, or Cubic.
     *
     * @param index   valid point index within the current segment, including
     *                its start point
     * @return        point in path coordinates without perspective division
     */
    Vec2 GetPoint2D(int index) const {
      return points_[pts_backset_for_verb(PeekVerb()) + index];
    }

    /**
     * @brief Returns the current conic weight. Only valid when PeekVerb()
     *        is kConic.
     *
     * @return conic weight for the current segment
     */
    float ConicWeight() const { return *weights_; }

   private:
    constexpr static int pts_advance_after_verb(Verb verb) {
      switch (verb) {
        case Verb::kMove:
          return 1;
        case Verb::kLine:
          return 1;
        case Verb::kQuad:
          return 2;
        case Verb::kConic:
          return 2;
        case Verb::kCubic:
          return 3;
        case Verb::kClose:
          return 0;
        default:
          break;
      }
      // can not reach here
      return 0;
    }

    constexpr static int pts_backset_for_verb(Verb verb) {
      switch (verb) {
        case Verb::kMove:
          return 0;
        case Verb::kLine:
          return -1;
        case Verb::kQuad:
          return -1;
        case Verb::kConic:
          return -1;
        case Verb::kCubic:
          return -1;
        case Verb::kClose:
          return -1;
        default:
          break;
      }
      // can not reach here
      return 0;
    }

   private:
    const Verb* verb_ = nullptr;
    const Vec2* points_ = nullptr;
    const float* weights_ = nullptr;
  };

  Path() = default;
  Path(const Path&) = default;
  Path(Path&&) noexcept = default;
  Path& operator=(const Path&) = default;
  Path& operator=(Path&&) noexcept = default;

  size_t CountPoints() const;
  size_t CountVerbs() const;

  enum ArcSize {
    kSmall,
    kLarge,
  };

  bool GetLastPt(Point* lastPt) const;
  Point GetPoint(int index) const;
  Path::Verb GetVerb(int index) const;
  Point GetLastMovePt() const;

  /**
   * @brief Returns x/y of the indexed point without perspective division.
   *
   * @param index   point index in Path
   * @return        point in path coordinates, or (0, 0) if index is negative
   *                or outside CountPoints()
   */
  Vec2 GetPoint2D(int index) const;

  /**
   * @brief Copies x/y of the last point without perspective division.
   *
   * @param last_pt   optional output for the last point in path coordinates;
   *                  receives (0, 0) if Path is empty
   * @return          true if Path contains a point, false otherwise
   */
  bool GetLastPt2D(Vec2* last_pt) const;

  /**
   * @brief Returns x/y of the last move point without perspective division.
   *
   * @return point in path coordinates, or (0, 0) if Path is empty
   */
  Vec2 GetLastMovePt2D() const;

  /**
   * @brief Copies x/y of Path points without perspective division.
   *
   * @param dst       output storage, or nullptr to query CountPoints()
   * @param capacity  maximum number of points to copy into dst
   * @return          number of points copied, or CountPoints() if dst is null
   */
  size_t CopyPoints2D(Vec2* dst, size_t capacity) const;
  /**
   * Returns true for finite Point array values between negative float and
   * positive float. Returns false for any Point array value of FloatInfinity
   * value or FloatNaN.
   *
   * @return true if all Point values are finite
   */
  bool IsFinite() const;
  /**
   * Returns true if Path contains only one line;
   * @param line    if Path contains one line and line is not nullptr, line is
   *                set to this array
   * @return        true if Path contains exactly one line
   */
  bool IsLine(Point line[2]) const;
  /**
   * @brief Returns true if Path contains a single Move followed by a Line.
   *
   * @param line    optional storage for x/y of the two endpoints without
   *                perspective division; unchanged on failure
   * @return        true if Path contains exactly one line
   */
  bool IsLine2D(Vec2 line[2]) const;
  bool IsEmpty() const { return !data_; }

  bool operator==(const Path& other) const;
  bool operator!=(const Path& other) const { return !(*this == other); }
  Direction GetFirstDirection() const;

  Path WithFillType(PathFillType type) const;

  /**
   * @brief Returns PathFillType, the rule used to fill Path.
   *
   * @return PathFillType
   */
  PathFillType GetFillType() const { return fill_type_; }

  /**
   * Returns true if Path is equivalent to Rect when filled.
   * If false: rect, is_closed and direction are unchanged.
   * If true: rect, is_closed and direction are written to if not nullptr.
   *
   * @param rect        storage for bounds of Rect; may be nullptr
   * @param is_closed   storage set to true if Path is closed; may be nullptr.
   * @param direction   storage set to Rect direction; may be nullptr.
   * @return true       Path is equivalent to rect
   */
  bool IsRect(Rect* rect, bool* is_closed = nullptr,
              Direction* direction = nullptr) const;

  /**
   * Returns true if Path is equivalent to simple RRect (round rect) when
   * filled. If false: rrect are unchanged. If true: rrect are written to if not
   * nullptr.
   *
   * @param rrect       storage for RRect; may be nullptr
   * @return true       Path is equivalent to rrect
   */
  bool IsSimpleRRect(RRect* rrect) const;

  /**
   * Returns the IsAType of the Path.
   *
   * @return IsAType    the type of the Path
   */
  IsAType GetIsAType() const;

  /**
   * Returns true if point(x, y) is contained by Path. This taking PathFillType
   * into account.
   *
   * @param x       x-axis value of containment test
   * @param y       y-axis value of containment test
   * @return        true if Path contains this point(x, y)
   */
  bool Contains(float x, float y) const;

  Rect GetBounds() const;
  void Dump() const;

  const Verb* VerbsBegin() const;
  const Verb* VerbsEnd() const;
  const Vec2* Points() const;
  const float* ConicWeights() const;
  size_t CountConicWeights() const;

  /**
   * @internal
   * @param matrix
   * @return
   */
  Path CopyWithMatrix(Matrix const& matrix) const;
  Path CopyWithScale(float scale) const;

  ConvexityType GetConvexityType() const;
  bool IsConvex() const { return GetConvexityType() == ConvexityType::kConvex; }

  struct SegmentMask {
    enum Value : uint32_t {
      kLine = 0x0001,
      kQuad = 0x0002,
      kConic = 0x0004,
      kCubic = 0x0008,
    };
  };

  /**
   * Returns a mask, where each set bit corresponds to a SegmentMask constant if
   * Path contains one or more verbs of that type. Returns zero if Path contains
   * no lines, or curves: quads, conics, or cubics.
   *
   * GetSegmentMasks() returns a cached result; it is very fast.
   * @return  SegmentMask bits or zero
   */
  uint32_t GetSegmentMasks() const;

 private:
  friend class PathBuilder;
  friend class PathPriv;

  struct Data;
  struct Storage {
    std::atomic<size_t> refs{1};
    virtual ~Storage() = default;
  };

  class StorageRef {
   public:
    StorageRef() = default;
    explicit StorageRef(Storage* storage) : storage_(storage) {}
    StorageRef(const StorageRef& other) : storage_(other.storage_) {
      if (storage_) {
        storage_->refs.fetch_add(1, std::memory_order_relaxed);
      }
    }
    StorageRef(StorageRef&& other) noexcept
        : storage_(std::exchange(other.storage_, nullptr)) {}
    StorageRef& operator=(StorageRef other) noexcept {
      Swap(other);
      return *this;
    }
    ~StorageRef() {
      if (storage_ &&
          storage_->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete storage_;
      }
    }

    explicit operator bool() const { return storage_ != nullptr; }
    bool operator==(const StorageRef& other) const {
      return storage_ == other.storage_;
    }
    bool IsUnique() const {
      return storage_ && storage_->refs.load(std::memory_order_acquire) == 1;
    }
    void Swap(StorageRef& other) noexcept {
      std::swap(storage_, other.storage_);
    }
    Data* operator->() const;
    Data& operator*() const;

   private:
    Storage* storage_ = nullptr;
  };

  Path(StorageRef data, PathFillType fill_type, ConvexityType convexity_hint);
  const Data& GetData() const;
  ConvexityType ComputeConvexity(Direction* direction) const;
  int LeadingMoveToCount() const;

  // Only PathBuilder can edit Data, and only while it owns it exclusively.
  StorageRef data_;
  PathFillType fill_type_ = PathFillType::kWinding;
  ConvexityType convexity_hint_ = ConvexityType::kUnknown;
};

}  // namespace skity

#endif  // INCLUDE_SKITY_GRAPHIC_PATH_HPP
