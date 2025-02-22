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

// TODO: Overload all && functions with const& version if T is copyable? The
// latter copies T instead of moving it. This could lead to a lot of unintended
// copies if expensive types have copy constructors, which is common in
// preexisting C++ code since there's no concept of Clone there (which will TBD
// in this library). So it's not clear if this is the right thing to do
// actually, needs thought.

#pragma once

#include <type_traits>

#include "subspace/assertions/check.h"
#include "subspace/assertions/unreachable.h"
#include "subspace/construct/default.h"
#include "subspace/iter/from_iterator.h"
#include "subspace/macros/always_inline.h"
#include "subspace/macros/compiler.h"
#include "subspace/macros/nonnull.h"
#include "subspace/marker/unsafe.h"
#include "subspace/mem/clone.h"
#include "subspace/mem/copy.h"
#include "subspace/mem/forward.h"
#include "subspace/mem/move.h"
#include "subspace/mem/mref.h"
#include "subspace/mem/relocate.h"
#include "subspace/mem/replace.h"
#include "subspace/mem/take.h"
#include "subspace/ops/eq.h"
#include "subspace/ops/ord.h"
#include "subspace/option/__private/is_option_type.h"
#include "subspace/option/__private/is_tuple_type.h"
#include "subspace/option/__private/marker.h"
#include "subspace/option/__private/storage.h"
#include "subspace/option/state.h"
#include "subspace/result/__private/is_result_type.h"

namespace sus::iter {
template <class ItemT>
class IteratorBase;
template <class Item>
class Once;
namespace __private {
template <class T>
constexpr auto begin(const T& t) noexcept;
template <class T>
constexpr auto end(const T& t) noexcept;
}  // namespace __private
}  // namespace sus::iter

namespace sus::result {
template <class T, class E>
class Result;
}  // namespace sus::result

namespace sus::tuple_type {
template <class T, class... Ts>
class Tuple;
}  // namespace sus::tuple_type

namespace sus::option {

using State::None;
using State::Some;
using sus::iter::Once;
using sus::option::__private::Storage;
using sus::option::__private::StoragePointer;

/// A type which either holds #Some value of type `T`, or #None.
///
/// `Option<const T>` for non-reference-type `T` is disallowed, as the Option
/// owns the `T` in that case and it ensures the `Option` and the `T` are both
/// accessed with the same const-ness.
///
/// If a type provides a never-value field (see mem/never_value.h), and is a
/// [standard-layout
/// type](https://en.cppreference.com/w/cpp/named_req/StandardLayoutType), then
/// Option<T> will have the same size as T.
///
/// However the never-value field places some limitations on what can be
/// constexpr in the Option type. Because it is not possible to query the state
/// of the Option in a constant evaluation context, state-querying methods can
/// not be constexpr, nor any method that branches based on the current state,
/// such as `unwrap_or()`.
template <class T>
class Option final {
  // Note that `const T&` is allowed (so we don't `std::remove_reference_t<T>`)
  // as it would require dropping the const qualification to copy that into the
  // Option as a `T&`.
  static_assert(!std::is_const_v<T>,
                "`Option<const T>` should be written `const Option<T>`, as "
                "const applies transitively.");

 public:
  /// Default-construct an Option that is holding no value.
  ///
  /// The Option's contained type `T` must be #Default.
  inline constexpr Option() noexcept = default;

  /// Construct an Option that is holding the given value.
  static inline constexpr Option some(const T& t) noexcept
    requires(!std::is_reference_v<T> && ::sus::mem::CopyOrRef<T>)
  {
    return Option(t);
  }

  static inline constexpr Option some(T&& t) noexcept
    requires(!std::is_reference_v<T>)
  {
    if constexpr (::sus::mem::MoveOrRef<T>) {
      return Option(move_to_storage(t));
    } else {
      static_assert(::sus::mem::CopyOrRef<T>,
                    "All types should be Copy or Move.");
      return Option(t);
    }
  }

  static inline constexpr Option some(T t) noexcept
    requires(std::is_reference_v<T>)
  {
    return Option(move_to_storage(t));
  }

  /// Construct an Option that is holding no value.
  static inline constexpr Option none() noexcept { return Option(); }

