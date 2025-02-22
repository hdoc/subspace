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

#pragma once

// TODO: Support for Result<void, E>.
// TODO: Support for Result<T&, E>.

// TODO: and, and_then, as_mut, as_ref, cloned, copied, expect, expect_err,
// etc...
// https://doc.rust-lang.org/std/result/enum.Result.html

#include "subspace/assertions/check.h"
#include "subspace/assertions/unreachable.h"
#include "subspace/iter/__private/adaptors.h"
#include "subspace/macros/no_unique_address.h"
#include "subspace/marker/unsafe.h"
#include "subspace/mem/clone.h"
#include "subspace/mem/copy.h"
#include "subspace/mem/move.h"
#include "subspace/mem/mref.h"
#include "subspace/mem/relocate.h"
#include "subspace/mem/replace.h"
#include "subspace/mem/take.h"
#include "subspace/option/option.h"
#include "subspace/result/__private/marker.h"
#include "subspace/result/__private/result_state.h"
#include "subspace/result/__private/storage.h"

namespace sus::iter {
template <class ItemT>
class IteratorBase;
template <class Item>
class Once;
template <class T>
constexpr auto begin(const T& t) noexcept;
template <class T>
constexpr auto end(const T& t) noexcept;
}  // namespace sus::iter

namespace sus::result {

using sus::iter::Once;

/// The representation of an Result's state, which can either be #Ok to
/// represent it has a success value, or #Err for when it is holding an error
/// value.
enum class State : bool {
  /// The Result is holding an error value.
  Err = 0,
  /// The Result is holding a success value.
  Ok = 1,
};
using State::Err;
using State::Ok;

template <class T, class E>
class [[nodiscard]] Result final {
  static_assert(!std::is_reference_v<T>,
                "References in Result are not yet supported.");
  static_assert(!std::is_reference_v<E>,
                "References in Result are not yet supported.");

 public:
  using OkType = T;
  using ErrType = E;

  /// Construct an Result that is holding the given success value.
  static constexpr inline Result with(const T& t) noexcept
    requires(::sus::mem::Copy<T>)
  {
    return Result(WithOk, t);
  }
  static constexpr inline Result with(T&& t) noexcept
    requires(::sus::mem::Move<T>)
  {
    return Result(WithOk, ::sus::move(t));
  }

  /// Construct an Result that is holding the given error value.
  static constexpr inline Result with_err(const E& e) noexcept
    requires(::sus::mem::Copy<E>)
  {
    return Result(WithErr, e);
  }
  static constexpr inline Result with_err(E&& e) noexcept
    requires(::sus::mem::Move<E>)
  {
    return Result(WithErr, ::sus::move(e));
  }

  /// Takes each element in the Iterator: if it is an Err, no further elements
  /// are taken, and the Err is returned. Should no Err occur, a container with
  /// the values of each Result is returned.
  ///
  /// sus::iter::FromIterator trait.
  template <class U>
  static constexpr Result from_iter(
      ::sus::iter::IteratorBase<Result<U, E>>&& iter) noexcept
    requires(::sus::iter::FromIterator<T, U>)
  {
    auto err = Option<E>::none();
    auto success_out =
        Result::with(T::from_iter(::sus::iter::__private::Unwrapper(
            ::sus::move(iter), mref(err),
            [](Result<U, E>&& r) { return static_cast<Result<U, E>&&>(r); })));
    return ::sus::move(err).map_or_else(
        [&]() { return ::sus::move(success_out); },
        [](E e) { return Result::with_err(e); });
  }

  /// Destructor for the Result.
  ///
  /// Destroys the Ok or Err value contained within the Result.
  ///
  /// If T and E can be trivially destroyed, we don't need to explicitly destroy
  /// them, so we can use the default destructor, which allows Result<T, E> to
  /// also be trivially destroyed.
  constexpr ~Result()
    requires(std::is_trivially_destructible_v<T> &&
             std::is_trivially_destructible_v<E>)
  = default;

  constexpr inline ~Result() noexcept
    requires(!std::is_trivially_destructible_v<T> ||
             !std::is_trivially_destructible_v<E>)
  {
    switch (state_) {
      case __private::ResultState::IsMoved: break;
      case __private::ResultState::IsOk: storage_.ok_.~T(); break;
      case __private::ResultState::IsErr: storage_.err_.~E(); break;
    }
  }

