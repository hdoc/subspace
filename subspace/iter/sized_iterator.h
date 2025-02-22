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

#include "subspace/convert/subclass.h"
#include "subspace/mem/relocate.h"
#include "subspace/mem/size_of.h"
// Doesn't include iterator_defn.h because it's included from there.

namespace sus::iter {

template <class Item>
class IteratorBase;

template <class Item, size_t SubclassSize, size_t SubclassAlign>
struct [[sus_trivial_abi]] SizedIterator final {
  constexpr SizedIterator(void (*destroy)(char& sized)) : destroy(destroy) {}

  SizedIterator(SizedIterator&& o)
      : destroy(::sus::mem::replace_ptr(mref(o.destroy), nullptr)) {
    memcpy(sized, o.sized, SubclassSize);
  }
  SizedIterator& operator=(SizedIterator&& o) = delete;

  ~SizedIterator() {
    if (destroy) destroy(*sized);
  }

  IteratorBase<Item>& iterator_mut() {
    // The `sized` array points to the stored Iterator subclass, which we cast
    // to the base class.
    return *reinterpret_cast<IteratorBase<Item>*>(sized);
  }

 private:
  alignas(SubclassAlign) char sized[SubclassSize];
  void (*destroy)(char& sized);

  sus_class_trivially_relocatable(::sus::marker::unsafe_fn, decltype(sized),
                                  decltype(destroy));
};

/// Make a SizedIterator which wraps a trivially relocatable iterator and erases
/// its type.
///
/// This type may only be used when the IteratorSubclass can be trivially
/// relocated. It stores the SubclassIterator directly into the SizedIterator,
/// erasing its type but remaining trivially relocatable.
template <::sus::mem::Move IteratorSubclass, int&...,
          class SubclassItem = typename IteratorSubclass::Item,
          class SizedIteratorType = SizedIterator<
              SubclassItem, ::sus::mem::size_of<IteratorSubclass>(),
              alignof(IteratorSubclass)>>
inline SizedIteratorType make_sized_iterator(IteratorSubclass&& subclass)
  requires(::sus::convert::SameOrSubclassOf<IteratorSubclass*,
                                            IteratorBase<SubclassItem>*> &&
           ::sus::mem::relocate_by_memcpy<IteratorSubclass>)
{
  auto it = SizedIteratorType([](char& sized) {
    reinterpret_cast<IteratorSubclass&>(sized).~IteratorSubclass();
  });
  new (&it.iterator_mut()) IteratorSubclass(::sus::move(subclass));
  return it;
}

}  // namespace sus::iter