  /// Takes each item in the Iterator: if it is None, no further elements are
  /// taken, and the None is returned. Should no None occur, a container of type
  /// T containing the values of type U from each Option<U> is returned.
  ///
  /// sus::iter::FromIterator trait.
  template <class U>
  static constexpr Option from_iter(
      ::sus::iter::IteratorBase<Option<U>>&& option_iter) noexcept
    requires(!std::is_reference_v<T> && ::sus::iter::FromIterator<T, U>)
  {
    struct Iter : public ::sus::iter::IteratorBase<U> {
      Iter(::sus::iter::IteratorBase<Option<U>>&& iter, bool& found_none)
          : iter(iter), found_none(found_none) {}

      Option<U> next() noexcept final {
        Option<Option<U>> item = iter.next();
        if (found_none || item.is_none()) return Option<U>::none();
        found_none = item->is_none();
        return ::sus::move(item).flatten();
      }

      ::sus::iter::IteratorBase<Option<U>>& iter;
      bool& found_none;
    };

    bool found_none = false;
    auto collected =
        T::from_iter(Iter(::sus::move(option_iter), ::sus::mref(found_none)));
    if (found_none)
      return Option::none();
    else
      return Option::some(::sus::move(collected));
  }

  /// Destructor for the Option.
  ///
  /// Destroys the value contained within the option, if there is one.
  ///
  /// If T can be trivially destroyed, we don't need to explicitly destroy it,
  /// so we can use the default destructor, which allows Option<T> to also be
  /// trivially destroyed.
  constexpr ~Option() noexcept
    requires(std::is_trivially_destructible_v<T>)
  = default;

  constexpr inline ~Option() noexcept
    requires(!std::is_trivially_destructible_v<T>)
  {
    if (t_.state() == Some) t_.destroy();
  }

  // If T can be trivially copy-constructed, Option<T> can also be trivially
  // copy-constructed.
  constexpr Option(const Option& o)
    requires(::sus::mem::CopyOrRef<T> &&
             std::is_trivially_copy_constructible_v<T>)
  = default;

  constexpr Option(const Option& o) noexcept
    requires(::sus::mem::CopyOrRef<T> &&
             !std::is_trivially_copy_constructible_v<T>)
  {
    if (o.t_.state() == Some) t_.construct_from_none(o.t_.val());
  }

  constexpr Option(const Option& o)
    requires(!::sus::mem::CopyOrRef<T>)
  = delete;

  // If T can be trivially move-constructed, Option<T> can also be trivially
  // move-constructed.
  constexpr Option(Option&& o)
    requires(::sus::mem::MoveOrRef<T> &&
             std::is_trivially_move_constructible_v<T>)
  = default;

  constexpr Option(Option&& o) noexcept
    requires(::sus::mem::MoveOrRef<T> &&
             !std::is_trivially_move_constructible_v<T>)
  {
    if (o.t_.state() == Some) t_.construct_from_none(o.t_.take_and_set_none());
  }

  constexpr Option(Option&& o)
    requires(!::sus::mem::MoveOrRef<T>)
  = delete;

  // If T can be trivially copy-assigned, Option<T> can also be trivially
  // copy-assigned.
  constexpr Option& operator=(const Option& o)
    requires(::sus::mem::CopyOrRef<T> && std::is_trivially_copy_assignable_v<T>)
  = default;

  Option& operator=(const Option& o) noexcept
    requires(::sus::mem::CopyOrRef<T> &&
             !std::is_trivially_copy_assignable_v<T>)
  {
    if (o.t_.state() == Some)
      t_.set_some(copy_to_storage(o.t_.val()));
    else if (t_.state() == Some)
      t_.set_none();
    return *this;
  }

  constexpr Option& operator=(const Option& o)
    requires(!::sus::mem::CopyOrRef<T>)
  = delete;

  // If T can be trivially move-assigned, we don't need to explicitly construct
  // it, so we can use the default destructor, which allows Option<T> to also
  // be trivially move-assigned.
  constexpr Option& operator=(Option&& o)
    requires(::sus::mem::MoveOrRef<T> && std::is_trivially_move_assignable_v<T>)
  = default;

  Option& operator=(Option&& o) noexcept
    requires(::sus::mem::MoveOrRef<T> &&
             !std::is_trivially_move_assignable_v<T>)
  {
    if (o.t_.state() == Some)
      t_.set_some(o.t_.take_and_set_none());
    else if (t_.state() == Some)
      t_.set_none();
    return *this;
  }

  constexpr Option& operator=(Option&& o)
    requires(!::sus::mem::MoveOrRef<T>)
  = delete;

  /// sus::mem::Clone trait.
  constexpr Option clone() const& noexcept
    requires(::sus::mem::Clone<T> && !::sus::mem::CopyOrRef<T>)
  {
    if (t_.state() == Some)
      return Option(::sus::clone(t_.val()));
    else
      return Option::none();
  }

  /// sus::mem::CloneInto trait.
  void clone_from(const Option& source) & noexcept
    requires(::sus::mem::Clone<T> && !::sus::mem::CopyOrRef<T>)
  {
    if (t_.state() == Some && source.t_.state() == Some) {
      ::sus::clone_into(mref(t_.val_mut()), source.t_.val());
    } else {
      *this = source.clone();
    }
  }