  /// If T and E can be trivially copy-constructed, Result<T, E> can also be
  /// trivially copy-constructed.
  ///
  /// #[doc.overloads=copy]
  constexpr Result(const Result&)
    requires(::sus::mem::Copy<T> && ::sus::mem::Copy<E> &&
             std::is_trivially_copy_constructible_v<T> &&
             std::is_trivially_copy_constructible_v<E>)
  = default;

  Result(const Result& rhs) noexcept
    requires(::sus::mem::Copy<T> && ::sus::mem::Copy<E> &&
             !(std::is_trivially_copy_constructible_v<T> &&
               std::is_trivially_copy_constructible_v<E>))
      : state_(rhs.state_) {
    ::sus::check(state_ != __private::ResultState::IsMoved);
    switch (state_) {
      case __private::ResultState::IsOk:
        new (&storage_.ok_) T(rhs.storage_.ok_);
        break;
      case __private::ResultState::IsErr:
        new (&storage_.err_) E(rhs.storage_.err_);
        break;
      case __private::ResultState::IsMoved:
        // SAFETY: The state_ is verified to be Ok or Err at the top of the
        // function.
        ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
    }
  }

  constexpr Result(const Result&)
    requires(!::sus::mem::Copy<T> || !::sus::mem::Copy<E>)
  = delete;

  /// If T and E can be trivially copy-assigned, Result<T, E> can also be
  /// trivially copy-assigned.
  ///
  /// #[doc.overloads=copy]
  constexpr Result& operator=(const Result& o)
    requires(::sus::mem::Copy<T> && ::sus::mem::Copy<E> &&
             std::is_trivially_copy_assignable_v<T> &&
             std::is_trivially_copy_assignable_v<E>)
  = default;

  Result& operator=(const Result& o) noexcept
    requires(::sus::mem::Copy<T> && ::sus::mem::Copy<E> &&
             !(std::is_trivially_copy_assignable_v<T> &&
               std::is_trivially_copy_assignable_v<E>))
  {
    check(o.state_ != __private::ResultState::IsMoved);
    switch (state_) {
      case __private::ResultState::IsOk:
        switch (state_ = o.state_) {
          case __private::ResultState::IsOk:
            mem::replace_and_discard(mref(storage_.ok_), o.storage_.ok_);
            break;
          case __private::ResultState::IsErr:
            storage_.ok_.~T();
            new (&storage_.err_) E(o.storage_.err_);
            break;
          // SAFETY: This condition is check()'d at the top of the function.
          case __private::ResultState::IsMoved:
            ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
        }
        break;
      case __private::ResultState::IsErr:
        switch (state_ = o.state_) {
          case __private::ResultState::IsErr:
            mem::replace_and_discard(mref(storage_.err_), o.storage_.err_);
            break;
          case __private::ResultState::IsOk:
            storage_.err_.~E();
            new (&storage_.ok_) T(o.storage_.ok_);
            break;
          // SAFETY: This condition is check()'d at the top of the function.
          case __private::ResultState::IsMoved:
            ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
        }
        break;
      case __private::ResultState::IsMoved:
        switch (state_ = o.state_) {
          case __private::ResultState::IsErr:
            new (&storage_.err_) E(o.storage_.err_);
            break;
          case __private::ResultState::IsOk:
            new (&storage_.ok_) T(o.storage_.ok_);
            break;
            // SAFETY: This condition is check()'d at the top of the function.
          case __private::ResultState::IsMoved:
            ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
        }
        break;
    }
    return *this;
  }

  /// If T and E can be trivially move-constructed, Result<T, E> can also be
  /// trivially move-constructed.
  ///
  /// #[doc.overloads=move]
  constexpr Result(Result&&)
    requires(::sus::mem::Move<T> && ::sus::mem::Move<E> &&
             std::is_trivially_move_constructible_v<T> &&
             std::is_trivially_move_constructible_v<E>)
  = default;

  Result(Result&& rhs) noexcept
    requires(::sus::mem::Move<T> && ::sus::mem::Move<E> &&
             !(std::is_trivially_move_constructible_v<T> &&
               std::is_trivially_move_constructible_v<E>))
      : state_(::sus::mem::replace(mref(rhs.state_),
                                   __private::ResultState::IsMoved)) {
    ::sus::check(state_ != __private::ResultState::IsMoved);
    switch (state_) {
      case __private::ResultState::IsOk:
        new (&storage_.ok_) T(::sus::move(rhs.storage_.ok_));
        break;
      case __private::ResultState::IsErr:
        new (&storage_.err_) E(::sus::move(rhs.storage_.err_));
        break;
      case __private::ResultState::IsMoved:
        // SAFETY: The state_ is verified to be Ok or Err at the top of the
        // function.
        ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
    }
  }

