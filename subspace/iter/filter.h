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

#include "subspace/fn/fn_defn.h"
#include "subspace/iter/iterator_concept.h"
#include "subspace/iter/iterator_defn.h"
#include "subspace/iter/sized_iterator.h"
#include "subspace/mem/move.h"
#include "subspace/mem/relocate.h"

namespace sus::iter {

using ::sus::mem::relocate_by_memcpy;

template <class ItemT, size_t InnerIterSize, size_t InnerIterAlign>
class Filter final
    : public IteratorImpl<Filter<ItemT, InnerIterSize, InnerIterAlign>, ItemT> {
  using Pred = ::sus::fn::FnMut<bool(
      // TODO: write a sus::const_ref<T>?
      const std::remove_reference_t<const std::remove_reference_t<ItemT>&>&)>;
  using InnerSizedIter = SizedIterator<ItemT, InnerIterSize, InnerIterAlign>;

 public:
  using Item = ItemT;

  static Filter with(Pred&& pred, InnerSizedIter&& next_iter) noexcept {
    return Filter(::sus::move(pred), ::sus::move(next_iter));
  }

  Option<Item> next() noexcept final {
    IteratorBase<Item>& next_iter = next_iter_.iterator_mut();
    Pred& pred = pred_;

    // TODO: Just call find(pred) on itself?
    while (true) {
      Option<Item> item = next_iter.next();
      if (item.is_none() ||
          pred(item.as_ref().unwrap_unchecked(::sus::marker::unsafe_fn)))
        return item;
    }
  }

 private:
  Filter(Pred&& pred, InnerSizedIter&& next_iter)
      : pred_(::sus::move(pred)), next_iter_(::sus::move(next_iter)) {}

  Pred pred_;
  InnerSizedIter next_iter_;

  // The InnerSizedIter is already known to be trivially relocatable, as
  // SizedIterator is only constructed for trivially relocatable iterators.
  // Likewise, the predicate is known to be trivially relocatable because FnMut
  // is.
  sus_class_trivially_relocatable(::sus::marker::unsafe_fn, decltype(pred_),
                                  decltype(next_iter_));
};

}  // namespace sus::iter