  /// Returns whether the Option currently contains a value.
  ///
  /// If there is a value present, it can be extracted with `unwrap()` or
  /// `expect()`, or can be accessed through `operator->` and `operator*`.
  constexpr bool is_some() const noexcept { return t_.state() == Some; }
  /// Returns whether the Option is currently empty, containing no value.
  constexpr bool is_none() const noexcept { return t_.state() == None; }

  /// An operator which returns the state of the Option, either #Some or #None.
  ///
  /// This supports the use of an Option in a `switch()`, allowing it to act as
  /// a tagged union between "some value" and "no value".
  ///
  /// # Example
  ///
  /// ```cpp
  /// auto x = Option<int>::some(2);
  /// switch (x) {
  ///  case Some:
  ///   return sus::move(x).unwrap_unchecked(::sus::marker::unsafe_fn);
  ///  case None:
  ///   return -1;
  /// }
  /// ```
  constexpr operator State() const& { return t_.state(); }

  /// Returns the contained value inside the Option.
  ///
  /// The function will panic with the given message if the Option's state is
  /// currently `None`.
  constexpr sus_nonnull_fn T expect(
      /* TODO: string view type */ sus_nonnull_arg const char*
          msg) && noexcept {
    ::sus::check_with_message(t_.state() == Some, *msg);
    return ::sus::move(*this).unwrap_unchecked(::sus::marker::unsafe_fn);
  }

  /// Returns the contained value inside the Option.
  ///
  /// The function will panic without a message if the Option's state is
  /// currently `None`.
  constexpr T unwrap() && noexcept {
    ::sus::check(t_.state() == Some);
    return ::sus::move(*this).unwrap_unchecked(::sus::marker::unsafe_fn);
  }

  /// Returns the contained value inside the Option.
  ///
  /// # Safety
  ///
  /// It is Undefined Behaviour to call this function when the Option's state is
  /// `None`. The caller is responsible for ensuring the Option contains a value
  /// beforehand, and the safer <unwrap>() or <expect>() should almost always be
  /// preferred.
  constexpr inline T unwrap_unchecked(
      ::sus::marker::UnsafeFnMarker) && noexcept {
    return t_.take_and_set_none();
  }

  /// Returns a reference to the contained value inside the Option.
  ///
  /// The reference is const if the Option is const, and is mutable otherwise.
  /// This method allows calling methods directly on the type inside the Option
  /// without unwrapping.
  ///
  /// # Panic
  ///
  /// The function will panic without a message if the Option's state is
  /// currently `None`.
  ///
  /// # Implementation Notes
  //
  /// Implementation note: We don't allow calling this method on rvalue types,
  /// it would allow a reference to the inner object to escape from an rvalue in
  /// an unbounded way. When passing an rvalue as a function argument,
  /// `Option::unwrap()` does the right thing already and will convert to a
  /// reference implicitly.
  ///
  /// Implementation note: This method is added in addition to the Rust Option
  /// API because:
  /// * C++ moving is verbose, making unwrap() on lvalues loud.
  /// * Unwrapping requires a new lvalue name since C++ doesn't allow name
  ///   reuse, making variable names bad.
  /// * It's expected due to std::optional and general container-of-one things
  ///   to provide access through operator* and operator->.
  constexpr const std::remove_reference_t<T>& operator*() const& noexcept {
    ::sus::check(t_.state() == Some);
    return t_.val();
  }
  constexpr const std::remove_reference_t<T>* operator*() && noexcept = delete;
  constexpr std::remove_reference_t<T>& operator*() & noexcept {
    ::sus::check(t_.state() == Some);
    return t_.val_mut();
  }

  /// Returns a pointer to the contained value inside the Option.
  ///
  /// The pointer is const if the Option is const, and is mutable otherwise.
  /// This method allows calling methods directly on the type inside the Option
  /// without unwrapping.
  ///
  /// # Panic
  ///
  /// The function will panic without a message if the Option's state is
  /// currently `None`.
  ///
  /// # Implementation Notes
  ///
  /// Implementation note: We don't allow calling this method on rvalue types,
  /// it would allow a pointer to the inner object to escape from an rvalue.
  /// When using an rvalue, `Option::unwrap()` does the right thing already and
  /// will give access to the fields and methods of the inner type.
  ///
  /// Implementation note: This method is added in addition to the Rust Option
  /// API because:
  /// * C++ moving is verbose, making unwrap() on lvalues loud.
  /// * Unwrapping requires a new lvalue name since C++ doesn't allow name
  ///   reuse, making variable names bad.
  /// * It's expected due to std::optional and general container-of-one things
  ///   to provide access through operator* and operator->.
  constexpr const std::remove_reference_t<T>* operator->() const& noexcept {
    ::sus::check(t_.state() == Some);
    return ::sus::mem::addressof(
        static_cast<const std::remove_reference_t<T>&>(t_.val()));
  }
  constexpr const std::remove_reference_t<T>* operator->() && noexcept;
  constexpr std::remove_reference_t<T>* operator->() & noexcept {
    ::sus::check(t_.state() == Some);
    return ::sus::mem::addressof(
        static_cast<std::remove_reference_t<T>&>(t_.val_mut()));
  }

