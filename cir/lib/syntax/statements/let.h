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

#include <string>

#include "cir/lib/source_span.h"
#include "cir/lib/syntax/type_reference.h"
#include "cir/llvm.h"
#include "subspace/choice/choice.h"
#include "subspace/prelude.h"

namespace cir::syntax {

enum LetClangTypeTag {
  Return,
  Variable,
};
using LetClangType =
    sus::Choice<sus_choice_types((LetClangTypeTag::Return, clang::QualType),
                                 (LetClangTypeTag::Variable, clang::VarDecl&))>;

struct Let {
  u32 id;
  TypeReference type;
  SourceSpan span;

  LetClangType clang_type;
};

}  // namespace cir::syntax

namespace cir {

struct Output;

inline std::string to_string(const syntax::Let& let,
                             const Output& output) noexcept {
  // TODO: Use fmt library (or add such to subspace).
  std::ostringstream s;
  s << "let _" << let.id.primitive_value << ": "
    << cir::to_string(let.type, output) << ";";
  return s.str();
}

}  // namespace cir
