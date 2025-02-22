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

#include <type_traits>

#include "subspace/num/types.h"

namespace {

// Regression test for MSVC compilation bug:
// https://developercommunity.visualstudio.com/t/MSVC-Compiler-bug-with:-numeric-literal/10108160
template <class T>
void TestOperationInTemplateFunction() {
  0_u32 + 1_u32;
}

}  // namespace