  /// Returns the contained value inside the Option, if there is one. Otherwise,
  /// returns `default_result`.
  ///
  /// Note that if it is non-trivial to construct a `default_result`, that
  /// <unwrap_or_else>() should be used instead, as it will only construct the
  /// default value if required.
  constexpr T unwrap_or(T default_result) && noexcept {
    if (t_.state() == Some) {
      return t_.take_and_set_none();
    } else {
      return default_result;
    }
  }

  /// Returns the contained value inside the Option, if there is one.
  /// Otherwise, returns the result of the given function.
  template <class Functor>
    requires(std::is_same_v<std::invoke_result_t<Functor>, T>)
  constexpr T unwrap_or_else(Functor f) && noexcept {
    if (t_.state() == Some) {
      return t_.take_and_set_none();
    } else {
      return f();
    }
  }

  /// Returns the contained value inside the Option, if there is one.
  /// Otherwise, constructs a default value for the type and returns that.
  ///
  /// The Option's contained type `T` must be #Default, and will be
  /// constructed through that trait.
  constexpr T unwrap_or_default() && noexcept
    requires(!std::is_reference_v<T> && ::sus::construct::Default<T>)
  {
    if (t_.state() == Some) {
      return t_.take_and_set_none();
    } else {
      return T();
    }
  }

  /// Stores the value `t` inside this Option, replacing any previous value, and
  /// returns a mutable reference to the new value.
  T& insert(T t) & noexcept
    requires(sus::mem::MoveOrRef<T>)
  {
    t_.set_some(move_to_storage(t));
    return t_.val_mut();
  }

  /// If the Option holds a value, returns a mutable reference to it. Otherwise,
  /// stores `t` inside the Option and returns a mutable reference to the new
  /// value.
  ///
  /// If it is non-trivial to construct `T`, the <get_or_insert_with>() method
  /// would be preferable, as it only constructs a `T` if needed.
  T& get_or_insert(T t) & noexcept
    requires(sus::mem::MoveOrRef<T>)
  {
    if (t_.state() == None) {
      t_.construct_from_none(move_to_storage(t));
    }
    return t_.val_mut();
  }

  /// If the Option holds a value, returns a mutable reference to it. Otherwise,
  /// constructs a default value `T`, stores it inside the Option and returns a
  /// mutable reference to the new value.
  ///
  /// This method differs from <unwrap_or_default>() in that it does not consume
  /// the Option, and instead it can not be called on rvalues.
  ///
  /// This is a shorthand for
  /// `Option<T>::get_or_insert_default(Default<T>::make_default)`.
  ///
  /// The Option's contained type `T` must be #Default, and will be
  /// constructed through that trait.
  constexpr T& get_or_insert_default() & noexcept
    requires(::sus::construct::Default<T>)
  {
    if (t_.state() == None) t_.construct_from_none(T());
    return t_.val_mut();
  }

  /// If the Option holds a value, returns a mutable reference to it. Otherwise,
  /// constructs a `T` by calling `f`, stores it inside the Option and returns a
  /// mutable reference to the new value.
  ///
  /// This method differs from <unwrap_or_else>() in that it does not consume
  /// the Option, and instead it can not be called on rvalues.
  template <class WithFn>
    requires(std::is_same_v<std::invoke_result_t<WithFn>, T>)
  T& get_or_insert_with(WithFn f) & noexcept {
    if (t_.state() == None) t_.construct_from_none(move_to_storage(f()));
    return t_.val_mut();
  }

  /// Returns a new Option containing whatever was inside the current Option.
  ///
  /// If this Option contains #None then it is left unchanged and returns an
  /// Option containing #None. If this Option contains #Some with a value, the
  /// value is moved into the returned Option and this Option will contain #None
  /// afterward.
  constexpr Option take() & noexcept {
    if (t_.state() == Some)
      return Option(t_.take_and_set_none());
    else
      return Option::none();
  }

  /// Maps the Option's value through a function.
  ///
  /// Consumes the Option, passing the value through the map function, and
  /// returning an `Option<R>` where `R` is the return type of the map function.
  ///
  /// Returns an `Option<R>` in state #None if the current Option is in state
  /// #None.
  template <class MapFn, int&..., class R = std::invoke_result_t<MapFn, T&&>>
    requires(!std::is_void_v<R>)
  constexpr Option<R> map(MapFn m) && noexcept {
    if (t_.state() == Some) {
      return Option<R>(m(t_.take_and_set_none()));
    } else {
      return Option<R>::none();
    }
  }