  constexpr Result(Result&&)
    requires(!::sus::mem::Move<T> || !::sus::mem::Move<E>)
  = delete;

  /// If T and E can be trivially move-assigned, Result<T, E> can also be
  /// trivially move-assigned.
  ///
  /// #[doc.overloads=move]
  constexpr Result& operator=(Result&& o)
    requires(::sus::mem::Move<T> && ::sus::mem::Move<E> &&
             std::is_trivially_move_assignable_v<T> &&
             std::is_trivially_move_assignable_v<E>)
  = default;

  Result& operator=(Result&& o) noexcept
    requires(::sus::mem::Move<T> && ::sus::mem::Move<E> &&
             !(std::is_trivially_move_assignable_v<T> &&
               std::is_trivially_move_assignable_v<E>))
  {
    check(o.state_ != __private::ResultState::IsMoved);
    switch (state_) {
      case __private::ResultState::IsOk:
        switch (state_ = ::sus::mem::replace(mref(o.state_),
                                             __private::ResultState::IsMoved)) {
          case __private::ResultState::IsOk:
            mem::replace_and_discard(mref(storage_.ok_),
                                     ::sus::move(o.storage_.ok_));
            break;
          case __private::ResultState::IsErr:
            storage_.ok_.~T();
            new (&storage_.err_) E(::sus::move(o.storage_.err_));
            break;
          // SAFETY: This condition is check()'d at the top of the function.
          case __private::ResultState::IsMoved:
            ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
        }
        break;
      case __private::ResultState::IsErr:
        switch (state_ = ::sus::mem::replace(mref(o.state_),
                                             __private::ResultState::IsMoved)) {
          case __private::ResultState::IsErr:
            mem::replace_and_discard(mref(storage_.err_),
                                     ::sus::move(o.storage_.err_));
            break;
          case __private::ResultState::IsOk:
            storage_.err_.~E();
            new (&storage_.ok_) T(::sus::move(o.storage_.ok_));
            break;
          // SAFETY: This condition is check()'d at the top of the function.
          case __private::ResultState::IsMoved:
            ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
        }
        break;
      case __private::ResultState::IsMoved:
        switch (state_ = ::sus::mem::replace(mref(o.state_),
                                             __private::ResultState::IsMoved)) {
          case __private::ResultState::IsErr:
            new (&storage_.err_) E(::sus::move(o.storage_.err_));
            break;
          case __private::ResultState::IsOk:
            new (&storage_.ok_) T(::sus::move(o.storage_.ok_));
            break;
            // SAFETY: This condition is check()'d at the top of the function.
          case __private::ResultState::IsMoved:
            ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
        }
        break;
    }
    return *this;
  }

  constexpr Result& operator=(Result&& o)
    requires(!::sus::mem::Move<T> || !::sus::mem::Move<E>)
  = delete;

  constexpr Result clone() const& noexcept
    requires(::sus::mem::Clone<T> && ::sus::mem::Clone<E> &&
             !(::sus::mem::Copy<T> && ::sus::mem::Copy<E>))
  {
    ::sus::check(state_ != __private::ResultState::IsMoved);
    switch (state_) {
      case __private::ResultState::IsOk:
        return Result::with(::sus::clone(storage_.ok_));
      case __private::ResultState::IsErr:
        return Result::with_err(::sus::clone(storage_.err_));
      case __private::ResultState::IsMoved: break;
    }
    // SAFETY: The state_ is verified to be Ok or Err at the top of the
    // function.
    ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
  }

  void clone_from(const Result& source) &
        requires(::sus::mem::Clone<T> && ::sus::mem::Clone<E> &&
                 !(::sus::mem::Copy<T> && ::sus::mem::Copy<E>))
  {
    ::sus::check(source.state_ != __private::ResultState::IsMoved);
    if (state_ == source.state_) {
      switch (state_) {
        case __private::ResultState::IsOk:
          ::sus::clone_into(mref(storage_.ok_), source.storage_.ok_);
          break;
        case __private::ResultState::IsErr:
          ::sus::clone_into(mref(storage_.err_), source.storage_.err_);
          break;
        case __private::ResultState::IsMoved:
          ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
      }
    } else {
      *this = source.clone();
    }
  }

