/*
 * Copyright (c) 2020 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "rellic/AST/Compat/Mangle.h"

#include <clang/AST/GlobalDecl.h>
#include <clang/AST/Mangle.h>

#include "rellic/BC/Version.h"

namespace rellic {

void MangleNameCXXCtor(clang::MangleContext *ctx,
                       clang::CXXConstructorDecl *decl, llvm::raw_ostream &os) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(11, 0)
  ctx->mangleName(clang::GlobalDecl(decl), os);
#else
  ctx->mangleCXXCtor(decl, clang::Ctor_Complete, os);
#endif
}

void MangleNameCXXDtor(clang::MangleContext *ctx,
                       clang::CXXDestructorDecl *decl, llvm::raw_ostream &os) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(11, 0)
  ctx->mangleName(clang::GlobalDecl(decl), os);
#else
  ctx->mangleCXXDtor(decl, clang::Dtor_Complete, os);
#endif
}
}  // namespace rellic