  /// Returns the provided default result (if none), or applies a function to
  /// the contained value (if any).
  ///
  /// Arguments passed to map_or are eagerly evaluated; if you are passing the
  /// result of a function call, it is recommended to use map_or_else, which is
  /// lazily evaluated.
  template <class MapFn, class D, int&...,
            class R = std::invoke_result_t<MapFn, T&&>>
    requires(!std::is_void_v<R> && std::is_same_v<D, R>)
  constexpr R map_or(D default_result, MapFn m) && noexcept {
    if (t_.state() == Some) {
      return m(t_.take_and_set_none());
    } else {
      return default_result;
    }
  }

  /// Computes a default function result (if none), or applies a different
  /// function to the contained value (if any).
  template <class DefaultFn, class MapFn, int&...,
            class D = std::invoke_result_t<DefaultFn>,
            class R = std::invoke_result_t<MapFn, T&&>>
    requires(!std::is_void_v<R> && std::is_same_v<D, R>)
  constexpr R map_or_else(DefaultFn default_fn, MapFn m) && noexcept {
    if (t_.state() == Some) {
      return m(t_.take_and_set_none());
    } else {
      return default_fn();
    }
  }

  /// Consumes the Option and applies a predicate function to the value
  /// contained in the Option. Returns a new Option with the same value if the
  /// predicate returns true, otherwise returns an Option with its state set to
  /// #None.
  ///
  /// The predicate function must take `const T&` and return `bool`.
  template <class Predicate>
    requires(std::is_same_v<std::invoke_result_t<Predicate, const T&>, bool>)
  constexpr Option<T> filter(Predicate p) && noexcept {
    if (t_.state() == Some) {
      if (p(t_.val())) {
        return Option(t_.take_and_set_none());
      } else {
        // The state has to become None, and we must destroy the inner T.
        t_.set_none();
        return Option::none();
      }
    } else {
      return Option::none();
    }
  }

  /// Consumes this Option and returns an Option with #None if this Option holds
  /// #None, otherwise returns the given `opt`.
  template <class U>
  constexpr Option<U> and_opt(Option<U> opt) && noexcept {
    if (t_.state() == Some) {
      t_.set_none();
      return opt;
    } else {
      return Option<U>::none();
    }
  }

  /// Consumes this Option and returns an Option with #None if this Option holds
  /// #None, otherwise calls `f` with the contained value and returns an Option
  /// with the result.
  ///
  /// Some languages call this operation flatmap.
  template <
      class AndFn, int&..., class R = std::invoke_result_t<AndFn, T&&>,
      class InnerR = ::sus::option::__private::IsOptionType<R>::inner_type>
    requires(::sus::option::__private::IsOptionType<R>::value)
  constexpr Option<InnerR> and_then(AndFn f) && noexcept {
    if (t_.state() == Some)
      return f(t_.take_and_set_none());
    else
      return Option<InnerR>::none();
  }

  /// Consumes and returns an Option with the same value if this Option contains
  /// a value, otherwise returns the given `opt`.
  constexpr Option<T> or_opt(Option<T> opt) && noexcept {
    if (t_.state() == Some)
      return Option(t_.take_and_set_none());
    else
      return opt;
  }

  /// Consumes and returns an Option with the same value if this Option contains
  /// a value, otherwise returns the Option returned by `f`.
  template <class ElseFn, int&..., class R = std::invoke_result_t<ElseFn>>
    requires(std::is_same_v<R, Option<T>>)
  constexpr Option<T> or_else(ElseFn f) && noexcept {
    if (t_.state() == Some)
      return Option(t_.take_and_set_none());
    else
      return ::sus::move(f)();
  }

  /// Consumes this Option and returns an Option, holding the value from either
  /// this Option `opt`, if exactly one of them holds a value, otherwise returns
  /// an Option that holds #None.
  constexpr Option<T> xor_opt(Option<T> opt) && noexcept {
    if (t_.state() == Some) {
      // If `this` holds Some, we change `this` to hold None. If `opt` is None,
      // we return what this was holding, otherwise we return None.
      if (opt.t_.state() == None) {
        return Option(t_.take_and_set_none());
      } else {
        t_.set_none();
        return Option::none();
      }
    } else {
      // If `this` holds None, we need to do nothing to `this`. If `opt` is Some
      // we would return its value, and if `opt` is None we should return None.
      return opt;
    }
  }

  /// Transforms the `Option<T>` into a `Result<T, E>`, mapping `Some(v)` to
  /// `Ok(v)` and `None` to `Err(e)`.
  ///
  /// Arguments passed to #ok_or are eagerly evaluated; if you are passing the
  /// result of a function call, it is recommended to use ok_or_else, which is
  /// lazily evaluated.
  //
  // TODO: No refs in Result: https://github.com/chromium/subspace/issues/133
  template <class E, int&..., class Result = ::sus::result::Result<T, E>>
  constexpr Result ok_or(E e) && noexcept
    requires(!std::is_reference_v<T> && !std::is_reference_v<E>)
  {
    if (t_.state() == Some)
      return Result::with(t_.take_and_set_none());
    else
      return Result::with_err(::sus::move(e));
  }

