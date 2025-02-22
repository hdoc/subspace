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

#include "subspace/mem/replace.h"

#include <type_traits>

#include "googletest/include/gtest/gtest.h"
#include "subspace/macros/builtin.h"
#include "subspace/mem/relocate.h"
#include "subspace/prelude.h"

using sus::mem::replace;
using sus::mem::replace_ptr;

namespace {

TEST(Replace, ConstexprTrivialCopy) {
  using T = int;
  static_assert(std::is_trivially_copyable_v<T>, "");

  auto i = []() constexpr {
    T i(2);
    [[maybe_unused]] T j(::sus::mem::replace(mref(i), T(5)));
    return i;
  };
  auto j = []() constexpr {
    T i(2);
    T j(::sus::mem::replace(mref(i), T(5)));
    return j;
  };
  static_assert(i() == T(5), "");
  static_assert(j() == T(2), "");
}

TEST(Replace, ConstexprNonTrivial) {
  struct S {
    constexpr S(int n) : num(n) {}
    constexpr S(S&& other) : num(other.num), assigns(other.assigns) {}
    constexpr void operator=(S&& other) {
      num = other.num;
      assigns = other.assigns + 1;
    }
    int num;
    int assigns = 0;
  };
  static_assert(!std::is_trivially_copyable_v<S>, "");

  auto i = []() constexpr {
    S i(2);
    S j(::sus::mem::replace(mref(i), S(5)));
    return i;
  };
  auto j = []() constexpr {
    S i(2);
    S j(::sus::mem::replace(mref(i), S(5)));
    return j;
  };
  static_assert(i().num == 5, "");
  static_assert(j().num == 2, "");
  // The replace was done by move operations, since memcpy is not constexpr.
  static_assert(i().assigns == 1, "");
}

TEST(Replace, TrivialCopy) {
  using T = int;
  static_assert(std::is_trivially_copyable_v<T>, "");

  T i(2);
  T j(::sus::mem::replace(mref(i), T(5)));
  EXPECT_EQ(i, T(5));
  EXPECT_EQ(j, T(2));

  T lvalue(6);

  T k(::sus::mem::replace(mref(i), lvalue));
  EXPECT_EQ(i, T(6));
  EXPECT_EQ(k, T(5));

  ::sus::mem::replace_and_discard(mref(i), T(7));
  EXPECT_EQ(i, T(7));

  ::sus::mem::replace_and_discard(mref(i), lvalue);
  EXPECT_EQ(i, T(6));
}

TEST(Replace, NonTrivial) {
  struct S {
    constexpr S(int n) : num(n) {}
    constexpr S(const S&) : num(999), assigns(999) {}
    constexpr S(S&& other) : num(other.num), assigns(other.assigns) {}
    constexpr S& operator=(const S& other) {
      num = other.num;
      assigns = other.assigns + 1;
      return *this;
    }
    constexpr S& operator=(S&& other) {
      num = other.num;
      assigns = other.assigns + 1;
      return *this;
    }
    int num;
    int assigns = 0;
  };
  static_assert(!std::is_trivially_copyable_v<S>, "");

  S i(2);
  S j(::sus::mem::replace(mref(i), S(5)));
  EXPECT_EQ(i.num, 5);
  EXPECT_EQ(j.num, 2);
  // The replace was done by move operations.
  EXPECT_EQ(1, i.assigns);

  S lvalue(6);

  i.assigns = 0;
  S k(::sus::mem::replace(mref(i), lvalue));
  EXPECT_EQ(i.num, 6);
  EXPECT_EQ(k.num, 5);
  // The replace was done by move operations.
  EXPECT_EQ(1, i.assigns);

  i.assigns = 0;
  ::sus::mem::replace_and_discard(mref(i), S(7));
  EXPECT_EQ(i.num, 7);
  // The replace was done by move operations.
  EXPECT_EQ(1, i.assigns);

  i.assigns = 0;
  ::sus::mem::replace_and_discard(mref(i), lvalue);
  EXPECT_EQ(i.num, 6);
  // The replace was done by move operations.
  EXPECT_EQ(1, i.assigns);
}

TEST(ReplacePtr, Const) {
  int i1 = 1, i2 = 2;
  const int* p1 = &i1;
  const int* p2 = &i2;
  const int* o = replace_ptr(mref(p1), p2);
  EXPECT_EQ(o, &i1);
  EXPECT_EQ(p1, &i2);
  EXPECT_EQ(p2, &i2);

  o = replace_ptr(mref(p1), nullptr);
  EXPECT_EQ(o, &i2);
  EXPECT_EQ(p1, nullptr);
}

TEST(ReplacePtr, Mut) {
  int i1 = 1, i2 = 2;
  int* p1 = &i1;
  int* p2 = &i2;
  int* o = replace_ptr(mref(p1), p2);
  EXPECT_EQ(o, &i1);
  EXPECT_EQ(p1, &i2);
  EXPECT_EQ(p2, &i2);

  o = replace_ptr(mref(p1), nullptr);
  EXPECT_EQ(o, &i2);
  EXPECT_EQ(p1, nullptr);
}

}  // namespace
