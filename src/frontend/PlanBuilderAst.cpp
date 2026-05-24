#include "PlanBuilderAst.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ParentMapContext.h>
#include <llvm/Support/Casting.h>

#include <algorithm>

namespace azteca::inspect_frontend
{

[[nodiscard]] std::string access_to_string(clang::AccessSpecifier access)
{
	switch (access)
	{
		case clang::AS_public:
			return "public";
		case clang::AS_protected:
			return "protected";
		case clang::AS_private:
			return "private";
		case clang::AS_none:
			return "none";
	}

	return "none";
}

[[nodiscard]] bool is_same_record(clang::CXXRecordDecl const* lhs, clang::CXXRecordDecl const* rhs)
{
	if (lhs == nullptr || rhs == nullptr)
	{
		return false;
	}

	return lhs->getCanonicalDecl() == rhs->getCanonicalDecl();
}

[[nodiscard]] bool contains_raw_this_expr(clang::Stmt const* statement)
{
	if (statement == nullptr)
	{
		return false;
	}

	if (auto const* expression = llvm::dyn_cast<clang::Expr>(statement))
	{
		auto const* ignored = expression->IgnoreParenImpCasts();
		if (llvm::isa<clang::CXXThisExpr>(ignored))
		{
			return true;
		}

		if (llvm::isa<clang::MemberExpr>(ignored))
		{
			return false;
		}
	}

	return std::ranges::any_of(statement->children(),
	    [](clang::Stmt const* child)
	    {
		    return contains_raw_this_expr(child);
	    });
}

[[nodiscard]] bool returns_this_identity(clang::Expr const* expression)
{
	if (expression == nullptr)
	{
		return false;
	}

	auto const* ignored = expression->IgnoreParenImpCasts();
	if (llvm::isa<clang::CXXThisExpr>(ignored))
	{
		return true;
	}

	if (auto const* unary_operator = llvm::dyn_cast<clang::UnaryOperator>(ignored))
	{
		return unary_operator->getOpcode() == clang::UO_Deref &&
		    llvm::isa<clang::CXXThisExpr>(unary_operator->getSubExpr()->IgnoreParenImpCasts());
	}

	return false;
}

[[nodiscard]] bool returns_deref_this_identity(clang::Expr const* expression)
{
	if (expression == nullptr)
	{
		return false;
	}

	auto const* ignored = expression->IgnoreParenImpCasts();
	auto const* unary_operator = llvm::dyn_cast<clang::UnaryOperator>(ignored);
	return unary_operator != nullptr && unary_operator->getOpcode() == clang::UO_Deref &&
	    llvm::isa<clang::CXXThisExpr>(unary_operator->getSubExpr()->IgnoreParenImpCasts());
}

[[nodiscard]] bool expression_contains(clang::Stmt const* root, clang::Stmt const* needle)
{
	if (root == nullptr || needle == nullptr)
	{
		return false;
	}

	if (root == needle)
	{
		return true;
	}

	return std::ranges::any_of(root->children(),
	    [needle](clang::Stmt const* child)
	    {
		    return expression_contains(child, needle);
	    });
}

[[nodiscard]] bool has_this_base(clang::Expr const* expression)
{
	if (expression == nullptr)
	{
		return false;
	}

	auto const* ignored = expression->IgnoreParenImpCasts();
	if (llvm::isa<clang::CXXThisExpr>(ignored))
	{
		return true;
	}

	if (auto const* member = llvm::dyn_cast<clang::MemberExpr>(ignored))
	{
		return has_this_base(member->getBase());
	}

	return false;
}

[[nodiscard]] bool has_explicit_this_base(clang::MemberExpr const& member)
{
	if (member.isImplicitAccess())
	{
		return false;
	}

	return has_this_base(member.getBase());
}

[[nodiscard]] bool has_auto_type(clang::QualType type)
{
	return type.getTypePtrOrNull() != nullptr && type->getContainedAutoType() != nullptr;
}

[[nodiscard]] bool has_overload_set(clang::NamedDecl const& declaration)
{
	auto const* context = declaration.getDeclContext();
	if (context == nullptr || declaration.getDeclName().isEmpty())
	{
		return false;
	}

	auto declarations = context->lookup(declaration.getDeclName());
	auto count = 0;
	for (auto const* candidate : declarations)
	{
		if (llvm::isa<clang::FunctionDecl>(candidate) ||
		    llvm::isa<clang::FunctionTemplateDecl>(candidate))
		{
			++count;
		}
	}

	return count > 1;
}

[[nodiscard]] clang::FieldDecl const* dependency_base_field(clang::CXXMemberCallExpr const& call)
{
	auto const* callee_expression = call.getCallee();
	if (callee_expression == nullptr)
	{
		return nullptr;
	}

	auto const* member_expression =
	    llvm::dyn_cast<clang::MemberExpr>(callee_expression->IgnoreParenImpCasts());
	if (member_expression == nullptr)
	{
		return nullptr;
	}

	auto const* base = member_expression->getBase();
	if (base == nullptr)
	{
		return nullptr;
	}

	auto const* base_member = llvm::dyn_cast<clang::MemberExpr>(base->IgnoreParenImpCasts());
	if (base_member == nullptr || !has_this_base(base_member->getBase()))
	{
		return nullptr;
	}

	return llvm::dyn_cast<clang::FieldDecl>(base_member->getMemberDecl());
}

[[nodiscard]] bool result_is_ignored(clang::ASTContext& context, clang::Expr const& expression)
{
	auto parents = context.getParents(expression);
	if (parents.empty())
	{
		return false;
	}

	auto const& parent = *parents.begin();
	if (parent.get<clang::CompoundStmt>() != nullptr)
	{
		return true;
	}

	if (auto const* cleanup = parent.get<clang::ExprWithCleanups>())
	{
		return result_is_ignored(context, *cleanup);
	}

	if (auto const* cast = parent.get<clang::ImplicitCastExpr>())
	{
		return result_is_ignored(context, *cast);
	}

	return false;
}

[[nodiscard]] bool is_std_move_or_forward(clang::FunctionDecl const& callee)
{
	auto qualified_name = callee.getQualifiedNameAsString();
	return qualified_name == "std::move" || qualified_name == "std::forward" ||
	    ((qualified_name.ends_with("::move") || qualified_name.ends_with("::forward")) &&
	        qualified_name.find("std::") != std::string::npos);
}

} // namespace azteca::inspect_frontend