  /// Returns true if the result is `Ok`.
  constexpr inline bool is_ok() const& noexcept {
    ::sus::check(state_ != __private::ResultState::IsMoved);
    return state_ == __private::ResultState::IsOk;
  }

  /// Returns true if the result is `Err`.
  constexpr inline bool is_err() const& noexcept {
    ::sus::check(state_ != __private::ResultState::IsMoved);
    return state_ == __private::ResultState::IsErr;
  }

  /// An operator which returns the state of the Result, either #Ok or #Err.
  ///
  /// This supports the use of an Result in a `switch()`, allowing it to act as
  /// a tagged union between "success" and "error".
  ///
  /// # Example
  ///
  /// ```cpp
  /// auto x = Result<int, char>::with(2);
  /// switch (x) {
  ///  case Ok:
  ///   return sus::move(x).unwrap_unchecked(::sus::marker::unsafe_fn);
  ///  case Err:
  ///   return -1;
  /// }
  /// ```
  constexpr inline operator State() const& noexcept {
    ::sus::check(state_ != __private::ResultState::IsMoved);
    return static_cast<State>(state_);
  }

  /// Converts from `Result<T, E>` to `Option<T>`.
  ///
  /// Converts self into an `Option<T>`, consuming self, and discarding the
  /// error, if any.
  constexpr inline Option<T> ok() && noexcept {
    ::sus::check(state_ != __private::ResultState::IsMoved);
    switch (
        ::sus::mem::replace(mref(state_), __private::ResultState::IsMoved)) {
      case __private::ResultState::IsOk:
        return Option<T>::some(::sus::mem::take_and_destruct(
            ::sus::marker::unsafe_fn, mref(storage_.ok_)));
      case __private::ResultState::IsErr:
        storage_.err_.~E();
        return Option<T>::none();
      case __private::ResultState::IsMoved: break;
    }
    // SAFETY: The state_ is verified to be Ok or Err at the top of the
    // function.
    ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
  }

  /// Converts from `Result<T, E>` to `Option<E>`.
  ///
  /// Converts self into an `Option<E>`, consuming self, and discarding the
  /// success value, if any.
  constexpr inline Option<E> err() && noexcept {
    ::sus::check(state_ != __private::ResultState::IsMoved);
    switch (
        ::sus::mem::replace(mref(state_), __private::ResultState::IsMoved)) {
      case __private::ResultState::IsOk:
        storage_.ok_.~T();
        return Option<E>::none();
      case __private::ResultState::IsErr:
        return Option<E>::some(::sus::mem::take_and_destruct(
            ::sus::marker::unsafe_fn, mref(storage_.err_)));
      case __private::ResultState::IsMoved: break;
    }
    // SAFETY: The state_ is verified to be Ok or Err at the top of the
    // function.
    ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
  }

  /// Returns the contained `Ok` value, consuming the self value.
  ///
  /// Because this function may panic, its use is generally discouraged.
  /// Instead, prefer to use pattern matching and handle the `Err` case
  /// explicitly, or call `unwrap_or()`, `unwrap_or_else()`, or
  /// `unwrap_or_default()`.
  ///
  /// # Panics
  /// Panics if the value is an `Err`.
  constexpr inline T unwrap() && noexcept {
    check_with_message(
        ::sus::mem::replace(mref(state_), __private::ResultState::IsMoved) ==
            __private::ResultState::IsOk,
        *"called `Result::unwrap()` on an `Err` value");
    return ::sus::mem::take_and_destruct(::sus::marker::unsafe_fn,
                                         mref(storage_.ok_));
  }

  /// Returns the contained `Ok` value, consuming the self value, without
  /// checking that the value is not an `Err`.
  ///
  /// # Safety
  /// Calling this method on an `Err` is Undefined Behavior.
  constexpr inline T unwrap_unchecked(
      ::sus::marker::UnsafeFnMarker) && noexcept {
    return ::sus::mem::take_and_destruct(::sus::marker::unsafe_fn,
                                         mref(storage_.ok_));
  }

