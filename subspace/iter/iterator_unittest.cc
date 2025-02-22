// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "subspace/iter/iterator.h"

#include "subspace/iter/empty.h"
#include "googletest/include/gtest/gtest.h"
#include "subspace/assertions/unreachable.h"
#include "subspace/construct/into.h"
#include "subspace/containers/array.h"
#include "subspace/containers/vec.h"
#include "subspace/iter/filter.h"
#include "subspace/macros/__private/compiler_bugs.h"
#include "subspace/prelude.h"

using sus::containers::Array;
using sus::fn::Fn;
using sus::iter::IteratorBase;
using sus::iter::IteratorImpl;
using sus::option::Option;

namespace {

// clang-format off
static_assert(
  sus::iter::Iterator<sus::iter::BoxedIterator<int, 8, 8>, int>);
static_assert(
  ::sus::iter::Iterator<sus::iter::Empty<int>, int>);
static_assert(
  sus::iter::Iterator<sus::iter::Filter<int, 8, 8>, int>);
static_assert(
  sus::iter::Iterator<sus::iter::Map<int, int, 8, 8>, int>);
static_assert(
  ::sus::iter::Iterator<sus::iter::Once<int>, int>);
static_assert(
  sus::iter::Iterator<sus::containers::ArrayIntoIter<int, 1>, int>);
static_assert(
  sus::iter::Iterator<sus::containers::VecIntoIter<int>, int>);
static_assert(
  sus::iter::Iterator<sus::containers::SliceIter<const int&>, const int&>);
static_assert(
  sus::iter::Iterator<sus::containers::SliceIterMut<int&>, int&>);
// clang-format on

template <class Item, size_t N>
class ArrayIterator final : public IteratorImpl<ArrayIterator<Item, N>, Item> {
 public:
  static ArrayIterator with_array(Item (&items)[N]) noexcept {
    return ArrayIterator(items);
  }

  Option<Item> next() noexcept final {
    if (++(index_) < N) {
      return items_[index_].take();
    } else {
      --(index_);
      return Option<Item>::none();
    }
  }

 protected:
  ArrayIterator(Item (&items)[N])
      : items_(Array<Option<Item>, N>::with_initializer(
            [&items, i = 0]() mutable -> Option<Item> {
              return Option<Item>::some(sus::move(items[i++]));
            })) {}

 private:
  size_t index_ = static_cast<size_t>(-1);
  Array<Option<Item>, N> items_;

  sus_class_trivially_relocatable_if_types(unsafe_fn, decltype(index_),
                                           decltype(items_));
};

static_assert(sus::iter::Iterator<ArrayIterator<int, 1>, int>);

template <class Item>
class EmptyIterator final : public IteratorImpl<EmptyIterator<Item>, Item> {
 public:
  EmptyIterator() {}

