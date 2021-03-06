//===--- MisleadingIndentationCheck.cpp - clang-tidy-----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MisleadingIndentationCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace readability {

static const IfStmt *getPrecedingIf(const SourceManager &SM,
                                    ASTContext *Context, const IfStmt *If) {
  auto parents = Context->getParents(*If);
  if (parents.size() != 1)
    return nullptr;
  if (const auto *PrecedingIf = parents[0].get<IfStmt>()) {
    SourceLocation PreviousElseLoc = PrecedingIf->getElseLoc();
    if (SM.getExpansionLineNumber(PreviousElseLoc) ==
        SM.getExpansionLineNumber(If->getIfLoc()))
      return PrecedingIf;
  }
  return nullptr;
}

void MisleadingIndentationCheck::danglingElseCheck(const SourceManager &SM,
                                                   ASTContext *Context,
                                                   const IfStmt *If) {
  SourceLocation IfLoc = If->getIfLoc();
  SourceLocation ElseLoc = If->getElseLoc();

  if (IfLoc.isMacroID() || ElseLoc.isMacroID())
    return;

  if (SM.getExpansionLineNumber(If->getThen()->getLocEnd()) ==
      SM.getExpansionLineNumber(ElseLoc))
    return;

  // Find location of first 'if' in a 'if else if' chain.
  for (auto PrecedingIf = getPrecedingIf(SM, Context, If); PrecedingIf;
       PrecedingIf = getPrecedingIf(SM, Context, PrecedingIf))
    IfLoc = PrecedingIf->getIfLoc();

  if (SM.getExpansionColumnNumber(IfLoc) !=
      SM.getExpansionColumnNumber(ElseLoc))
    diag(ElseLoc, "different indentation for 'if' and corresponding 'else'");
}

void MisleadingIndentationCheck::missingBracesCheck(const SourceManager &SM,
                                                    const CompoundStmt *CStmt) {
  const static StringRef StmtNames[] = {"if", "for", "while"};
  for (unsigned int i = 0; i < CStmt->size() - 1; i++) {
    const Stmt *CurrentStmt = CStmt->body_begin()[i];
    const Stmt *Inner = nullptr;
    int StmtKind = 0;

    if (const auto *CurrentIf = dyn_cast<IfStmt>(CurrentStmt)) {
      StmtKind = 0;
      Inner =
          CurrentIf->getElse() ? CurrentIf->getElse() : CurrentIf->getThen();
    } else if (const auto *CurrentFor = dyn_cast<ForStmt>(CurrentStmt)) {
      StmtKind = 1;
      Inner = CurrentFor->getBody();
    } else if (const auto *CurrentWhile = dyn_cast<WhileStmt>(CurrentStmt)) {
      StmtKind = 2;
      Inner = CurrentWhile->getBody();
    } else {
      continue;
    }

    if (isa<CompoundStmt>(Inner))
      continue;

    SourceLocation InnerLoc = Inner->getLocStart();
    SourceLocation OuterLoc = CurrentStmt->getLocStart();

    if (SM.getExpansionLineNumber(InnerLoc) ==
        SM.getExpansionLineNumber(OuterLoc))
      continue;

    const Stmt *NextStmt = CStmt->body_begin()[i + 1];
    SourceLocation NextLoc = NextStmt->getLocStart();

    if (InnerLoc.isMacroID() || OuterLoc.isMacroID() || NextLoc.isMacroID())
      continue;

    if (SM.getExpansionColumnNumber(InnerLoc) ==
        SM.getExpansionColumnNumber(NextLoc)) {
      diag(NextLoc, "misleading indentation: statement is indented too deeply");
      diag(OuterLoc, "did you mean this line to be inside this '%0'",
           DiagnosticIDs::Note)
          << StmtNames[StmtKind];
    }
  }
}

void MisleadingIndentationCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(ifStmt(hasElse(stmt())).bind("if"), this);
  Finder->addMatcher(
      compoundStmt(has(stmt(anyOf(ifStmt(), forStmt(), whileStmt()))))
          .bind("compound"),
      this);
}

void MisleadingIndentationCheck::check(const MatchFinder::MatchResult &Result) {
  if (const auto *If = Result.Nodes.getNodeAs<IfStmt>("if"))
    danglingElseCheck(*Result.SourceManager, Result.Context, If);

  if (const auto *CStmt = Result.Nodes.getNodeAs<CompoundStmt>("compound"))
    missingBracesCheck(*Result.SourceManager, CStmt);
}

} // namespace readability
} // namespace tidy
} // namespace clang