  /// Returns the contained `Err` value, consuming the self value.
  ///
  /// # Panics
  /// Panics if the value is an `Ok`.
  constexpr inline E unwrap_err() && noexcept {
    check_with_message(
        ::sus::mem::replace(mref(state_), __private::ResultState::IsMoved) ==
            __private::ResultState::IsErr,
        *"called `Result::unwrap_err()` on an `Ok` value");
    return ::sus::mem::take_and_destruct(::sus::marker::unsafe_fn,
                                         mref(storage_.err_));
  }

  /// Returns the contained `Err` value, consuming the self value, without
  /// checking that the value is not an `Ok`.
  ///
  /// # Safety
  /// Calling this method on an `Ok` is Undefined Behavior.
  constexpr inline E unwrap_err_unchecked(
      ::sus::marker::UnsafeFnMarker) && noexcept {
    return ::sus::mem::take_and_destruct(::sus::marker::unsafe_fn,
                                         mref(storage_.err_));
  }

  constexpr Once<const T&> iter() const& noexcept {
    ::sus::check(state_ != __private::ResultState::IsMoved);
    if (state_ == __private::ResultState::IsOk)
      return Once<const T&>::with(Option<const T&>::some(storage_.ok_));
    else
      return Once<const T&>::with(Option<const T&>::none());
  }
  Once<const T&> iter() const&& = delete;

  constexpr Once<T&> iter_mut() & noexcept {
    ::sus::check(state_ != __private::ResultState::IsMoved);
    if (state_ == __private::ResultState::IsOk)
      return Once<T&>::with(Option<T&>::some(mref(storage_.ok_)));
    else
      return Once<T&>::with(Option<T&>::none());
  }

  constexpr Once<T> into_iter() && noexcept {
    ::sus::check(state_ != __private::ResultState::IsMoved);
    if (::sus::mem::replace(mref(state_), __private::ResultState::IsMoved) ==
        __private::ResultState::IsOk) {
      return Once<T>::with(Option<T>::some(::sus::mem::take_and_destruct(
          ::sus::marker::unsafe_fn, mref(storage_.ok_))));
    } else {
      storage_.err_.~E();
      return Once<T>::with(Option<T>::none());
    }
  }

  /// sus::ops::Eq<Result<T, E>> trait.
  template <class U, class F>
    requires(::sus::ops::Eq<T, U> && ::sus::ops::Eq<E, F>)
  friend constexpr bool operator==(const Result& l,
                                   const Result<U, F>& r) noexcept {
    ::sus::check(l.state_ != __private::ResultState::IsMoved);
    switch (l.state_) {
      case __private::ResultState::IsOk:
        return r.is_ok() && l.storage_.ok_ == r.storage_.ok_;
      case __private::ResultState::IsErr:
        return r.is_err() && l.storage_.err_ == r.storage_.err_;
      case __private::ResultState::IsMoved: break;
    }
    ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
  }

  template <class U, class F>
    requires(!(::sus::ops::Eq<T, U> && ::sus::ops::Eq<E, F>))
  friend constexpr bool operator==(const Result& l,
                                   const Result<U, F>& r) = delete;

  /// Compares two Result.
  ///
  /// Satisfies sus::ops::Ord<Result<T, E>> if sus::ops::Ord<T> and
  /// sus::ops::Ord<E>.
  ///
  /// Satisfies sus::ops::WeakOrd<Result<T, E>> if sus::ops::WeakOrd<T> and
  /// sus::ops::WeakOrd<E>.
  ///
  /// Satisfies sus::ops::PartialOrd<Result<T, E>> if sus::ops::PartialOrd<T>
  /// and sus::ops::PartialOrd<E>.
  //
  // sus::ops::Ord<Result<T, E>> trait.
  // sus::ops::WeakOrd<Result<T, E>> trait.
  // sus::ops::PartialOrd<Result<T, E>> trait.
  template <class U, class F>
    requires(::sus::ops::Ord<T, U> && ::sus::ops::Ord<E, F>)
  friend constexpr auto operator<=>(const Result& l,
                                    const Result<U, F>& r) noexcept {
    ::sus::check(l.state_ != __private::ResultState::IsMoved);
    switch (l.state_) {
      case __private::ResultState::IsOk:
        if (r.is_ok())
          return std::strong_order(l.storage_.ok_, r.storage_.ok_);
        else
          return std::strong_ordering::greater;
      case __private::ResultState::IsErr:
        if (r.is_err())
          return std::strong_order(l.storage_.err_, r.storage_.err_);
        else
          return std::strong_ordering::less;
      case __private::ResultState::IsMoved: break;
    }
    ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
  }