  /// Transforms the `Option<T>` into a `Result<T, E>`, mapping `Some(v)` to
  /// `Ok(v)` and `None` to `Err(f())`.
  //
  // TODO: No refs in Result: https://github.com/chromium/subspace/issues/133
  template <class ElseFn, int&..., class E = std::invoke_result_t<ElseFn>,
            class Result = ::sus::result::Result<T, E>>
  constexpr Result ok_or_else(ElseFn f) && noexcept
    requires(!std::is_reference_v<T> && !std::is_reference_v<E>)
  {
    if (t_.state() == Some)
      return Result::with(t_.take_and_set_none());
    else
      return Result::with_err(::sus::move(f)());
  }

  /// Transposes an #Option of a #Result into a #Result of an #Option.
  ///
  /// `None` will be mapped to `Ok(None)`. `Some(Ok(_))` and `Some(Err(_))` will
  /// be mapped to `Ok(Some(_))` and `Err(_)`.
  template <int&...,
            class OkType =
                typename ::sus::result::__private::IsResultType<T>::ok_type,
            class ErrType =
                typename ::sus::result::__private::IsResultType<T>::err_type,
            class Result = ::sus::result::Result<Option<OkType>, ErrType>>
    requires(::sus::result::__private::IsResultType<T>::value)
  constexpr Result transpose() && noexcept {
    if (t_.state() == None) {
      return Result::with(Option<OkType>::none());
    } else {
      if (t_.val().is_ok()) {
        return Result::with(Option<OkType>::some(
            t_.take_and_set_none().unwrap_unchecked(::sus::marker::unsafe_fn)));
      } else {
        return Result::with_err(t_.take_and_set_none().unwrap_err_unchecked(
            ::sus::marker::unsafe_fn));
      }
    }
  }

  /// Zips self with another Option.
  ///
  /// If self is `Some(s)` and other is `Some(o)`, this method returns
  /// `Some((s, o))`. Otherwise, `None` is returned.
  template <class U, int&..., class Tuple = ::sus::tuple_type::Tuple<T, U>>
  constexpr Option<Tuple> zip(Option<U> o) && noexcept {
    if (o.t_.state() == None) {
      if (t_.state() == Some) t_.set_none();
      return Option<Tuple>::none();
    } else if (t_.state() == None) {
      return Option<Tuple>::none();
    } else {
      return Option<Tuple>::some(
          Tuple::with(t_.take_and_set_none(), ::sus::move(o).unwrap()));
    }
  }

  /// Unzips an Option holding a Tuple of two values into a Tuple of two
  /// Options.
  ///
  /// `Option<Tuple<i32, u32>>` is unzipped to `Tuple<Option<i32>,
  /// Option<u32>>`.
  ///
  /// If self is Some, the result is a Tuple with both Options holding the
  /// values from self. Otherwise, the result is a Tuple of two Options set to
  /// None.
  constexpr auto unzip() && noexcept
    requires(!std::is_reference_v<T> && __private::IsTupleOfSizeTwo<T>::value)
  {
    using U = __private::IsTupleOfSizeTwo<T>::first_type;
    using V = __private::IsTupleOfSizeTwo<T>::second_type;
    if (is_some()) {
      auto&& [u, v] = t_.take_and_set_none();
      return ::sus::tuple_type::Tuple<Option<U>, Option<V>>::with(
          Option<U>::some(::sus::forward<U>(u)),
          Option<V>::some(::sus::forward<V>(v)));
    } else {
      return ::sus::tuple_type::Tuple<Option<U>, Option<V>>::with(
          Option<U>::none(), Option<V>::none());
    }
  }

  /// Replaces whatever the Option is currently holding with #Some value `t` and
  /// returns an Option holding what was there previously.
  Option replace(T t) & noexcept
    requires(sus::mem::MoveOrRef<T>)
  {
    if (t_.state() == None) {
      t_.construct_from_none(move_to_storage(t));
      return Option::none();
    } else {
      return Option(t_.replace_some(move_to_storage(t)));
    }
  }

  /// Maps an `Option<T&>` to an `Option<T>` by copying the referenced `T`.
  constexpr Option<std::remove_const_t<std::remove_reference_t<T>>>
  copied() && noexcept
    requires(std::is_reference_v<T> && ::sus::mem::Copy<T>)
  {
    if (t_.state() == None) {
      return Option<std::remove_const_t<std::remove_reference_t<T>>>::none();
    } else {
      return Option<std::remove_const_t<std::remove_reference_t<T>>>::some(
          t_.val());
    }
  }

