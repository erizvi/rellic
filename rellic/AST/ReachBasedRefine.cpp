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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/ReachBasedRefine.h"

namespace rellic {

namespace {

using IfStmtVec = std::vector<clang::IfStmt *>;

static IfStmtVec GetIfStmts(clang::CompoundStmt *compound) {
  IfStmtVec result;
  for (auto stmt : compound->body()) {
    if (auto ifstmt = clang::dyn_cast<clang::IfStmt>(stmt)) {
      result.push_back(ifstmt);
    }
  }
  return result;
}

}  // namespace

char ReachBasedRefine::ID = 0;

ReachBasedRefine::ReachBasedRefine(clang::ASTContext &ctx,
                                   rellic::IRToASTVisitor &ast_gen)
    : ModulePass(ReachBasedRefine::ID),
      ast_ctx(&ctx),
      ast_gen(&ast_gen),
      z3_ctx(new z3::context()),
      z3_gen(new rellic::Z3ConvVisitor(ast_ctx, z3_ctx.get())),
      z3_solver(*z3_ctx, "sat") {}

bool ReachBasedRefine::Prove(z3::expr expr) {
  z3::goal goal(*z3_ctx);
  goal.add((!expr).simplify());
  auto app = z3_solver(goal);
  CHECK(app.size() == 1) << "Unexpected multiple goals in application!";
  return app[0].is_decided_unsat();
}

z3::expr ReachBasedRefine::GetZ3Cond(clang::IfStmt *ifstmt) {
  auto cond = ifstmt->getCond();
  auto expr = z3_gen->Z3BoolCast(z3_gen->GetOrCreateZ3Expr(cond));
  return expr.simplify();
}

void ReachBasedRefine::CreateIfElseStmts(IfStmtVec stmts) {
  // Else-if candidate IfStmts and their Z3 form
  // reaching conditions.
  IfStmtVec elifs;
  z3::expr_vector conds(*z3_ctx);
  // Test that determines if a new IfStmts is not
  // reachable from the already gathered IfStmts.
  auto IsUnrechable = [this, &conds](z3::expr cond) {
    return Prove(!(cond && z3::mk_or(conds)));
  };
  // Test to determine if we have enough candidate
  // IfStmts to form an else-if cascade.
  auto IsTautology = [this, &conds] {
    return Prove(z3::mk_or(conds) == z3_ctx->bool_val(true));
  };

  // Gather else-if candidates
  for (auto stmt : llvm::make_range(stmts.rbegin(), stmts.rend())) {
    // Quit if we gathered enough IfStmts for a cascade.
    // This is recognized when the conjuction of reaching
    // conditions of all the IfStmts form a tautology.
    if (IsTautology()) {
      break;
    }
    // Clear else-if IfStmts if we find a path among them.
    auto cond = GetZ3Cond(stmt);
    if (stmt->getElse() || !IsUnrechable(cond)) {
      conds = z3::expr_vector(*z3_ctx);
      elifs.clear();
    }
    // Add the current if-statement to the else-if candidates.
    conds.push_back(cond);
    elifs.push_back(stmt);
  }

  // Check if we have enough statements to work with
  if (elifs.size() < 2) {
    return;
  }

  // Create the else-if cascade
  clang::IfStmt *sub = nullptr;
  for (auto stmt : llvm::make_range(elifs.rbegin(), elifs.rend())) {
    auto cond = stmt->getCond();
    auto then = stmt->getThen();
    if (stmt == elifs.back()) {
      sub = CreateIfStmt(*ast_ctx, cond, then);
      substitutions[stmt] = sub;
    } else if (stmt == elifs.front()) {
      std::vector<clang::Stmt *> thens({then});
      sub->setElse(CreateCompoundStmt(*ast_ctx, thens));
      substitutions[stmt] = nullptr;
    } else {
      auto elif = CreateIfStmt(*ast_ctx, cond, then);
      sub->setElse(elif);
      sub = elif;
      substitutions[stmt] = nullptr;
    }
  }
}

bool ReachBasedRefine::VisitCompoundStmt(clang::CompoundStmt *compound) {
  // DLOG(INFO) << "VisitCompoundStmt";
  // Create else-if cascade substitutions for IfStmts in `compound`
  CreateIfElseStmts(GetIfStmts(compound));
  // Apply created else-if substitutions and
  // create a replacement for `compound`
  if (ReplaceChildren(compound, substitutions)) {
    std::vector<clang::Stmt *> new_body;
    for (auto stmt : compound->body()) {
      if (stmt) {
        new_body.push_back(stmt);
      }
    }
    substitutions[compound] = CreateCompoundStmt(*ast_ctx, new_body);
  }
  return true;
}

bool ReachBasedRefine::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Reachability-based refinement";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

llvm::ModulePass *createReachBasedRefinePass(clang::ASTContext &ctx,
                                             rellic::IRToASTVisitor &gen) {
  return new ReachBasedRefine(ctx, gen);
}

}  // namespace rellic