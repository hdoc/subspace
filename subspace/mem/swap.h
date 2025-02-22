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

#include <string.h>

#include <type_traits>

#include "subspace/mem/addressof.h"
#include "subspace/mem/move.h"
#include "subspace/mem/mref.h"
#include "subspace/mem/relocate.h"
#include "subspace/mem/size_of.h"

namespace sus::mem {

template <class T>
  requires(sus::mem::Move<T>)
constexpr void swap(T& lhs, T& rhs) noexcept {
  if constexpr (::sus::mem::relocate_by_memcpy<T>) {
    // memcpy() is not constexpr so we can't use it in constexpr evaluation.
    if (!std::is_constant_evaluated()) {
      char temp[::sus::mem::data_size_of<T>()];
      memcpy(temp, ::sus::mem::addressof(lhs), ::sus::mem::data_size_of<T>());
      memcpy(::sus::mem::addressof(lhs), ::sus::mem::addressof(rhs),
             ::sus::mem::data_size_of<T>());
      memcpy(::sus::mem::addressof(rhs), temp, ::sus::mem::data_size_of<T>());
      return;
    }
  }
  T temp(::sus::move(lhs));
  lhs = ::sus::move(rhs);
  rhs = ::sus::move(temp);
}

}  // namespace sus::mem