  /// Maps an `Option<T&>` to an `Option<T>` by cloning the referenced `T`.
  constexpr Option<std::remove_const_t<std::remove_reference_t<T>>>
  cloned() && noexcept
    requires(std::is_reference_v<T> && ::sus::mem::Clone<T>)
  {
    if (t_.state() == None) {
      return Option<std::remove_const_t<std::remove_reference_t<T>>>::none();
    } else {
      // Specify the type `T` for clone() as `t_.val()` may be a
      // `StoragePointer<T>` when the Option is holding a reference, and we want
      // to clone the `T` object, not the `StoragePointer<T>`. The latter
      // converts to a `const T&`.
      return Option<std::remove_const_t<std::remove_reference_t<T>>>::some(
          ::sus::clone<std::remove_reference_t<T>>(t_.val()));
    }
  }

  /// Maps an `Option<Option<T>>` to an `Option<T>`.
  constexpr T flatten() && noexcept
    requires(::sus::option::__private::IsOptionType<T>::value)
  {
    if (t_.state() == Some)
      return ::sus::move(*this).unwrap_unchecked(::sus::marker::unsafe_fn);
    else
      return T::none();
  }

  /// Returns an Option<const T&> from this Option<T>, that either holds #None
  /// or a reference to the value in this Option.
  ///
  /// When not holding a `sus::mem::NeverValueField` type, the method can be
  /// evaluated in a constant expression.
  //
  // Not constexpr as it needs to read the state of the value which can't be
  // done if `T` is `NeverValueField`.
  constexpr Option<const std::remove_reference_t<T>&> as_ref() const& noexcept {
    if (t_.state() == None)
      return Option<const std::remove_reference_t<T>&>::none();
    else
      return Option<const std::remove_reference_t<T>&>(t_.val());
  }
  // Calling as_ref() on an rvalue is not returning a reference to the inner
  // value if the inner value is already a reference, so we allow calling it on
  // an rvalue Option in that case.
  constexpr Option<const std::remove_reference_t<T>&> as_ref() const&& noexcept
    requires(std::is_reference_v<T>)
  {
    return as_ref();
  }

  /// Returns an Option<T&> from this Option<T>, that either holds #None or a
  /// reference to the value in this Option.
  //
  // Not constexpr as it needs to read the state of the value which can't be
  // done if `T` is `NeverValueField`.
  constexpr Option<T&> as_mut() & noexcept {
    if (t_.state() == None)
      return Option<T&>::none();
    else
      return Option<T&>(t_.val_mut());
  }
  // Calling as_mut() on an rvalue is not returning a reference to the inner
  // value if the inner value is already a reference, so we allow calling it on
  // an rvalue Option in that case.
  constexpr Option<const std::remove_reference_t<T>&> as_mut() && noexcept
    requires(std::is_reference_v<T>)
  {
    return as_mut();
  }

  constexpr Once<const std::remove_reference_t<T>&> iter() const& noexcept {
    return Once<const std::remove_reference_t<T>&>::with(as_ref());
  }
  constexpr Once<const T&> iter() const&& = delete;

  constexpr Once<T&> iter_mut() & noexcept { return Once<T&>::with(as_mut()); }

  constexpr Once<T> into_iter() && noexcept { return Once<T>::with(take()); }

 private:
  template <class U>
  friend class Option;

  // Since `T` may be a reference or a value type, this constructs the correct
  // storage from a `T` object or a `T&&` (which is received as `T&`).
  template <class U>
  static constexpr inline decltype(auto) copy_to_storage(const U& t) {
    if constexpr (std::is_reference_v<T>)
      return StoragePointer<T&>(t);
    else
      return t;
  }
  // Since `T` may be a reference or a value type, this constructs the correct
  // storage from a `T` object or a `T&&` (which is received as `T&`).
  template <class U>
  static constexpr inline decltype(auto) move_to_storage(U&& t) {
    if constexpr (std::is_reference_v<T>)
      return StoragePointer<T&>(t);
    else
      return ::sus::move(t);
  }

  /// Constructors for #Some.
  constexpr explicit Option(T t)
    requires(std::is_reference_v<T>)
      : t_(StoragePointer<T>(t)) {}
  constexpr explicit Option(const T& t)
    requires(!std::is_reference_v<T>)
      : t_(t) {}
  constexpr explicit Option(T&& t)
    requires(!std::is_reference_v<T> && ::sus::mem::MoveOrRef<T>)
      : t_(::sus::move(t)) {}

  template <class U>
  using StorageType =
      std::conditional_t<std::is_reference_v<U>, Storage<StoragePointer<U>>,
                         Storage<U>>;
  StorageType<T> t_;

