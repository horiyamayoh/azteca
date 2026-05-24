#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/Type.h>
#include <clang/Basic/SourceLocation.h>

#include <string>

#include "azteca/ExtractionPlan.hpp"
#include "azteca/SourceLocation.hpp"

namespace azteca::inspect_frontend
{

[[nodiscard]] std::string type_string(clang::ASTContext const& context, clang::QualType type);

[[nodiscard]] azteca::SourceLocation source_location(
    clang::ASTContext const& context, clang::SourceLocation location);

[[nodiscard]] azteca::SourceRange source_range(
    clang::ASTContext const& context, clang::SourceRange range);

[[nodiscard]] std::string source_text(clang::ASTContext const& context, clang::SourceRange range);

[[nodiscard]] PlanEvidence make_evidence(clang::ASTContext const& context, std::string rule_id,
    std::string reason, clang::SourceRange range, bool conservative = false,
    std::string certainty = "certain");

} // namespace azteca::inspect_frontend
