#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/Type.h>

#include <string>

namespace azteca::inspect_frontend
{

[[nodiscard]] std::string access_to_string(clang::AccessSpecifier access);
[[nodiscard]] bool is_same_record(clang::CXXRecordDecl const* lhs, clang::CXXRecordDecl const* rhs);
[[nodiscard]] bool contains_raw_this_expr(clang::Stmt const* statement);
[[nodiscard]] bool returns_this_identity(clang::Expr const* expression);
[[nodiscard]] bool returns_deref_this_identity(clang::Expr const* expression);
[[nodiscard]] bool expression_contains(clang::Stmt const* root, clang::Stmt const* needle);
[[nodiscard]] bool has_this_base(clang::Expr const* expression);
[[nodiscard]] bool has_explicit_this_base(clang::MemberExpr const& member);
[[nodiscard]] bool has_auto_type(clang::QualType type);
[[nodiscard]] bool has_overload_set(clang::NamedDecl const& declaration);
[[nodiscard]] clang::FieldDecl const* dependency_base_field(clang::CXXMemberCallExpr const& call);
[[nodiscard]] bool result_is_ignored(clang::ASTContext& context, clang::Expr const& expression);
[[nodiscard]] bool is_std_move_or_forward(clang::FunctionDecl const& callee);

} // namespace azteca::inspect_frontend
