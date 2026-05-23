#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>

#include <string>
#include <vector>

#include "azteca/MethodSpec.hpp"

namespace azteca
{

[[nodiscard]] bool method_matches_spec(
    MethodSpec const& spec, clang::ASTContext const& context, clang::CXXMethodDecl const& method);

[[nodiscard]] std::string method_signature(
    clang::ASTContext const& context, clang::CXXMethodDecl const& method);

[[nodiscard]] std::vector<std::string> method_template_arguments(
    clang::ASTContext const& context, clang::CXXMethodDecl const& method);

} // namespace azteca