  template <class U, class F>
    requires((!::sus::ops::Ord<T, U> || !::sus::ops::Ord<E, F>) &&
             ::sus::ops::WeakOrd<T, U> && ::sus::ops::WeakOrd<E, F>)
  friend constexpr auto operator<=>(const Result& l,
                                    const Result<U, F>& r) noexcept {
    ::sus::check(l.state_ != __private::ResultState::IsMoved);
    switch (l.state_) {
      case __private::ResultState::IsOk:
        if (r.is_ok())
          return std::weak_order(l.storage_.ok_, r.storage_.ok_);
        else
          return std::weak_ordering::greater;
      case __private::ResultState::IsErr:
        if (r.is_err())
          return std::weak_order(l.storage_.err_, r.storage_.err_);
        else
          return std::weak_ordering::less;
      case __private::ResultState::IsMoved: break;
    }
    ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
  }

  template <class U, class F>
    requires((!::sus::ops::WeakOrd<T, U> || !::sus::ops::WeakOrd<E, F>) &&
             ::sus::ops::PartialOrd<T, U> && ::sus::ops::PartialOrd<E, F>)
  friend constexpr auto operator<=>(const Result& l,
                                    const Result<U, F>& r) noexcept {
    ::sus::check(l.state_ != __private::ResultState::IsMoved);
    switch (l.state_) {
      case __private::ResultState::IsOk:
        if (r.is_ok())
          return std::partial_order(l.storage_.ok_, r.storage_.ok_);
        else
          return std::partial_ordering::greater;
      case __private::ResultState::IsErr:
        if (r.is_err())
          return std::partial_order(l.storage_.err_, r.storage_.err_);
        else
          return std::partial_ordering::less;
      case __private::ResultState::IsMoved: break;
    }
    ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
  }

  template <class U, class F>
    requires(!::sus::ops::PartialOrd<T, U> || !::sus::ops::PartialOrd<E, F>)
  friend constexpr auto operator<=>(const Result& l,
                                    const Result<U, F>& r) noexcept = delete;

 private:
  enum WithOkType { WithOk };
  constexpr inline Result(WithOkType, const T& t) noexcept
      : storage_(__private::kWithT, t), state_(__private::ResultState::IsOk) {}
  constexpr inline Result(WithOkType, T&& t) noexcept
    requires(::sus::mem::Move<T>)
      : storage_(__private::kWithT, ::sus::move(t)),
        state_(__private::ResultState::IsOk) {}
  enum WithErrType { WithErr };
  constexpr inline Result(WithErrType, const E& e) noexcept
      : storage_(__private::kWithE, e), state_(__private::ResultState::IsErr) {}
  constexpr inline Result(WithErrType, E&& e) noexcept
    requires(::sus::mem::Move<E>)
      : storage_(__private::kWithE, ::sus::move(e)),
        state_(__private::ResultState::IsErr) {}

  [[sus_no_unique_address]] __private::Storage<T, E> storage_;
  __private::ResultState state_;

  sus_class_trivially_relocatable_if_types(::sus::marker::unsafe_fn, T, E,
                                           decltype(state_));
};

// Implicit for-ranged loop iteration via `Result::iter()`.
using sus::iter::__private::begin;
using sus::iter::__private::end;

/// Used to construct a Result<T, E> with an Ok(t) value.
///
/// Calling ok() produces a hint to make a Result<T, E> but does not actually
/// construct Result<T, E>. This is to deduce the actual types `T` and `E` when
/// it is constructed, avoid specifying them both here, and support conversions.
template <class T>
[[nodiscard]] inline constexpr auto ok(
    T&& t sus_if_clang([[clang::lifetimebound]])) noexcept {
  return __private::OkMarker<T&&>(::sus::forward<T>(t));
}

/// Used to construct a Result<T, E> with an Err(e) value.
///
/// Calling err() produces a hint to make a Result<T, E> but does not actually
/// construct Result<T, E>. This is to deduce the actual types `T` and `E` when
/// it is constructed, avoid specifying them both here, and support conversions.
template <class E>
[[nodiscard]] inline constexpr auto err(
    E&& e sus_if_clang([[clang::lifetimebound]])) noexcept {
  return __private::ErrMarker<E&&>(::sus::forward<E>(e));
}

}  // namespace sus::result