  Option<Item> next() noexcept final { return Option<Item>::none(); }
};

TEST(IteratorBase, ForLoop) {
  int nums[5] = {1, 2, 3, 4, 5};

  auto it_lvalue = ArrayIterator<int, 5>::with_array(nums);
  int count = 0;
  for (auto i : it_lvalue) {
    static_assert(std::is_same_v<decltype(i), int>, "");
    EXPECT_EQ(i, ++count);
  }
  EXPECT_EQ(count, 5);

  count = 0;
  for (auto i : ArrayIterator<int, 5>::with_array(nums)) {
    static_assert(std::is_same_v<decltype(i), int>, "");
    EXPECT_EQ(i, ++count);
  }
  EXPECT_EQ(count, 5);
}

TEST(IteratorBase, All) {
  {
    int nums[5] = {1, 2, 3, 4, 5};
    auto it = ArrayIterator<int, 5>::with_array(nums);
    EXPECT_TRUE(it.all([](int i) { return i <= 5; }));
  }
  {
    int nums[5] = {1, 2, 3, 4, 5};
    auto it = ArrayIterator<int, 5>::with_array(nums);
    EXPECT_FALSE(it.all([](int i) { return i <= 4; }));
  }
  {
    int nums[5] = {1, 2, 3, 4, 5};
    auto it = ArrayIterator<int, 5>::with_array(nums);
    EXPECT_FALSE(it.all([](int i) { return i <= 0; }));
  }

  // Shortcuts at the first failure.
  {
    int nums[5] = {1, 2, 3, 4, 5};
    auto it = ArrayIterator<int, 5>::with_array(nums);
    EXPECT_FALSE(it.all([](int i) { return i <= 3; }));
    Option<int> n = it.next();
    ASSERT_TRUE(n.is_some());
    // The `all()` function stopped when it consumed 4, so we can still consume
    // 5.
    EXPECT_EQ(n.as_ref().unwrap(), 5);
  }

  {
    auto it = EmptyIterator<int>();
    EXPECT_TRUE(it.all([](int) { return false; }));
  }
}

TEST(IteratorBase, Any) {
  {
    int nums[5] = {1, 2, 3, 4, 5};
    auto it = ArrayIterator<int, 5>::with_array(nums);
    EXPECT_TRUE(it.any([](int i) { return i == 5; }));
  }
  {
    int nums[5] = {1, 2, 3, 4, 5};
    auto it = ArrayIterator<int, 5>::with_array(nums);
    EXPECT_FALSE(it.any([](int i) { return i == 6; }));
  }
  {
    int nums[5] = {1, 2, 3, 4, 5};
    auto it = ArrayIterator<int, 5>::with_array(nums);
    EXPECT_TRUE(it.any([](int i) { return i == 1; }));
  }

  // Shortcuts at the first success.
  {
    int nums[5] = {1, 2, 3, 4, 5};
    auto it = ArrayIterator<int, 5>::with_array(nums);
    EXPECT_TRUE(it.any([](int i) { return i == 3; }));
    Option<int> n = it.next();
    ASSERT_TRUE(n.is_some());
    // The `any()` function stopped when it consumed 3, so we can still consume
    // 4.
    EXPECT_EQ(n.as_ref().unwrap(), 4);
  }

  {
    auto it = EmptyIterator<int>();
    EXPECT_FALSE(it.any([](int) { return false; }));
  }
}

TEST(IteratorBase, Count) {
  {
    int nums[5] = {1, 2, 3, 4, 5};
    auto it = ArrayIterator<int, 5>::with_array(nums);
    EXPECT_EQ(it.count(), 5_usize);
  }
  {
    int nums[2] = {4, 5};
    auto it = ArrayIterator<int, 2>::with_array(nums);
    EXPECT_EQ(it.count(), 2_usize);
  }
  {
    int nums[1] = {2};
    auto it = ArrayIterator<int, 1>::with_array(nums);
    EXPECT_EQ(it.count(), 1_usize);
  }

  // Consumes the whole iterator.
  {
    int nums[5] = {1, 2, 3, 4, 5};
    auto it = ArrayIterator<int, 5>::with_array(nums);
    EXPECT_EQ(it.count(), 5_usize);
    Option<int> n = it.next();
    ASSERT_FALSE(n.is_some());
  }

  {
    auto it = EmptyIterator<int>();
    EXPECT_EQ(it.count(), 0_usize);
  }
}

TEST(Iterator, Filter) {
  i32 nums[5] = {1, 2, 3, 4, 5};

  auto fit = ArrayIterator<i32, 5>::with_array(nums).filter(
      [](const i32& i) { return i >= 3; });
  EXPECT_EQ(fit.count(), 3_usize);

  auto fit2 = ArrayIterator<i32, 5>::with_array(nums)
                  .filter([](const i32& i) { return i >= 3; })
                  .filter([](const i32& i) { return i <= 4; });
  i32 expect = 3;
  for (i32 i : fit2) {
    EXPECT_EQ(expect, i);
    expect += 1;
  }
  EXPECT_EQ(expect, 5);
}

struct Filtering {
  Filtering(i32 i) : i(i) {}
  Filtering(Filtering&& f) : i(f.i) {}
  ~Filtering() {}
  i32 i;
};
static_assert(!sus::mem::relocate_by_memcpy<Filtering>);

TEST(Iterator, FilterNonTriviallyRelocatable) {
  Filtering nums[5] = {Filtering(1), Filtering(2), Filtering(3), Filtering(4),
                       Filtering(5)};

  auto non_relocatable_it = ArrayIterator<Filtering, 5>::with_array(nums);
  static_assert(!sus::mem::relocate_by_memcpy<decltype(non_relocatable_it)>);

  auto fit = sus::move(non_relocatable_it).box().filter([](const Filtering& f) {
    return f.i >= 3;
  });
  EXPECT_EQ(fit.count(), 3_usize);
}

TEST(Iterator, Map) {
  i32 nums[5] = {1, 2, 3, 4, 5};

  auto it = ArrayIterator<i32, 5>::with_array(nums).map(
      [](i32&& i) { return u32::from(i); });
  auto v = sus::move(it).collect_vec();
  static_assert(std::same_as<decltype(v), sus::Vec<u32>>);
  {
    u32 expect = 1u;
    for (u32 i : v) {
      EXPECT_EQ(expect, i);
      expect += 1u;
    }
  }

  struct MapOut {
    u32 val;
  };

  auto it2 = ArrayIterator<i32, 5>::with_array(nums)
                 .map([](i32&& i) { return u32::from(i); })
                 .map([](u32&& i) { return MapOut(i); });
  auto v2 = sus::move(it2).collect_vec();
  static_assert(std::same_as<decltype(v2), sus::Vec<MapOut>>);
  {
    u32 expect = 1u;
    for (const MapOut& i : v2) {
      EXPECT_EQ(expect, i.val);
      expect += 1u;
    }
  }
}

template <class T>
struct CollectSum {
  sus_clang_bug_54040(CollectSum(T sum) : sum(sum){});

  static constexpr CollectSum from_iter(IteratorBase<T>&& iter) noexcept {
    T sum = T();
    for (T t : iter) sum += t;
    return CollectSum(sum);
  }

  T sum;
};

TEST(Iterator, Collect) {
  i32 nums[5] = {1, 2, 3, 4, 5};

  auto collected =
      ArrayIterator<i32, 5>::with_array(nums).collect<CollectSum<i32>>();
  EXPECT_EQ(collected.sum, 1 + 2 + 3 + 4 + 5);
}

TEST(Iterator, CollectVec) {
  i32 nums[5] = {1, 2, 3, 4, 5};

  auto collected = ArrayIterator<i32, 5>::with_array(nums).collect_vec();
  static_assert(std::same_as<decltype(collected), Vec<i32>>);
  EXPECT_EQ(collected.len(), 5u);
  EXPECT_EQ(collected[0u], 1);
  EXPECT_EQ(collected[2u], 3);
  EXPECT_EQ(collected[4u], 5);
}

}  // namespace