  sus_class_trivially_relocatable_if_types(::sus::marker::unsafe_fn,
                                           StorageType<T>);
};

/// sus::ops::Eq<Option<U>> trait.
template <class T, class U>
  requires(::sus::ops::Eq<T, U>)
constexpr inline bool operator==(const Option<T>& l,
                                 const Option<U>& r) noexcept {
  switch (l) {
    case Some:
      return r.is_some() &&
             (l.as_ref().unwrap_unchecked(::sus::marker::unsafe_fn) ==
              r.as_ref().unwrap_unchecked(::sus::marker::unsafe_fn));
    case None: return r.is_none();
  }
  ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
}

template <class T, class U>
  requires(!::sus::ops::Eq<T, U>)
constexpr inline bool operator==(const Option<T>& l,
                                 const Option<U>& r) = delete;

/// Compares two Options.
///
/// Satisfies sus::ops::Ord<Option<U>> if sus::ops::Ord<U>.
///
/// Satisfies sus::ops::WeakOrd<Option<U>> if sus::ops::WeakOrd<U>.
///
/// Satisfies sus::ops::PartialOrd<Option<U>> if sus::ops::PartialOrd<U>.
//
// sus::ops::Ord<Option<U>> trait.
// sus::ops::WeakOrd<Option<U>> trait.
// sus::ops::PartialOrd<Option<U>> trait.
template <class T, class U>
  requires(::sus::ops::ExclusiveOrd<T, U>)
constexpr inline auto operator<=>(const Option<T>& l,
                                  const Option<U>& r) noexcept {
  switch (l) {
    case Some:
      if (r.is_some()) {
        return l.as_ref().unwrap_unchecked(::sus::marker::unsafe_fn) <=>
               r.as_ref().unwrap_unchecked(::sus::marker::unsafe_fn);
      } else {
        return std::strong_ordering::greater;
      }
    case None:
      if (r.is_some())
        return std::strong_ordering::less;
      else
        return std::strong_ordering::equivalent;
  }
  ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
}

// sus::ops::WeakOrd<Option<U>> trait.
template <class T, class U>
  requires(::sus::ops::ExclusiveWeakOrd<T, U>)
constexpr inline auto operator<=>(const Option<T>& l,
                                  const Option<U>& r) noexcept {
  switch (l) {
    case Some:
      if (r.is_some()) {
        return l.as_ref().unwrap_unchecked(::sus::marker::unsafe_fn) <=>
               r.as_ref().unwrap_unchecked(::sus::marker::unsafe_fn);
      } else {
        return std::weak_ordering::greater;
      }
    case None:
      if (r.is_some())
        return std::weak_ordering::less;
      else
        return std::weak_ordering::equivalent;
  }
  ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
}

// sus::ops::PartialOrd<Option<U>> trait.
template <class T, class U>
  requires(::sus::ops::ExclusivePartialOrd<T, U>)
constexpr inline auto operator<=>(const Option<T>& l,
                                  const Option<U>& r) noexcept {
  switch (l) {
    case Some:
      if (r.is_some()) {
        return l.as_ref().unwrap_unchecked(::sus::marker::unsafe_fn) <=>
               r.as_ref().unwrap_unchecked(::sus::marker::unsafe_fn);
      } else {
        return std::partial_ordering::greater;
      }
    case None:
      if (r.is_some())
        return std::partial_ordering::less;
      else
        return std::partial_ordering::equivalent;
  }
  ::sus::unreachable_unchecked(::sus::marker::unsafe_fn);
}

template <class T, class U>
  requires(!::sus::ops::PartialOrd<T, U>)
constexpr inline auto operator<=>(const Option<T>& l,
                                  const Option<U>& r) noexcept = delete;

// Implicit for-ranged loop iteration via `Option::iter()`.
using sus::iter::__private::begin;
using sus::iter::__private::end;

/// Used to construct an Option<T> with a Some(t) value.
///
/// Calling some() produces a hint to make an Option<T> but does not actually
/// construct Option<T>. This is to allow constructing an Option<T> or
/// Option<T&> correctly.
template <class T>
[[nodiscard]] inline constexpr __private::SomeMarker<T&&> some(
    T&& t sus_if_clang([[clang::lifetimebound]])) noexcept {
  return __private::SomeMarker<T&&>(::sus::forward<T>(t));
}

/// Used to construct an Option<T> with a None value.
///
/// Calling none() produces a hint to make an Option<T> but does not actually
/// construct Option<T>. This is because the type `T` is not known until the
/// construction is explicitly requested.
[[nodiscard]] inline constexpr auto none() noexcept {
  return __private::NoneMarker();
}

}  // namespace sus::option

// Promote Option and its enum values into the `sus` namespace.
namespace sus {
using ::sus::option::none;
using ::sus::option::None;
using ::sus::option::Option;
using ::sus::option::some;
using ::sus::option::Some;
}  // namespace sus
