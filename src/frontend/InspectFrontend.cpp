#include "azteca/InspectFrontend.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/StmtCXX.h>
#include <clang/AST/Type.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "../collect/InspectCollector.hpp"
#include "../resolve/MethodSelector.hpp"
#include "azteca/MethodSpec.hpp"

namespace azteca
{
namespace
{

struct ToolState
{
	MethodSpec spec;
	std::vector<ExtractionPlan> plans;
	std::optional<ExtractionPlan> template_fallback_plan;
	Diagnostics diagnostics;
	bool selected_template_without_explicit_args{false};
};

using inspect_collect::append_events;
using inspect_collect::build_path_burden;
using inspect_collect::deduplicate_events;
using inspect_collect::deduplicate_strings;
using inspect_collect::operator_port_name;
using inspect_collect::PathEvent;
using inspect_collect::PathState;
using inspect_collect::remove_trailing_underscore;
using inspect_collect::replace_colons;
using inspect_collect::sanitize_identifier;

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

[[nodiscard]] bool contains_this_expr(clang::Stmt const* statement)
{
	if (statement == nullptr)
	{
		return false;
	}

	if (llvm::isa<clang::CXXThisExpr>(statement))
	{
		return true;
	}

	return std::ranges::any_of(statement->children(),
	    [](clang::Stmt const* child)
	    {
		    return contains_this_expr(child);
	    });
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

[[nodiscard]] std::string type_string(clang::ASTContext const& context, clang::QualType type)
{
	clang::PrintingPolicy policy(context.getLangOpts());
	policy.SuppressTagKeyword = true;
	policy.SuppressUnwrittenScope = true;
	return type.getAsString(policy);
}

[[nodiscard]] bool is_std_move_or_forward(clang::FunctionDecl const& callee)
{
	auto qualified_name = callee.getQualifiedNameAsString();
	return qualified_name == "std::move" || qualified_name == "std::forward" ||
	    ((qualified_name.ends_with("::move") || qualified_name.ends_with("::forward")) &&
	        qualified_name.find("std::") != std::string::npos);
}

[[nodiscard]] SourceLocation source_location(
    clang::ASTContext const& context, clang::SourceLocation location)
{
	if (location.isInvalid())
	{
		return {};
	}

	auto const& source_manager = context.getSourceManager();
	auto spelling_location = source_manager.getSpellingLoc(location);
	if (spelling_location.isInvalid())
	{
		return {};
	}

	auto presumed = source_manager.getPresumedLoc(spelling_location);
	if (presumed.isInvalid())
	{
		return {};
	}

	return {
	    .file = presumed.getFilename(),
	    .line = presumed.getLine(),
	    .column = presumed.getColumn(),
	};
}

[[nodiscard]] azteca::SourceRange source_range(
    clang::ASTContext const& context, clang::SourceRange range)
{
	if (range.isInvalid())
	{
		return {};
	}

	auto const& source_manager = context.getSourceManager();
	auto const& language_options = context.getLangOpts();
	auto end_location =
	    clang::Lexer::getLocForEndOfToken(range.getEnd(), 0, source_manager, language_options);
	if (end_location.isInvalid())
	{
		end_location = range.getEnd();
	}

	return {
	    .begin = source_location(context, range.getBegin()),
	    .end = source_location(context, end_location),
	};
}

[[nodiscard]] std::string source_text(clang::ASTContext const& context, clang::SourceRange range)
{
	if (range.isInvalid())
	{
		return "";
	}

	auto const& source_manager = context.getSourceManager();
	auto const& language_options = context.getLangOpts();
	bool invalid = false;
	auto text = clang::Lexer::getSourceText(
	    clang::CharSourceRange::getTokenRange(range), source_manager, language_options, &invalid);

	if (invalid)
	{
		return "";
	}

	return text.str();
}

class PlanBuilder;

class CallEventVisitor : public clang::RecursiveASTVisitor<CallEventVisitor>
{
   public:
	CallEventVisitor(PlanBuilder& builder, std::vector<PathEvent>& events);

	bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr* call);
	bool VisitCXXOperatorCallExpr(clang::CXXOperatorCallExpr* call);
	bool VisitCallExpr(clang::CallExpr* call);

   private:
	PlanBuilder& builder_;
	std::vector<PathEvent>& events_;
};

class PlanBuilder : public clang::RecursiveASTVisitor<PlanBuilder>
{
   public:
	PlanBuilder(clang::ASTContext& context, clang::CXXMethodDecl const& method)
	    : context_(context), method_(method)
	{
	}

	[[nodiscard]] ExtractionPlan build()
	{
		initialize_rule_coverage();
		plan_.target.qualified_name = method_.getQualifiedNameAsString();
		plan_.target.signature = method_signature(context_, method_);
		plan_.target.source_file = source_location(context_, method_.getLocation()).file;
		plan_.target.line = source_location(context_, method_.getLocation()).line;
		plan_.schema_version = 2;
		plan_.result = "extracted";
		plan_.confidence = "high";
		plan_.mmir.target_name = plan_.target.qualified_name;
		record_method_qualifiers();

		if (auto const* body = method_.getBody())
		{
			TraverseStmt(const_cast<clang::Stmt*>(body));
			remove_dependency_object_fields();
			plan_.paths = analyze_paths(*body);
			deduplicate_strings(plan_.control_flow_summary.conservative_reasons);
			if (!validate_mmir(plan_.mmir, plan_.diagnostics))
			{
				plan_.result = "invalid-plan";
			}
		}
		else
		{
			plan_.result = "not-yet-implemented";
			plan_.diagnostics.add(DiagnosticSeverity::kWarning, "AZTECA_METHOD_NO_BODY",
			    "target method has no body available");
		}

		finalize_deterministic_order();
		build_gtest_preview();
		return plan_;
	}

	bool VisitMemberExpr(clang::MemberExpr* member)
	{
		auto const* field = llvm::dyn_cast<clang::FieldDecl>(member->getMemberDecl());
		if (field == nullptr)
		{
			add_shape_observation(*member);
			return true;
		}

		if (!has_this_base(member->getBase()))
		{
			add_shape_observation(*member);
			return true;
		}

		if (is_dependency_object_base(*member))
		{
			return true;
		}

		auto access = classify_field_access(*member);
		auto const* field_parent = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(field->getParent());
		auto const kIsBaseField = !is_same_record(field_parent, method_.getParent());
		auto const* rule_id = "LR-003";
		auto const* reason = "receiver field mutation";
		if (kIsBaseField)
		{
			rule_id = "LR-011";
			reason = "base receiver field access";
		}
		else if (access == FieldAccess::kAddress)
		{
			rule_id = "LR-029";
			reason = "receiver field address taken";
		}
		else if (access == FieldAccess::kRead)
		{
			if (has_explicit_this_base(*member))
			{
				rule_id = "LR-002";
				reason = "explicit this receiver field read";
			}
			else
			{
				rule_id = "LR-001";
				reason = "receiver field read";
			}
		}
		auto evidence = make_evidence(rule_id, reason, member->getSourceRange());
		add_mmir(kIsBaseField ? MmirNodeKind::kBaseFieldRef : MmirNodeKind::kFieldRef,
		    field->getNameAsString(), member->getExprLoc(), member->getSourceRange(), evidence,
		    field->getQualifiedNameAsString());
		add_receiver_field(*field, access, evidence);
		add_semantic_feature(kIsBaseField ? "base_state" : "receiver_state",
		    kIsBaseField ? ConstructHandling::kModeled : ConstructHandling::kSupported, reason,
		    evidence);
		add_envelope_requirement(kIsBaseField ? EnvelopeRequirementKind::kBaseState
		                                      : EnvelopeRequirementKind::kSelfState,
		    reason, field->getQualifiedNameAsString(), evidence);

		if (!kIsBaseField &&
		    (field->getAccess() == clang::AS_private || field->getAccess() == clang::AS_protected))
		{
			auto access_evidence = make_evidence("LR-048",
			    "private/protected receiver state is modeled without changing product access",
			    member->getSourceRange());
			add_semantic_feature("access_control", ConstructHandling::kModeled,
			    "inspect reports non-public receiver state without #define private public",
			    access_evidence);
		}

		if (access == FieldAccess::kAddress)
		{
			add_envelope_requirement(EnvelopeRequirementKind::kAddressableCell,
			    "address-taken field must be represented as a cell",
			    field->getQualifiedNameAsString(), evidence);
			add_construct("field address taking", ConstructHandling::kModeled,
			    "field address is unit-testable through addressable cell modeling",
			    {"model field as azteca::cell", "avoid exposing a fake product object address"},
			    member->getExprLoc(), evidence);
		}

		if (field->isBitField())
		{
			mark_conservative("AZTECA_BIT_FIELD_PARTIAL",
			    "bit-field receiver state is partial in Phase A", field->getLocation());
		}

		if (field->getParent() != nullptr && field->getParent()->isAnonymousStructOrUnion())
		{
			mark_conservative("AZTECA_ANONYMOUS_UNION_PARTIAL",
			    "anonymous struct or union receiver state is partial in Phase A",
			    field->getLocation());
		}

		return true;
	}

	bool VisitDeclRefExpr(clang::DeclRefExpr* reference)
	{
		if (llvm::isa<clang::ParmVarDecl>(reference->getDecl()))
		{
			add_mmir(MmirNodeKind::kArgRef, reference->getDecl()->getNameAsString(),
			    reference->getExprLoc(), reference->getSourceRange(),
			    make_evidence("MMIR-ARG", "method argument reference", reference->getSourceRange()),
			    reference->getDecl()->getQualifiedNameAsString());
		}
		else if (auto const* variable = llvm::dyn_cast<clang::VarDecl>(reference->getDecl()))
		{
			if (variable->isLocalVarDecl())
			{
				add_mmir(MmirNodeKind::kLocalRef, reference->getDecl()->getNameAsString(),
				    reference->getExprLoc(), reference->getSourceRange(),
				    make_evidence(
				        "MMIR-LOCAL", "local variable reference", reference->getSourceRange()),
				    reference->getDecl()->getQualifiedNameAsString());
			}
			else if (variable->hasGlobalStorage())
			{
				auto evidence =
				    make_evidence("LR-010", "global variable reference is modeled as env state",
				        reference->getSourceRange());
				add_mmir(MmirNodeKind::kGlobalRef, variable->getNameAsString(),
				    reference->getExprLoc(), reference->getSourceRange(), evidence,
				    variable->getQualifiedNameAsString());
				add_semantic_feature("global_state", ConstructHandling::kModeled,
				    "global variable read/write should be represented by environment state",
				    evidence);
				add_envelope_requirement(EnvelopeRequirementKind::kGlobalEnvironment,
				    "global state is external to receiver self",
				    variable->getQualifiedNameAsString(), evidence);
			}
		}

		return true;
	}

	bool VisitVarDecl(clang::VarDecl* variable)
	{
		if (variable->isLocalVarDecl())
		{
			local_types_[variable->getCanonicalDecl()] = type_string(context_, variable->getType());
			auto evidence =
			    make_evidence("LR-014", "local variable declaration", variable->getSourceRange());
			add_semantic_feature("local_variable", ConstructHandling::kSupported,
			    "local variable declaration is part of inspect MMIR", evidence);
			if (has_auto_type(variable->getType()))
			{
				auto auto_evidence = make_evidence(
				    "LR-043", "auto type is resolved by Clang Sema", variable->getSourceRange());
				add_semantic_feature("auto_type", ConstructHandling::kSupported,
				    "auto is reported after semantic type resolution", auto_evidence);
			}
			record_dependency_local(*variable);
		}
		return true;
	}

	bool VisitBinaryOperator(clang::BinaryOperator* binary_operator)
	{
		add_mmir(binary_operator->isAssignmentOp() ? MmirNodeKind::kAssign : MmirNodeKind::kBinary,
		    binary_operator->getOpcodeStr().str(), binary_operator->getOperatorLoc(),
		    binary_operator->getSourceRange(),
		    make_evidence(binary_operator->isAssignmentOp() ? "LR-003" : "LR-006",
		        binary_operator->isAssignmentOp() ? "assignment expression" : "binary expression",
		        binary_operator->getSourceRange()));
		if ((binary_operator->getOpcode() == clang::BO_EQ ||
		        binary_operator->getOpcode() == clang::BO_NE) &&
		    (contains_raw_this_expr(binary_operator->getLHS()) ||
		        contains_raw_this_expr(binary_operator->getRHS())))
		{
			add_object_ref_requirement("this identity comparison",
			    source_text(context_, binary_operator->getSourceRange()),
			    binary_operator->getOperatorLoc(), binary_operator->getSourceRange(), "LR-020");
		}
		return true;
	}

	bool VisitConditionalOperator(clang::ConditionalOperator* conditional_operator)
	{
		auto evidence = make_evidence("LR-041",
		    "ternary conditional expression is tracked as expression control flow",
		    conditional_operator->getSourceRange());
		add_mmir(MmirNodeKind::kConditional, "?:", conditional_operator->getQuestionLoc(),
		    conditional_operator->getSourceRange(), evidence);
		add_semantic_feature("conditional_operator", ConstructHandling::kSupported,
		    "ternary expression is part of local expression semantics", evidence);
		return true;
	}

	bool VisitBinaryConditionalOperator(clang::BinaryConditionalOperator* conditional_operator)
	{
		auto evidence =
		    make_evidence("LR-041", "GNU binary conditional expression is tracked conservatively",
		        conditional_operator->getSourceRange(), true, "conservative");
		add_mmir(MmirNodeKind::kConditional, "?:", conditional_operator->getQuestionLoc(),
		    conditional_operator->getSourceRange(), evidence);
		add_semantic_feature("conditional_operator", ConstructHandling::kConservative,
		    "binary conditional expression is a compiler-extension expression form", evidence);
		add_construct("binary conditional operator", ConstructHandling::kConservative,
		    "compiler-extension conditional syntax requires dedicated lowering",
		    {"standard ternary lowering when available", "dependency boundary for extension use"},
		    conditional_operator->getQuestionLoc(), evidence);
		return true;
	}

	bool VisitUnaryOperator(clang::UnaryOperator* unary_operator)
	{
		if (unary_operator->getOpcode() == clang::UO_AddrOf)
		{
			auto const* ignored = unary_operator->getSubExpr()->IgnoreParenImpCasts();
			if (auto const* method = member_function_pointer_target(ignored); method != nullptr)
			{
				auto const kTargetClass = method->getParent() == nullptr
				    ? std::string{"<unknown class>"}
				    : method->getParent()->getQualifiedNameAsString();
				auto const kTarget =
				    method->getQualifiedNameAsString() + " " + method_signature(context_, *method);
				auto const kPointerType = type_string(context_, unary_operator->getType());
				auto reason = "pointer-to-member target " + kTargetClass +
				    "::" + method->getNameAsString() + " has signature " +
				    method_signature(context_, *method) + " and pointer type " + kPointerType;
				auto evidence = make_evidence("LR-030",
				    "address of member function is reported with pointer-to-member type details",
				    unary_operator->getSourceRange(), true, "conservative");
				add_mmir(MmirNodeKind::kUnsupported, "member_function_pointer",
				    unary_operator->getOperatorLoc(), unary_operator->getSourceRange(), evidence,
				    {}, kTarget);
				add_construct("member function pointer", ConstructHandling::kNotYetImplemented,
				    std::move(reason), {"dependency boundary", "live validation"},
				    unary_operator->getOperatorLoc(), evidence);
				return true;
			}
		}

		add_mmir(MmirNodeKind::kUnary,
		    clang::UnaryOperator::getOpcodeStr(unary_operator->getOpcode()).str(),
		    unary_operator->getOperatorLoc(), unary_operator->getSourceRange(),
		    make_evidence("LR-006", "unary expression", unary_operator->getSourceRange()));
		return true;
	}

	bool VisitIfStmt(clang::IfStmt* if_statement)
	{
		plan_.control_flow_summary.has_if = true;
		add_mmir(MmirNodeKind::kIf, "if", if_statement->getIfLoc(), if_statement->getSourceRange(),
		    make_evidence("LR-005", "if statement", if_statement->getSourceRange()));
		return true;
	}

	bool VisitSwitchStmt(clang::SwitchStmt* switch_statement)
	{
		plan_.control_flow_summary.has_switch = true;
		auto evidence = make_evidence("LR-015",
		    "switch statement is segmented into case/default paths in Phase A",
		    switch_statement->getSourceRange());
		add_mmir(MmirNodeKind::kSwitch, "switch", switch_statement->getSwitchLoc(),
		    switch_statement->getSourceRange(), evidence);
		add_semantic_feature("switch_control_flow", ConstructHandling::kSupported,
		    "switch case/default labels are represented as path branches", evidence);
		return true;
	}

	bool VisitForStmt(clang::ForStmt* statement)
	{
		return record_loop(*statement, statement->getForLoc(), "for loop", "LR-015");
	}

	bool VisitWhileStmt(clang::WhileStmt* statement)
	{
		return record_loop(*statement, statement->getWhileLoc(), "while loop", "LR-015");
	}

	bool VisitDoStmt(clang::DoStmt* statement)
	{
		return record_loop(*statement, statement->getDoLoc(), "do loop", "LR-015");
	}

	bool VisitCXXForRangeStmt(clang::CXXForRangeStmt* statement)
	{
		plan_.control_flow_summary.has_range_for = true;
		return record_loop(*statement, statement->getBeginLoc(), "range-for loop", "LR-016");
	}

	bool VisitBreakStmt(clang::BreakStmt* statement)
	{
		auto evidence = make_evidence(
		    "LR-046", "break statement is tracked in control flow", statement->getSourceRange());
		add_mmir(MmirNodeKind::kBreak, "break", statement->getBreakLoc(),
		    statement->getSourceRange(), evidence);
		add_semantic_feature("break_continue", ConstructHandling::kSupported,
		    "break statement is reported as control-flow intent", evidence);
		return true;
	}

	bool VisitContinueStmt(clang::ContinueStmt* statement)
	{
		auto evidence = make_evidence(
		    "LR-046", "continue statement is tracked in control flow", statement->getSourceRange());
		add_mmir(MmirNodeKind::kContinue, "continue", statement->getContinueLoc(),
		    statement->getSourceRange(), evidence);
		add_semantic_feature("break_continue", ConstructHandling::kSupported,
		    "continue statement is reported as control-flow intent", evidence);
		return true;
	}

	bool VisitReturnStmt(clang::ReturnStmt* return_statement)
	{
		plan_.control_flow_summary.has_return = true;
		add_mmir(MmirNodeKind::kReturn, "return", return_statement->getReturnLoc(),
		    return_statement->getSourceRange(),
		    make_evidence("LR-004", "return statement", return_statement->getSourceRange()));
		if (returns_this_identity(return_statement->getRetValue()))
		{
			add_object_ref_requirement("return this",
			    source_text(context_, return_statement->getSourceRange()),
			    return_statement->getReturnLoc(), return_statement->getSourceRange(), "LR-021");
			auto evidence =
			    make_evidence("LR-021", "return this or return *this requires object_ref modeling",
			        return_statement->getSourceRange());
			add_construct("return this", ConstructHandling::kModeled,
			    "receiver identity is returned as object_ref instead of a fake C pointer",
			    {"object_ref return", "live validation for API-level pointer behavior"},
			    return_statement->getReturnLoc(), evidence);
			if (returns_deref_this_identity(return_statement->getRetValue()))
			{
				auto deref_evidence =
				    make_evidence("LR-022", "return *this requires self reference modeling",
				        return_statement->getSourceRange());
				add_construct("return *this", ConstructHandling::kModeled,
				    "receiver reference is mapped to self view instead of a fake C reference",
				    {"self reference view", "live validation for API-level reference behavior"},
				    return_statement->getReturnLoc(), deref_evidence);
			}
		}
		return true;
	}

	bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr* call)
	{
		if (auto const* callee = call->getMethodDecl();
		    llvm::isa_and_nonnull<clang::CXXDestructorDecl>(callee))
		{
			auto evidence = make_evidence("LR-027",
			    "explicit destructor call requires lifetime state", call->getSourceRange());
			add_mmir(MmirNodeKind::kLifetimeOp, "destructor_call", call->getExprLoc(),
			    call->getSourceRange(), evidence, "lifetime");
			add_semantic_feature("lifetime_operation", ConstructHandling::kModeled,
			    "explicit destructor call is represented as lifetime intent", evidence);
			add_envelope_requirement(EnvelopeRequirementKind::kLifetimeState,
			    "explicit destructor call requires lifetime tracking",
			    source_text(context_, call->getSourceRange()), evidence);
			add_construct("explicit destructor call", ConstructHandling::kModeled,
			    "object lifetime termination is modeled, not executed on fake storage",
			    {"lifetime_state", "destructor kernel when available", "live validation"},
			    call->getExprLoc(), evidence);
			return true;
		}

		auto port = dependency_port_for_call(*call);
		if (port.has_value())
		{
			add_dependency_port(*port);
			auto kind = port->evidence.rule_id == "LR-012" ? MmirNodeKind::kDispatchCall
			                                               : MmirNodeKind::kBoundaryCall;
			add_mmir(kind, port->name, call->getExprLoc(), call->getSourceRange(), port->evidence,
			    port->name, port->original_callee);
			if (port->evidence.rule_id == "LR-012")
			{
				add_semantic_feature("virtual_dispatch", ConstructHandling::kModeled,
				    "virtual call is represented by dispatch table observation", port->evidence);
				add_envelope_requirement(EnvelopeRequirementKind::kDispatchTable,
				    "virtual call requires dispatch table", port->original_callee, port->evidence);
				add_envelope_requirement(EnvelopeRequirementKind::kObjectRef,
				    "virtual dispatch uses receiver identity", port->original_callee,
				    port->evidence);
			}
			else
			{
				add_envelope_requirement(EnvelopeRequirementKind::kDependencyBoundary,
				    "dependency call is represented by transcript port", port->original_callee,
				    port->evidence);
			}
		}
		else if (!is_shape_call(*call))
		{
			add_unclassified_call(*call);
		}

		if (contains_this_argument(*call))
		{
			add_object_ref_requirement("this passed to dependency",
			    source_text(context_, call->getSourceRange()), call->getExprLoc(),
			    call->getSourceRange(), "LR-020");
		}

		return true;
	}

	bool VisitCallExpr(clang::CallExpr* call)
	{
		if (llvm::isa<clang::CXXMemberCallExpr>(call) ||
		    llvm::isa<clang::CXXOperatorCallExpr>(call))
		{
			return true;
		}

		if (auto const* callee = call->getDirectCallee();
		    callee != nullptr && is_std_move_or_forward(*callee))
		{
			auto evidence = make_evidence("LR-044",
			    "std::move/std::forward is a value-category cast, not a dependency call",
			    call->getSourceRange());
			add_mmir(MmirNodeKind::kCast, callee->getNameAsString(), call->getExprLoc(),
			    call->getSourceRange(), evidence);
			add_semantic_feature("value_category_cast", ConstructHandling::kSupported,
			    "std::move/std::forward is tracked without creating a transcript port", evidence);
			return true;
		}

		auto port = dependency_port_for_call(*call);
		if (port.has_value())
		{
			add_dependency_port(*port);
			add_mmir(MmirNodeKind::kBoundaryCall, port->name, call->getExprLoc(),
			    call->getSourceRange(), port->evidence, port->name, port->original_callee);
			add_envelope_requirement(EnvelopeRequirementKind::kDependencyBoundary,
			    "function call is represented by transcript port", port->original_callee,
			    port->evidence);
		}
		else
		{
			add_unclassified_call(*call);
		}

		if (contains_this_argument(*call))
		{
			add_object_ref_requirement("this passed to dependency",
			    source_text(context_, call->getSourceRange()), call->getExprLoc(),
			    call->getSourceRange(), "LR-020");
		}

		return true;
	}

	bool VisitCXXOperatorCallExpr(clang::CXXOperatorCallExpr* call)
	{
		auto const* callee = call->getDirectCallee();
		if (auto const* method = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(callee);
		    method != nullptr && method->getParent() != nullptr && method->getParent()->isLambda())
		{
			auto evidence = make_evidence("LR-017",
			    "lambda call operator is local logic in Phase A inspect", call->getSourceRange());
			add_mmir(MmirNodeKind::kLambda, "lambda_call", call->getExprLoc(),
			    call->getSourceRange(), evidence);
			add_semantic_feature("lambda_call", ConstructHandling::kSupported,
			    "lambda invocation is kept with local logic unless the closure escapes", evidence);
			return true;
		}

		auto original_symbol =
		    callee == nullptr ? std::string{"operator"} : callee->getQualifiedNameAsString();
		auto evidence = make_evidence("LR-013",
		    "overloaded operator is resolved by Clang and represented as a boundary candidate",
		    call->getSourceRange());
		add_mmir(MmirNodeKind::kOperatorCall,
		    original_symbol.empty() ? std::string{"operator"} : original_symbol, call->getExprLoc(),
		    call->getSourceRange(), evidence, sanitize_identifier(original_symbol),
		    original_symbol);
		add_semantic_feature("overloaded_operator", ConstructHandling::kBoundary,
		    "operator call is resolved and tracked as a dependency candidate", evidence);
		add_construct("overloaded operator", ConstructHandling::kBoundary,
		    "operator semantics are preserved through resolved callee metadata",
		    {"direct lowering for built-in operators",
		        "dependency boundary for overloaded operators"},
		    call->getExprLoc(), evidence);
		if (callee != nullptr)
		{
			auto const* method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
			auto kind = inspect_collect::classify_dependency_kind(
			    callee->getReturnType()->isVoidType() || result_is_ignored(context_, *call),
			    method == nullptr || method->isConst());

			add_dependency_port({
			    .kind = kind,
			    .name = stable_port_name(operator_port_name(original_symbol), *callee),
			    .original_callee = original_symbol,
			    .return_type = type_string(context_, callee->getReturnType()),
			    .argument_types = call_argument_types(*call),
			    .location = source_location(context_, call->getExprLoc()),
			    .evidence = evidence,
			});
			add_envelope_requirement(EnvelopeRequirementKind::kDependencyBoundary,
			    "overloaded operator is represented by a dependency boundary", original_symbol,
			    evidence);
		}
		return true;
	}

	bool VisitLambdaExpr(clang::LambdaExpr* lambda)
	{
		auto const kCapturesThis = std::ranges::any_of(lambda->captures(),
		    [](clang::LambdaCapture const& capture)
		    {
			    return capture.capturesThis();
		    });
		auto evidence = make_evidence(kCapturesThis ? "LR-018" : "LR-017",
		    kCapturesThis ? "lambda captures this and requires self capture modeling"
		                  : "lambda does not capture this",
		    lambda->getSourceRange(), kCapturesThis, kCapturesThis ? "conservative" : "certain");
		add_mmir(MmirNodeKind::kLambda, kCapturesThis ? "lambda_this_capture" : "lambda",
		    lambda->getBeginLoc(), lambda->getSourceRange(), evidence);
		add_semantic_feature("lambda",
		    kCapturesThis ? ConstructHandling::kModeled : ConstructHandling::kSupported,
		    kCapturesThis ? "this capture maps to self in generated kernel"
		                  : "lambda body does not require receiver identity",
		    evidence);
		if (kCapturesThis)
		{
			add_construct("lambda this capture", ConstructHandling::kModeled,
			    "captured this must be rewritten as self capture when it does not escape",
			    {"self capture", "dependency boundary when closure escapes"}, lambda->getBeginLoc(),
			    evidence);
			add_envelope_requirement(EnvelopeRequirementKind::kSelfState,
			    "lambda this capture requires access to receiver self", "lambda", evidence);
		}
		return true;
	}

	bool VisitDependentScopeDeclRefExpr(clang::DependentScopeDeclRefExpr* expression)
	{
		auto evidence = make_evidence("LR-033",
		    "dependent name cannot be resolved without template instantiation",
		    expression->getSourceRange(), true, "conservative");
		add_mmir(MmirNodeKind::kUnsupported, "dependent_decl_ref", expression->getBeginLoc(),
		    expression->getSourceRange(), evidence);
		add_semantic_feature("dependent_name", ConstructHandling::kNotYetImplemented,
		    "dependent declaration reference requires template instantiation support", evidence);
		add_construct("dependent name", ConstructHandling::kNotYetImplemented,
		    "dependent name resolution is outside Phase A inspect",
		    {"inspect an instantiated non-template method", "future template-aware MMIR"},
		    expression->getBeginLoc(), evidence);
		return true;
	}

	bool VisitCXXDependentScopeMemberExpr(clang::CXXDependentScopeMemberExpr* expression)
	{
		auto evidence = make_evidence("LR-033",
		    "dependent member name cannot be resolved without template instantiation",
		    expression->getSourceRange(), true, "conservative");
		add_mmir(MmirNodeKind::kUnsupported, "dependent_member_ref", expression->getBeginLoc(),
		    expression->getSourceRange(), evidence);
		add_semantic_feature("dependent_name", ConstructHandling::kNotYetImplemented,
		    "dependent member reference requires template instantiation support", evidence);
		add_construct("dependent member name", ConstructHandling::kNotYetImplemented,
		    "dependent member resolution is outside Phase A inspect",
		    {"inspect an instantiated non-template method", "future template-aware MMIR"},
		    expression->getBeginLoc(), evidence);
		return true;
	}

	bool VisitCXXThrowExpr(clang::CXXThrowExpr* throw_expression)
	{
		plan_.control_flow_summary.has_throw = true;
		auto evidence =
		    make_evidence("LR-035", "throw expression is part of unit-observable semantics",
		        throw_expression->getSourceRange());
		add_mmir(MmirNodeKind::kThrow, "throw", throw_expression->getThrowLoc(),
		    throw_expression->getSourceRange(), evidence);
		add_semantic_feature("exception_throw", ConstructHandling::kSupported,
		    "throw expression is preserved as exception intent", evidence);
		add_envelope_requirement(EnvelopeRequirementKind::kExceptionModel,
		    "exception behavior is unit-observable", "throw", evidence);
		return true;
	}

	bool VisitCXXTryStmt(clang::CXXTryStmt* try_statement)
	{
		plan_.control_flow_summary.has_try = true;
		plan_.control_flow_summary.conservative = true;
		plan_.control_flow_summary.conservative_reasons.emplace_back(
		    "try/catch is represented conservatively in Phase A");
		auto evidence = make_evidence("LR-036",
		    "try/catch is represented as conservative exception control flow",
		    try_statement->getSourceRange(), true, "conservative");
		add_mmir(MmirNodeKind::kTry, "try", try_statement->getTryLoc(),
		    try_statement->getSourceRange(), evidence);
		add_semantic_feature("try_catch", ConstructHandling::kConservative,
		    "try/catch paths are summarized conservatively", evidence);
		add_envelope_requirement(EnvelopeRequirementKind::kExceptionModel,
		    "try/catch requires exception-aware lowering", "try", evidence);
		return true;
	}

	bool VisitCXXDynamicCastExpr(clang::CXXDynamicCastExpr* cast)
	{
		auto evidence = make_evidence("LR-023",
		    "dynamic_cast involving receiver state requires type_tag modeling",
		    cast->getSourceRange());
		add_mmir(MmirNodeKind::kCast, "dynamic_cast", cast->getBeginLoc(), cast->getSourceRange(),
		    evidence);
		add_semantic_feature("dynamic_type", ConstructHandling::kModeled,
		    "dynamic_cast is represented through explicit dynamic type model", evidence);
		add_envelope_requirement(EnvelopeRequirementKind::kTypeTag,
		    "dynamic_cast requires type_tag", source_text(context_, cast->getSourceRange()),
		    evidence);
		add_construct("dynamic_cast", ConstructHandling::kModeled,
		    "RTTI-dependent decision is modeled explicitly instead of requiring a fake object",
		    {"type_tag model", "live validation"}, cast->getBeginLoc(), evidence);
		return true;
	}

	bool VisitCXXReinterpretCastExpr(clang::CXXReinterpretCastExpr* cast)
	{
		auto evidence = make_evidence("LR-025",
		    "reinterpret_cast is represented as byte or dependency boundary",
		    cast->getSourceRange(), true, "conservative");
		add_mmir(MmirNodeKind::kByteView, "reinterpret_cast", cast->getBeginLoc(),
		    cast->getSourceRange(), evidence);
		add_semantic_feature("byte_representation", ConstructHandling::kBoundary,
		    "reinterpret_cast may depend on ABI representation", evidence);
		add_envelope_requirement(EnvelopeRequirementKind::kByteView,
		    "byte representation must not fake product object layout",
		    source_text(context_, cast->getSourceRange()), evidence);
		add_construct("reinterpret_cast", ConstructHandling::kBoundary,
		    "object representation is not reproduced by self state",
		    {"byte_view boundary", "dependency boundary", "live validation"}, cast->getBeginLoc(),
		    evidence);
		return true;
	}

	bool VisitCXXTypeidExpr(clang::CXXTypeidExpr* typeid_expression)
	{
		auto evidence = make_evidence("LR-024",
		    "typeid expression requires explicit type information when dynamic",
		    typeid_expression->getSourceRange());
		add_mmir(MmirNodeKind::kTypeInfo, "typeid", typeid_expression->getBeginLoc(),
		    typeid_expression->getSourceRange(), evidence);
		add_semantic_feature("type_info", ConstructHandling::kModeled,
		    "typeid is represented through type metadata in Phase A", evidence);
		add_envelope_requirement(EnvelopeRequirementKind::kTypeTag,
		    "typeid may require dynamic type metadata",
		    source_text(context_, typeid_expression->getSourceRange()), evidence);
		return true;
	}

	bool VisitCXXDeleteExpr(clang::CXXDeleteExpr* delete_expression)
	{
		auto evidence =
		    make_evidence("LR-026", "delete expression requires lifetime intent modeling",
		        delete_expression->getSourceRange());
		add_mmir(MmirNodeKind::kLifetimeOp, "delete", delete_expression->getBeginLoc(),
		    delete_expression->getSourceRange(), evidence, "lifetime");
		add_semantic_feature("lifetime_operation", ConstructHandling::kModeled,
		    "delete is represented as lifetime intent and effect", evidence);
		add_envelope_requirement(EnvelopeRequirementKind::kLifetimeState,
		    "delete requires lifetime state",
		    source_text(context_, delete_expression->getSourceRange()), evidence);
		add_construct("delete expression", ConstructHandling::kModeled,
		    "storage ownership is modeled as lifetime intent, not actual deallocation",
		    {"lifetime_state", "delete effect", "live validation"},
		    delete_expression->getBeginLoc(), evidence);
		return true;
	}

	bool VisitCXXNewExpr(clang::CXXNewExpr* new_expression)
	{
		auto has_this_placement = std::ranges::any_of(new_expression->placement_arguments(),
		    [](clang::Expr const* argument)
		    {
			    return contains_raw_this_expr(argument);
		    });
		if (!has_this_placement)
		{
			return true;
		}

		auto evidence = make_evidence("LR-028",
		    "placement new involving this requires lifetime reinitialization model",
		    new_expression->getSourceRange());
		add_mmir(MmirNodeKind::kLifetimeOp, "placement_new_this", new_expression->getBeginLoc(),
		    new_expression->getSourceRange(), evidence, "lifetime");
		add_semantic_feature("lifetime_operation", ConstructHandling::kModeled,
		    "placement new on this is represented as lifetime reinitialization intent", evidence);
		add_envelope_requirement(EnvelopeRequirementKind::kLifetimeState,
		    "placement new on this requires lifetime state",
		    source_text(context_, new_expression->getSourceRange()), evidence);
		add_construct("placement new on this", ConstructHandling::kModeled,
		    "lifetime restart is modeled without constructing a fake product object",
		    {"constructor kernel when available", "lifetime_state", "live validation"},
		    new_expression->getBeginLoc(), evidence);
		return true;
	}

	bool VisitCXXConstructExpr(clang::CXXConstructExpr* construct_expression)
	{
		auto const* constructor = construct_expression->getConstructor();
		if (constructor == nullptr || constructor->isTrivial() || constructor->isImplicit())
		{
			return true;
		}

		auto evidence = make_evidence("LR-049",
		    "non-trivial constructor call is represented as construction intent",
		    construct_expression->getSourceRange(), true, "conservative");
		record_constructor_local_shape(*construct_expression, *constructor, evidence);
		add_mmir(MmirNodeKind::kLifetimeOp, "constructor_call", construct_expression->getBeginLoc(),
		    construct_expression->getSourceRange(), evidence, "lifetime",
		    constructor->getQualifiedNameAsString());
		add_semantic_feature("constructor_call", ConstructHandling::kConservative,
		    "constructor effects require construction-aware lowering", evidence);
		add_envelope_requirement(EnvelopeRequirementKind::kLifetimeState,
		    "constructor call requires lifetime/value construction modeling",
		    constructor->getQualifiedNameAsString(), evidence);
		add_construct("constructor call", ConstructHandling::kConservative,
		    "constructor semantics are reported but not code-generated in Phase A",
		    {"value construction model", "recursive constructor analysis when available",
		        "live validation"},
		    construct_expression->getBeginLoc(), evidence);
		return true;
	}

	bool VisitCXXDefaultArgExpr(clang::CXXDefaultArgExpr* expression)
	{
		auto const* parameter = expression->getParam();
		auto parameter_name =
		    parameter == nullptr ? std::string{"<unknown>"} : parameter->getNameAsString();
		auto evidence = make_evidence("LR-042", "default argument use is supplied by Clang Sema",
		    expression->getSourceRange());
		add_mmir(MmirNodeKind::kDefaultArgument, parameter_name, expression->getUsedLocation(),
		    expression->getSourceRange(), evidence);
		add_semantic_feature("default_argument", ConstructHandling::kSupported,
		    "default argument is visible as a semantic call argument", evidence);
		return true;
	}

	bool VisitCXXStaticCastExpr(clang::CXXStaticCastExpr* cast)
	{
		return record_supported_cast(*cast, cast->getBeginLoc(), "static_cast");
	}

	bool VisitCXXFunctionalCastExpr(clang::CXXFunctionalCastExpr* cast)
	{
		return record_supported_cast(*cast, cast->getBeginLoc(), "functional_cast");
	}

	bool VisitCStyleCastExpr(clang::CStyleCastExpr* cast)
	{
		return record_supported_cast(*cast, cast->getBeginLoc(), "c_style_cast");
	}

	bool VisitCXXConstCastExpr(clang::CXXConstCastExpr* cast)
	{
		auto evidence = make_evidence("LR-045",
		    "const_cast may change mutability and is represented as a boundary",
		    cast->getSourceRange(), true, "conservative");
		add_mmir(MmirNodeKind::kCast, "const_cast", cast->getBeginLoc(), cast->getSourceRange(),
		    evidence);
		add_semantic_feature("cast_expression", ConstructHandling::kBoundary,
		    "const_cast requires mutability-aware lowering", evidence);
		add_construct("const_cast", ConstructHandling::kBoundary,
		    "const removal is not approximated through fake object access",
		    {"mutability-aware boundary", "live validation"}, cast->getBeginLoc(), evidence);
		return true;
	}

	bool VisitUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr* expression)
	{
		auto evidence = make_evidence("LR-039",
		    "unevaluated type/value context is tracked without dependency effects",
		    expression->getSourceRange());
		add_mmir(MmirNodeKind::kUnevaluatedContext, "unevaluated_context",
		    expression->getBeginLoc(), expression->getSourceRange(), evidence);
		add_semantic_feature("unevaluated_context", ConstructHandling::kSupported,
		    "sizeof/alignof style expression is tracked without creating effects", evidence);
		return true;
	}

	bool VisitCXXNoexceptExpr(clang::CXXNoexceptExpr* expression)
	{
		auto evidence = make_evidence(
		    "LR-039", "noexcept operand is an unevaluated context", expression->getSourceRange());
		add_mmir(MmirNodeKind::kUnevaluatedContext, "noexcept_expr", expression->getBeginLoc(),
		    expression->getSourceRange(), evidence);
		add_semantic_feature("unevaluated_context", ConstructHandling::kSupported,
		    "noexcept expression is tracked without dependency effects", evidence);
		return true;
	}

	bool VisitDecompositionDecl(clang::DecompositionDecl* declaration)
	{
		if (record_structured_binding_shape(*declaration))
		{
			auto evidence = make_evidence("LR-037",
			    "structured binding fields are expanded from resolved Clang bindings",
			    declaration->getSourceRange());
			add_mmir(MmirNodeKind::kStructuredBinding, declaration->getNameAsString(),
			    declaration->getBeginLoc(), declaration->getSourceRange(), evidence);
			add_semantic_feature("structured_binding", ConstructHandling::kSupported,
			    "structured binding fields are represented as shape observations", evidence);
			return true;
		}

		auto evidence =
		    make_evidence("LR-037", "structured binding is represented conservatively in Phase A",
		        declaration->getSourceRange(), true, "conservative");
		add_mmir(MmirNodeKind::kStructuredBinding, declaration->getNameAsString(),
		    declaration->getBeginLoc(), declaration->getSourceRange(), evidence);
		add_semantic_feature("structured_binding", ConstructHandling::kConservative,
		    "structured binding requires decomposition-aware lowering", evidence);
		add_construct("structured binding", ConstructHandling::kConservative,
		    "decomposition must be mapped before kernel generation",
		    {"decomposition lowering", "shape field expansion"}, declaration->getBeginLoc(),
		    evidence);
		return true;
	}

	bool VisitCoroutineBodyStmt(clang::CoroutineBodyStmt* statement)
	{
		auto evidence = make_evidence("LR-038",
		    "coroutine frame and suspension semantics are not implemented in Phase A",
		    statement->getSourceRange(), true, "conservative");
		add_semantic_feature("coroutine", ConstructHandling::kNotYetImplemented,
		    "coroutine lowering requires a dedicated state model", evidence);
		add_construct("coroutine", ConstructHandling::kNotYetImplemented,
		    "coroutine frame and suspend/resume semantics require a future model",
		    {"live validation", "extract a pure helper from the coroutine body"},
		    statement->getBeginLoc(), evidence);
		return true;
	}

	[[nodiscard]] std::string overload_suffix(clang::FunctionDecl const& callee) const
	{
		if (callee.getNumParams() == 0U)
		{
			return "void";
		}

		std::string suffix;
		for (auto index = unsigned{0}; index < callee.getNumParams(); ++index)
		{
			if (!suffix.empty())
			{
				suffix += "_";
			}
			suffix +=
			    sanitize_identifier(type_string(context_, callee.getParamDecl(index)->getType()));
		}

		return suffix.empty() ? "args" : suffix;
	}

	[[nodiscard]] std::string stable_port_name(
	    std::string const& base_name, clang::FunctionDecl const& callee) const
	{
		auto sanitized = sanitize_identifier(base_name);
		if (sanitized.empty())
		{
			sanitized = "call";
		}

		if (!has_overload_set(callee))
		{
			return sanitized;
		}

		return sanitized + "__" + overload_suffix(callee);
	}

	[[nodiscard]] std::optional<DependencyPort> dependency_port_for_call(
	    clang::CXXMemberCallExpr const& call)
	{
		auto const* callee = call.getMethodDecl();
		if (callee == nullptr)
		{
			return std::nullopt;
		}

		if (callee->isVirtual())
		{
			auto kind = inspect_collect::classify_dependency_kind(
			    callee->getReturnType()->isVoidType() || result_is_ignored(context_, call),
			    callee->isConst());

			return DependencyPort{
			    .kind = kind,
			    .name = "dispatch_" + callee->getNameAsString(),
			    .original_callee = callee->getQualifiedNameAsString(),
			    .return_type = type_string(context_, callee->getReturnType()),
			    .argument_types = call_argument_types(call),
			    .location = source_location(context_, call.getExprLoc()),
			    .evidence = make_evidence("LR-012",
			        "virtual call requires dispatch observation in Phase A", call.getSourceRange()),
			};
		}

		if (is_same_record(callee->getParent(), method_.getParent()) && !callee->isVirtual())
		{
			return DependencyPort{
			    .kind = DependencyKind::kRecursiveCandidate,
			    .name = stable_port_name(callee->getNameAsString(), *callee),
			    .original_callee = callee->getQualifiedNameAsString(),
			    .return_type = type_string(context_, callee->getReturnType()),
			    .argument_types = call_argument_types(call),
			    .location = source_location(context_, call.getExprLoc()),
			    .evidence = make_evidence("LR-007",
			        "same-class nonvirtual helper can be considered for recursive extraction",
			        call.getSourceRange()),
			};
		}

		auto const* base_field = dependency_base_field(call);
		if (base_field == nullptr)
		{
			return std::nullopt;
		}

		dependency_fields_.insert(base_field->getCanonicalDecl());

		auto classification = inspect_collect::classify_member_object_call(
		    callee->getReturnType()->isVoidType() || result_is_ignored(context_, call),
		    callee->isConst());

		auto base_name = remove_trailing_underscore(base_field->getNameAsString());
		return DependencyPort{
		    .kind = classification.kind,
		    .name = stable_port_name(base_name + "_" + callee->getNameAsString(), *callee),
		    .original_callee = callee->getQualifiedNameAsString(),
		    .return_type = type_string(context_, callee->getReturnType()),
		    .argument_types = call_argument_types(call),
		    .location = source_location(context_, call.getExprLoc()),
		    .evidence = make_evidence(std::move(classification.rule_id),
		        std::move(classification.reason), call.getSourceRange()),
		};
	}

	[[nodiscard]] std::optional<DependencyPort> dependency_port_for_call(
	    clang::CallExpr const& call)
	{
		auto const* callee = call.getDirectCallee();
		if (callee == nullptr)
		{
			return std::nullopt;
		}

		if (is_std_move_or_forward(*callee))
		{
			return std::nullopt;
		}

		if (auto const* method = llvm::dyn_cast<clang::CXXMethodDecl>(callee))
		{
			if (!method->isStatic() && !llvm::isa<clang::CXXOperatorCallExpr>(&call))
			{
				return std::nullopt;
			}

			if (!method->isStatic())
			{
				if (method->getParent() != nullptr && method->getParent()->isLambda())
				{
					return std::nullopt;
				}

				auto kind = inspect_collect::classify_dependency_kind(
				    method->getReturnType()->isVoidType() || result_is_ignored(context_, call),
				    method->isConst());

				return DependencyPort{
				    .kind = kind,
				    .name = stable_port_name(
				        operator_port_name(method->getQualifiedNameAsString()), *method),
				    .original_callee = method->getQualifiedNameAsString(),
				    .return_type = type_string(context_, method->getReturnType()),
				    .argument_types = call_argument_types(call),
				    .location = source_location(context_, call.getExprLoc()),
				    .evidence = make_evidence("LR-013",
				        "member overloaded operator is a resolved boundary candidate",
				        call.getSourceRange()),
				};
			}

			auto classification = inspect_collect::classify_static_member_call(
			    method->getReturnType()->isVoidType() || result_is_ignored(context_, call));

			return DependencyPort{
			    .kind = classification.kind,
			    .name =
			        stable_port_name(replace_colons(method->getQualifiedNameAsString()), *method),
			    .original_callee = method->getQualifiedNameAsString(),
			    .return_type = type_string(context_, method->getReturnType()),
			    .argument_types = call_argument_types(call),
			    .location = source_location(context_, call.getExprLoc()),
			    .evidence = make_evidence(std::move(classification.rule_id),
			        std::move(classification.reason), call.getSourceRange()),
			};
		}

		auto classification = inspect_collect::classify_free_function_call(
		    callee->getReturnType()->isVoidType() || result_is_ignored(context_, call));

		return DependencyPort{
		    .kind = classification.kind,
		    .name = stable_port_name(llvm::isa<clang::CXXOperatorCallExpr>(&call)
		            ? operator_port_name(callee->getQualifiedNameAsString())
		            : replace_colons(callee->getQualifiedNameAsString()),
		        *callee),
		    .original_callee = callee->getQualifiedNameAsString(),
		    .return_type = type_string(context_, callee->getReturnType()),
		    .argument_types = call_argument_types(call),
		    .location = source_location(context_, call.getExprLoc()),
		    .evidence = make_evidence(std::move(classification.rule_id),
		        std::move(classification.reason), call.getSourceRange()),
		};
	}

	[[nodiscard]] std::vector<PathEvent> collect_events(clang::Stmt const& statement)
	{
		std::vector<PathEvent> events;
		CallEventVisitor visitor(*this, events);
		visitor.TraverseStmt(const_cast<clang::Stmt*>(&statement));
		deduplicate_events(events);
		return events;
	}

   private:
	struct SwitchSegment
	{
		std::string label;
		std::vector<clang::Stmt const*> statements;
	};

	struct StructuredBindingSource
	{
		std::string source_dependency;
		std::optional<std::string> local_type;
	};

	void initialize_rule_coverage()
	{
		auto add = [this](std::string rule_id, ConstructHandling handling, std::string note)
		{
			plan_.rule_coverage.push_back({
			    .rule_id = std::move(rule_id),
			    .handling = handling,
			    .note = std::move(note),
			    .observed = false,
			});
		};

		add("LR-001", ConstructHandling::kSupported, "implicit data member read");
		add("LR-002", ConstructHandling::kSupported, "explicit this data member read");
		add("LR-003", ConstructHandling::kSupported, "data member mutation");
		add("LR-004", ConstructHandling::kSupported, "return statement");
		add("LR-005", ConstructHandling::kSupported, "if statement");
		add("LR-006", ConstructHandling::kSupported, "built-in expression");
		add("LR-007", ConstructHandling::kSupported, "same-class nonvirtual helper candidate");
		add("LR-008", ConstructHandling::kBoundary, "static member calls are boundary candidates");
		add("LR-009", ConstructHandling::kBoundary, "free function calls are boundary candidates");
		add("LR-010", ConstructHandling::kModeled, "global state is modeled as env");
		add("LR-011", ConstructHandling::kModeled, "base state is modeled explicitly");
		add("LR-012", ConstructHandling::kModeled, "virtual calls require dispatch table");
		add("LR-013", ConstructHandling::kBoundary, "overloaded operators are resolved boundaries");
		add("LR-014", ConstructHandling::kSupported, "local variable declaration");
		add("LR-015", ConstructHandling::kConservative, "loops are conservative paths in Phase A");
		add("LR-016", ConstructHandling::kConservative, "range-for is conservative in Phase A");
		add("LR-017", ConstructHandling::kSupported, "lambda without this capture");
		add("LR-018", ConstructHandling::kModeled, "lambda with this capture maps to self");
		add("LR-019", ConstructHandling::kSupported, "noexcept is reported from method metadata");
		add("LR-020", ConstructHandling::kModeled, "raw this escape requires object_ref");
		add("LR-021", ConstructHandling::kModeled, "return this requires object_ref");
		add("LR-022", ConstructHandling::kModeled, "return *this requires object_ref/self view");
		add("LR-023", ConstructHandling::kModeled, "dynamic_cast requires type_tag");
		add("LR-024", ConstructHandling::kModeled, "typeid requires type_tag");
		add("LR-025", ConstructHandling::kBoundary, "reinterpret_cast this requires byte boundary");
		add("LR-026", ConstructHandling::kModeled, "delete this requires lifetime state");
		add("LR-027", ConstructHandling::kModeled,
		    "explicit destructor call requires lifetime state");
		add("LR-028", ConstructHandling::kModeled, "placement new on this requires lifetime state");
		add("LR-029", ConstructHandling::kModeled, "field address requires addressable cell");
		add("LR-030", ConstructHandling::kNotYetImplemented,
		    "member function pointer is reported only");
		add("LR-031", ConstructHandling::kNotYetImplemented,
		    "constructor target is outside Phase A");
		add("LR-032", ConstructHandling::kNotYetImplemented,
		    "destructor target is outside Phase A");
		add("LR-033", ConstructHandling::kSupported,
		    "instantiated function template specializations are inspectable");
		add("LR-034", ConstructHandling::kConservative, "macro source mapping is conservative");
		add("LR-035", ConstructHandling::kSupported,
		    "throw expression is preserved as exception intent");
		add("LR-036", ConstructHandling::kConservative, "try/catch is conservative in Phase A");
		add("LR-037", ConstructHandling::kSupported,
		    "structured binding field expansion when bindings resolve to record fields");
		add("LR-038", ConstructHandling::kNotYetImplemented, "coroutines are future work");
		add("LR-039", ConstructHandling::kSupported,
		    "unevaluated contexts are tracked without effects");
		add("LR-040", ConstructHandling::kNotYetImplemented,
		    "default member initializer is future work");
		add("LR-041", ConstructHandling::kSupported,
		    "conditional operator is expression control flow");
		add("LR-042", ConstructHandling::kSupported,
		    "default arguments are supplied by Clang Sema");
		add("LR-043", ConstructHandling::kSupported,
		    "auto type is recorded after semantic resolution");
		add("LR-044", ConstructHandling::kSupported,
		    "casts and std::move/std::forward are resolved expressions");
		add("LR-045", ConstructHandling::kBoundary,
		    "const_cast requires mutability-aware boundary handling");
		add("LR-046", ConstructHandling::kSupported, "break/continue statements are tracked");
		add("LR-047", ConstructHandling::kSupported,
		    "cv/ref member qualifiers are part of target metadata");
		add("LR-048", ConstructHandling::kModeled,
		    "access control is reported without rewriting product visibility");
		add("LR-049", ConstructHandling::kConservative,
		    "constructor calls in method bodies require construction-aware lowering");
	}

	void mark_rule_observed(std::string const& rule_id)
	{
		if (!rule_id.starts_with("LR-"))
		{
			return;
		}

		auto iterator = std::ranges::find_if(plan_.rule_coverage,
		    [&rule_id](RuleCoverage const& coverage)
		    {
			    return coverage.rule_id == rule_id;
		    });
		if (iterator != plan_.rule_coverage.end())
		{
			iterator->observed = true;
		}
	}

	void finalize_deterministic_order()
	{
		std::ranges::sort(plan_.receiver_state,
		    [](ReceiverField const& lhs, ReceiverField const& rhs)
		    {
			    return std::tie(lhs.name, lhs.type, lhs.access_specifier) <
			        std::tie(rhs.name, rhs.type, rhs.access_specifier);
		    });

		std::ranges::sort(plan_.dependency_ports,
		    [](DependencyPort const& lhs, DependencyPort const& rhs)
		    {
			    return std::tie(lhs.kind, lhs.name, lhs.original_callee, lhs.return_type,
			               lhs.argument_types) < std::tie(rhs.kind, rhs.name, rhs.original_callee,
			                                         rhs.return_type, rhs.argument_types);
		    });

		for (auto& shape : plan_.shape_candidates)
		{
			std::ranges::sort(shape.observed_members);
		}
		std::ranges::sort(plan_.shape_candidates,
		    [](ShapeCandidate const& lhs, ShapeCandidate const& rhs)
		    {
			    return std::tie(lhs.name, lhs.source_dependency, lhs.observed_members) <
			        std::tie(rhs.name, rhs.source_dependency, rhs.observed_members);
		    });

		std::ranges::sort(plan_.object_ref_requirements,
		    [](ObjectRefRequirement const& lhs, ObjectRefRequirement const& rhs)
		    {
			    return std::tie(lhs.reason, lhs.expression) < std::tie(rhs.reason, rhs.expression);
		    });

		std::ranges::sort(plan_.semantic_features,
		    [](SemanticFeature const& lhs, SemanticFeature const& rhs)
		    {
			    return std::tie(lhs.name, lhs.handling, lhs.detail) <
			        std::tie(rhs.name, rhs.handling, rhs.detail);
		    });

		std::ranges::sort(plan_.unsupported_or_modeled_constructs,
		    [](UnsupportedOrModeledConstruct const& lhs, UnsupportedOrModeledConstruct const& rhs)
		    {
			    return std::tie(lhs.construct, lhs.handling, lhs.reason) <
			        std::tie(rhs.construct, rhs.handling, rhs.reason);
		    });

		std::ranges::sort(plan_.envelope_requirements,
		    [](EnvelopeRequirement const& lhs, EnvelopeRequirement const& rhs)
		    {
			    return std::tie(lhs.kind, lhs.source, lhs.reason) <
			        std::tie(rhs.kind, rhs.source, rhs.reason);
		    });
	}

	void record_method_qualifiers()
	{
		if (method_.isConst() || method_.isVolatile() ||
		    method_.getRefQualifier() != clang::RQ_None)
		{
			auto details = std::string{"target method qualifiers:"};
			if (method_.isConst())
			{
				details += " const";
			}
			if (method_.isVolatile())
			{
				details += " volatile";
			}
			switch (method_.getRefQualifier())
			{
				case clang::RQ_None:
					break;
				case clang::RQ_LValue:
					details += " &";
					break;
				case clang::RQ_RValue:
					details += " &&";
					break;
			}
			auto evidence = make_evidence(
			    "LR-047", "target method cv/ref qualifiers are recorded", method_.getSourceRange());
			add_semantic_feature(
			    "method_qualifier", ConstructHandling::kSupported, details, evidence);
		}

		auto const* proto = method_.getType()->getAs<clang::FunctionProtoType>();
		if (proto != nullptr && proto->isNothrow())
		{
			auto evidence = make_evidence("LR-019",
			    "target method has noexcept-compatible exception specification",
			    method_.getSourceRange());
			add_semantic_feature("noexcept", ConstructHandling::kSupported,
			    "target method is noexcept-compatible", evidence);
		}

		for (auto const* parameter : method_.parameters())
		{
			if (!parameter->hasDefaultArg())
			{
				continue;
			}

			auto evidence = make_evidence("LR-042",
			    "target method parameter has a default argument", parameter->getSourceRange());
			add_semantic_feature("default_argument", ConstructHandling::kSupported,
			    "target parameter default argument is visible in inspect metadata", evidence);
		}

		if (llvm::isa<clang::ClassTemplateSpecializationDecl>(method_.getParent()))
		{
			auto evidence =
			    make_evidence("LR-033", "target belongs to a class template specialization",
			        method_.getSourceRange(), true, "conservative");
			add_semantic_feature("template_specialization", ConstructHandling::kConservative,
			    "class template specialization is inspected after Clang instantiation", evidence);
			add_construct("class template specialization", ConstructHandling::kConservative,
			    "template-dependent source spelling may differ from instantiated AST",
			    {"inspect instantiated semantics only", "future template-source mapping"},
			    method_.getLocation(), evidence);
		}

		if (method_.isFunctionTemplateSpecialization())
		{
			auto evidence = make_evidence("LR-033",
			    "target is an instantiated function template specialization",
			    method_.getSourceRange());
			add_semantic_feature("template_specialization", ConstructHandling::kSupported,
			    "function template specialization is inspected from its instantiated body",
			    evidence);
		}
	}

	[[nodiscard]] PlanEvidence make_evidence(std::string rule_id, std::string reason,
	    clang::SourceRange range, bool conservative = false,
	    std::string certainty = "certain") const
	{
		return {
		    .rule_id = std::move(rule_id),
		    .reason = std::move(reason),
		    .certainty = std::move(certainty),
		    .conservative = conservative,
		    .source_range = source_range(context_, range),
		};
	}

	void mark_conservative(std::string code, std::string message, clang::SourceLocation location)
	{
		if (plan_.result == "extracted")
		{
			plan_.result = "extracted-with-conservative-notes";
		}
		if (plan_.confidence == "high")
		{
			plan_.confidence = "medium";
		}

		plan_.diagnostics.add(DiagnosticSeverity::kWarning, std::move(code), std::move(message),
		    source_location(context_, location));
	}

	bool record_loop(clang::Stmt const& statement, clang::SourceLocation location,
	    std::string_view label, std::string rule_id)
	{
		plan_.control_flow_summary.has_loop = true;
		plan_.control_flow_summary.conservative = true;
		plan_.control_flow_summary.conservative_reasons.emplace_back(
		    std::string(label) + " is represented conservatively in Phase A");
		auto evidence = make_evidence(std::move(rule_id),
		    std::string(label) + " is a conservative path region in Phase A",
		    statement.getSourceRange(), true, "conservative");
		add_mmir(MmirNodeKind::kLoop, std::string(label), location, statement.getSourceRange(),
		    evidence);
		add_semantic_feature("loop_control_flow", ConstructHandling::kConservative,
		    "loop body dependencies are summarized on a conservative path", evidence);
		return true;
	}

	bool record_supported_cast(
	    clang::ExplicitCastExpr const& cast, clang::SourceLocation location, std::string label)
	{
		auto evidence = make_evidence(
		    "LR-044", "cast expression is resolved by Clang Sema", cast.getSourceRange());
		add_mmir(MmirNodeKind::kCast, std::move(label), location, cast.getSourceRange(), evidence);
		add_semantic_feature("cast_expression", ConstructHandling::kSupported,
		    "cast expression is represented from resolved AST type information", evidence);
		return true;
	}

	void add_semantic_feature(
	    std::string name, ConstructHandling handling, std::string detail, PlanEvidence evidence)
	{
		mark_rule_observed(evidence.rule_id);
		auto duplicate = std::ranges::any_of(plan_.semantic_features,
		    [&name, handling](SemanticFeature const& feature)
		    {
			    return feature.name == name && feature.handling == handling;
		    });
		if (!duplicate)
		{
			plan_.semantic_features.push_back({
			    .name = std::move(name),
			    .handling = handling,
			    .detail = std::move(detail),
			    .evidence = std::move(evidence),
			});
		}
	}

	void add_construct(std::string construct, ConstructHandling handling, std::string reason,
	    std::vector<std::string> fallbacks, clang::SourceLocation location, PlanEvidence evidence)
	{
		mark_rule_observed(evidence.rule_id);
		if (handling == ConstructHandling::kConservative ||
		    handling == ConstructHandling::kNotYetImplemented)
		{
			if (plan_.result == "extracted")
			{
				plan_.result = "extracted-with-conservative-notes";
			}
			if (handling == ConstructHandling::kNotYetImplemented)
			{
				plan_.confidence = "low";
			}
			else if (plan_.confidence == "high")
			{
				plan_.confidence = "medium";
			}
		}

		auto duplicate = std::ranges::any_of(plan_.unsupported_or_modeled_constructs,
		    [&construct, handling](UnsupportedOrModeledConstruct const& existing)
		    {
			    return existing.construct == construct && existing.handling == handling;
		    });
		if (!duplicate)
		{
			if (handling == ConstructHandling::kConservative ||
			    handling == ConstructHandling::kNotYetImplemented)
			{
				auto const* code = handling == ConstructHandling::kNotYetImplemented
				    ? "AZTECA_UNSUPPORTED_CONSTRUCT"
				    : "AZTECA_CONSERVATIVE_CONSTRUCT";
				plan_.diagnostics.add(DiagnosticSeverity::kWarning, code, construct + ": " + reason,
				    source_location(context_, location));
			}
			plan_.unsupported_or_modeled_constructs.push_back({
			    .construct = std::move(construct),
			    .handling = handling,
			    .reason = std::move(reason),
			    .fallbacks = std::move(fallbacks),
			    .location = source_location(context_, location),
			    .evidence = std::move(evidence),
			});
		}
	}

	void add_envelope_requirement(
	    EnvelopeRequirementKind kind, std::string reason, std::string source, PlanEvidence evidence)
	{
		mark_rule_observed(evidence.rule_id);
		auto duplicate = std::ranges::any_of(plan_.envelope_requirements,
		    [kind, &source](EnvelopeRequirement const& requirement)
		    {
			    return requirement.kind == kind && requirement.source == source;
		    });
		if (!duplicate)
		{
			plan_.envelope_requirements.push_back({
			    .kind = kind,
			    .reason = std::move(reason),
			    .source = std::move(source),
			    .evidence = std::move(evidence),
			});
		}
	}

	void record_macro_if_needed(clang::SourceRange range)
	{
		if (range.isInvalid() || (!range.getBegin().isMacroID() && !range.getEnd().isMacroID()))
		{
			return;
		}

		auto rendered_range = source_range(context_, range).to_string();
		if (!macro_ranges_.insert(rendered_range).second)
		{
			return;
		}

		auto evidence = make_evidence("LR-034",
		    "macro-expanded source range is tracked conservatively", range, true, "conservative");
		add_semantic_feature("macro_expansion", ConstructHandling::kConservative,
		    "source spelling may differ from AST semantics", evidence);
		add_construct("macro expansion", ConstructHandling::kConservative,
		    "macro expansion requires source-map-aware regeneration",
		    {"use AST semantics for inspect", "treat complex macro spelling as conservative"},
		    range.getBegin(), evidence);
		add_envelope_requirement(EnvelopeRequirementKind::kMacroSourceMap,
		    "macro source mapping is required for faithful diagnostics", rendered_range, evidence);
	}

	[[nodiscard]] std::vector<std::string> call_argument_types(clang::CallExpr const& call) const
	{
		std::vector<std::string> argument_types;
		argument_types.reserve(call.getNumArgs());

		for (auto const* argument : call.arguments())
		{
			argument_types.push_back(type_string(context_, argument->getType()));
		}

		return argument_types;
	}

	[[nodiscard]] static bool contains_this_argument(clang::CallExpr const& call)
	{
		return std::ranges::any_of(call.arguments(),
		    [](clang::Expr const* argument)
		    {
			    return contains_raw_this_expr(argument);
		    });
	}

	[[nodiscard]] bool is_shape_call(clang::CXXMemberCallExpr const& call) const
	{
		auto const* callee_expression = call.getCallee();
		if (callee_expression == nullptr)
		{
			return false;
		}

		auto const* member_expression =
		    llvm::dyn_cast<clang::MemberExpr>(callee_expression->IgnoreParenImpCasts());
		if (member_expression == nullptr || member_expression->getBase() == nullptr)
		{
			return false;
		}

		auto const* reference =
		    llvm::dyn_cast<clang::DeclRefExpr>(member_expression->getBase()->IgnoreParenImpCasts());
		if (reference == nullptr)
		{
			return false;
		}

		auto const* variable = llvm::dyn_cast<clang::VarDecl>(reference->getDecl());
		return variable != nullptr &&
		    local_dependency_sources_.contains(variable->getCanonicalDecl());
	}

	[[nodiscard]] static clang::CXXMethodDecl const* member_function_pointer_target(
	    clang::Expr const* expression)
	{
		if (expression == nullptr)
		{
			return nullptr;
		}

		if (auto const* member = llvm::dyn_cast<clang::MemberExpr>(expression))
		{
			return llvm::dyn_cast<clang::CXXMethodDecl>(member->getMemberDecl());
		}

		if (auto const* reference = llvm::dyn_cast<clang::DeclRefExpr>(expression))
		{
			return llvm::dyn_cast<clang::CXXMethodDecl>(reference->getDecl());
		}

		return nullptr;
	}

	[[nodiscard]] FieldAccess classify_field_access(clang::MemberExpr const& member)
	{
		auto parents = context_.getParents(member);
		if (parents.empty())
		{
			return FieldAccess::kRead;
		}

		auto const& parent = *parents.begin();
		if (auto const* unary_operator = parent.get<clang::UnaryOperator>())
		{
			if (unary_operator->getOpcode() == clang::UO_AddrOf &&
			    expression_contains(unary_operator->getSubExpr(), &member))
			{
				return FieldAccess::kAddress;
			}

			if (unary_operator->isIncrementDecrementOp() &&
			    expression_contains(unary_operator->getSubExpr(), &member))
			{
				return FieldAccess::kReadWrite;
			}
		}

		if (auto const* binary_operator = parent.get<clang::BinaryOperator>())
		{
			if (binary_operator->isAssignmentOp() &&
			    expression_contains(binary_operator->getLHS(), &member))
			{
				if (llvm::isa<clang::CompoundAssignOperator>(binary_operator))
				{
					return FieldAccess::kReadWrite;
				}
				return FieldAccess::kWrite;
			}
		}

		return FieldAccess::kRead;
	}

	[[nodiscard]] bool is_dependency_object_base(clang::MemberExpr const& member)
	{
		auto const* current = static_cast<clang::Stmt const*>(&member);
		for (auto depth = 0; depth < 8; ++depth)
		{
			auto parents = context_.getParents(*current);
			if (parents.empty())
			{
				return false;
			}

			auto const& parent = *parents.begin();
			if (auto const* outer_member = parent.get<clang::MemberExpr>())
			{
				if (llvm::isa<clang::CXXMethodDecl>(outer_member->getMemberDecl()) &&
				    expression_contains(outer_member->getBase(), &member))
				{
					return true;
				}
				current = outer_member;
				continue;
			}

			auto const* expression = parent.get<clang::Expr>();
			if (expression == nullptr)
			{
				return false;
			}
			current = expression;
		}

		return false;
	}

	void add_receiver_field(
	    clang::FieldDecl const& field, FieldAccess access, PlanEvidence evidence)
	{
		auto const* canonical = field.getCanonicalDecl();
		auto iterator = receiver_field_indices_.find(canonical);
		if (iterator == receiver_field_indices_.end())
		{
			receiver_field_indices_[canonical] = plan_.receiver_state.size();
			plan_.receiver_state.push_back({
			    .name = field.getNameAsString(),
			    .type = type_string(context_, field.getType()),
			    .access = access,
			    .is_mutable = field.isMutable(),
			    .access_specifier = access_to_string(field.getAccess()),
			    .location = source_location(context_, field.getLocation()),
			    .evidence = std::move(evidence),
			});
			return;
		}

		auto& existing = plan_.receiver_state[iterator->second];
		if (existing.access != access)
		{
			existing.access = FieldAccess::kReadWrite;
			existing.evidence.reason = "receiver field read/write";
			existing.evidence.rule_id = "LR-003";
		}
	}

	void remove_dependency_object_fields()
	{
		if (dependency_fields_.empty())
		{
			return;
		}

		std::vector<ReceiverField> filtered;
		filtered.reserve(plan_.receiver_state.size());

		for (auto const& field : plan_.receiver_state)
		{
			auto should_remove = false;
			for (auto const* dependency_field : dependency_fields_)
			{
				if (field.name == dependency_field->getNameAsString() &&
				    field.access == FieldAccess::kRead)
				{
					should_remove = true;
					break;
				}
			}

			if (!should_remove)
			{
				filtered.push_back(field);
			}
		}

		plan_.receiver_state = std::move(filtered);

		std::vector<EnvelopeRequirement> envelope_filtered;
		envelope_filtered.reserve(plan_.envelope_requirements.size());
		for (auto const& requirement : plan_.envelope_requirements)
		{
			auto should_remove = false;
			for (auto const* dependency_field : dependency_fields_)
			{
				if (requirement.kind == EnvelopeRequirementKind::kSelfState &&
				    requirement.source == dependency_field->getQualifiedNameAsString())
				{
					should_remove = true;
					break;
				}
			}

			if (!should_remove)
			{
				envelope_filtered.push_back(requirement);
			}
		}
		plan_.envelope_requirements = std::move(envelope_filtered);

		if (plan_.receiver_state.empty())
		{
			std::erase_if(plan_.semantic_features,
			    [](SemanticFeature const& feature)
			    {
				    return feature.name == "receiver_state" || feature.name == "access_control";
			    });
			return;
		}

		auto const kHasNonPublicReceiverState = std::ranges::any_of(plan_.receiver_state,
		    [](ReceiverField const& field)
		    {
			    return field.access_specifier == "private" || field.access_specifier == "protected";
		    });
		if (!kHasNonPublicReceiverState)
		{
			std::erase_if(plan_.semantic_features,
			    [](SemanticFeature const& feature)
			    {
				    return feature.name == "access_control";
			    });
		}
	}

	void record_dependency_local(clang::VarDecl const& variable)
	{
		auto const* initializer = variable.getInit();
		if (initializer == nullptr)
		{
			return;
		}

		auto const* ignored = initializer->IgnoreParenImpCasts();
		std::optional<DependencyPort> port;
		if (auto const* member_call = llvm::dyn_cast<clang::CXXMemberCallExpr>(ignored))
		{
			port = dependency_port_for_call(*member_call);
		}
		else if (auto const* call = llvm::dyn_cast<clang::CallExpr>(ignored))
		{
			port = dependency_port_for_call(*call);
		}

		if (port.has_value() &&
		    (port->kind == DependencyKind::kQuery || port->kind == DependencyKind::kOperation))
		{
			local_dependency_sources_[variable.getCanonicalDecl()] = port->name;
		}
	}

	void add_shape_observation(clang::MemberExpr const& member)
	{
		auto const* base = member.getBase();
		if (base == nullptr)
		{
			return;
		}

		auto const* reference = llvm::dyn_cast<clang::DeclRefExpr>(base->IgnoreParenImpCasts());
		if (reference == nullptr)
		{
			return;
		}

		auto const* variable = llvm::dyn_cast<clang::VarDecl>(reference->getDecl());
		if (variable == nullptr)
		{
			return;
		}

		auto dependency = local_dependency_sources_.find(variable->getCanonicalDecl());
		if (dependency == local_dependency_sources_.end())
		{
			return;
		}

		auto type = local_types_.find(variable->getCanonicalDecl());
		auto local_type =
		    type == local_types_.end() ? std::optional<std::string>{} : std::optional{type->second};
		inspect_collect::record_shape_observation(plan_, dependency->second, local_type,
		    member.getMemberDecl()->getNameAsString(),
		    make_evidence("SHAPE-DEPENDENCY-RETURN",
		        "member access on dependency return local suggests a shape candidate",
		        member.getSourceRange()));
	}

	[[nodiscard]] bool record_structured_binding_shape(clang::DecompositionDecl const& declaration)
	{
		auto source = structured_binding_source(declaration);
		if (!source.has_value())
		{
			return false;
		}

		auto observed_members = std::vector<std::string>{};
		for (auto const* binding : declaration.bindings())
		{
			auto const* binding_expression = binding->getBinding();
			if (binding_expression == nullptr)
			{
				return false;
			}

			auto const* member =
			    llvm::dyn_cast<clang::MemberExpr>(binding_expression->IgnoreParenImpCasts());
			if (member == nullptr)
			{
				return false;
			}

			auto const* field = llvm::dyn_cast<clang::FieldDecl>(member->getMemberDecl());
			if (field == nullptr)
			{
				return false;
			}
			observed_members.push_back(field->getNameAsString());
		}

		if (observed_members.empty())
		{
			return false;
		}

		auto evidence = make_evidence("LR-037",
		    "structured binding exposes fields from a visible record definition",
		    declaration.getSourceRange());
		for (auto const& member : observed_members)
		{
			inspect_collect::record_shape_observation(
			    plan_, source->source_dependency, source->local_type, member, evidence);
		}
		return true;
	}

	[[nodiscard]] std::optional<StructuredBindingSource> structured_binding_source(
	    clang::DecompositionDecl const& declaration)
	{
		auto const* initializer = unwrap_decomposition_initializer(declaration.getInit());
		if (initializer == nullptr)
		{
			return std::nullopt;
		}

		if (auto const* member = llvm::dyn_cast<clang::MemberExpr>(initializer))
		{
			auto const* field = llvm::dyn_cast<clang::FieldDecl>(member->getMemberDecl());
			if (field != nullptr)
			{
				return StructuredBindingSource{
				    .source_dependency = field->getQualifiedNameAsString(),
				    .local_type = type_string(context_, member->getType()),
				};
			}
		}

		if (auto const* reference = llvm::dyn_cast<clang::DeclRefExpr>(initializer))
		{
			auto const* variable = llvm::dyn_cast<clang::VarDecl>(reference->getDecl());
			if (variable == nullptr)
			{
				return std::nullopt;
			}

			auto dependency = local_dependency_sources_.find(variable->getCanonicalDecl());
			if (dependency != local_dependency_sources_.end())
			{
				auto type = local_types_.find(variable->getCanonicalDecl());
				return StructuredBindingSource{
				    .source_dependency = dependency->second,
				    .local_type = type == local_types_.end() ? std::optional<std::string>{}
				                                             : std::optional{type->second},
				};
			}

			return StructuredBindingSource{
			    .source_dependency = "local:" + variable->getNameAsString(),
			    .local_type = type_string(context_, variable->getType()),
			};
		}

		if (auto const* member_call = llvm::dyn_cast<clang::CXXMemberCallExpr>(initializer))
		{
			auto port = dependency_port_for_call(*member_call);
			if (port.has_value())
			{
				auto source = port->name;
				add_dependency_port(std::move(*port));
				return StructuredBindingSource{
				    .source_dependency = std::move(source),
				    .local_type = type_string(context_, member_call->getType()),
				};
			}
		}

		if (auto const* call = llvm::dyn_cast<clang::CallExpr>(initializer))
		{
			auto port = dependency_port_for_call(*call);
			if (port.has_value())
			{
				auto source = port->name;
				add_dependency_port(std::move(*port));
				return StructuredBindingSource{
				    .source_dependency = std::move(source),
				    .local_type = type_string(context_, call->getType()),
				};
			}
		}

		return std::nullopt;
	}

	[[nodiscard]] static clang::Expr const* unwrap_decomposition_initializer(
	    clang::Expr const* expression)
	{
		while (expression != nullptr)
		{
			auto const* ignored = expression->IgnoreParenImpCasts();
			if (auto const* construct = llvm::dyn_cast<clang::CXXConstructExpr>(ignored);
			    construct != nullptr && construct->getNumArgs() == 1U)
			{
				expression = construct->getArg(0);
				continue;
			}
			return ignored;
		}

		return nullptr;
	}

	void record_constructor_local_shape(clang::CXXConstructExpr const& expression,
	    clang::CXXConstructorDecl const& constructor, PlanEvidence const& evidence)
	{
		auto const* variable = local_constructor_target(expression);
		if (variable == nullptr)
		{
			return;
		}

		auto const* record = constructor.getParent();
		auto const* definition = record == nullptr ? nullptr : record->getDefinition();
		if (definition == nullptr)
		{
			return;
		}

		for (auto const* field : definition->fields())
		{
			if (field->getAccess() != clang::AS_public)
			{
				continue;
			}
			inspect_collect::record_shape_observation(plan_, "local:" + variable->getNameAsString(),
			    std::optional{type_string(context_, variable->getType())}, field->getNameAsString(),
			    evidence);
		}
	}

	[[nodiscard]] clang::VarDecl const* local_constructor_target(
	    clang::CXXConstructExpr const& expression) const
	{
		auto parents = context_.getParents(expression);
		while (!parents.empty())
		{
			auto const& parent = *parents.begin();
			if (auto const* variable = parent.get<clang::VarDecl>();
			    variable != nullptr && variable->isLocalVarDecl() && variable->getInit() != nullptr)
			{
				return variable;
			}

			auto const* parent_expression = parent.get<clang::Expr>();
			if (parent_expression == nullptr)
			{
				return nullptr;
			}
			parents = context_.getParents(*parent_expression);
		}

		return nullptr;
	}

	void add_dependency_port(DependencyPort port)
	{
		auto duplicate = std::ranges::any_of(plan_.dependency_ports,
		    [&port](DependencyPort const& existing)
		    {
			    return existing.kind == port.kind && existing.name == port.name &&
			        existing.original_callee == port.original_callee &&
			        existing.return_type == port.return_type &&
			        existing.argument_types == port.argument_types;
		    });

		if (!duplicate)
		{
			plan_.dependency_ports.push_back(std::move(port));
		}
	}

	void add_object_ref_requirement(std::string reason, std::string expression,
	    clang::SourceLocation location, clang::SourceRange range, std::string rule_id)
	{
		auto source = source_location(context_, location);
		auto duplicate = std::ranges::any_of(plan_.object_ref_requirements,
		    [&reason, &expression](ObjectRefRequirement const& existing)
		    {
			    return existing.reason == reason && existing.expression == expression;
		    });

		if (!duplicate)
		{
			auto evidence = make_evidence(
			    std::move(rule_id), "this identity is observable and requires object_ref", range);
			plan_.object_ref_requirements.push_back({
			    .reason = std::move(reason),
			    .expression = std::move(expression),
			    .location = std::move(source),
			    .evidence = evidence,
			});
			add_semantic_feature("object_identity", ConstructHandling::kModeled,
			    "this identity is represented by object_ref", evidence);
			add_envelope_requirement(EnvelopeRequirementKind::kObjectRef,
			    "this identity is unit-observable", plan_.object_ref_requirements.back().expression,
			    evidence);
			add_mmir(MmirNodeKind::kObjectRefRequirement, "object_ref", location, range,
			    plan_.object_ref_requirements.back().evidence, "object_ref");
		}
	}

	void add_unclassified_call(clang::CallExpr const& call)
	{
		auto const* callee = call.getDirectCallee();
		auto original_symbol =
		    callee == nullptr ? std::string{} : callee->getQualifiedNameAsString();
		if (original_symbol.empty())
		{
			original_symbol = "unknown_call";
		}

		auto evidence = make_evidence("CALL-BOUNDARY",
		    "call is represented as a conservative dependency boundary", call.getSourceRange(),
		    true, "conservative");
		add_mmir(MmirNodeKind::kBoundaryCall, original_symbol, call.getExprLoc(),
		    call.getSourceRange(), evidence, sanitize_identifier(original_symbol), original_symbol);
		add_construct("call boundary", ConstructHandling::kBoundary,
		    "callee could not be lowered directly during Phase A inspect",
		    {"dependency transcript boundary", "recursive extraction when a body is available"},
		    call.getExprLoc(), evidence);
		add_envelope_requirement(EnvelopeRequirementKind::kDependencyBoundary,
		    "unclassified call is preserved as an explicit boundary", original_symbol, evidence);
	}

	void add_mmir(MmirNodeKind kind, std::string label, clang::SourceLocation location,
	    clang::SourceRange range, PlanEvidence const& evidence, std::string semantic_id = {},
	    std::string original_symbol = {})
	{
		mark_rule_observed(evidence.rule_id);
		record_macro_if_needed(range);
		plan_.mmir.nodes.push_back({
		    .kind = kind,
		    .label = std::move(label),
		    .location = source_location(context_, location),
		    .source_range = source_range(context_, range),
		    .semantic_id = std::move(semantic_id),
		    .original_symbol = std::move(original_symbol),
		    .rule_id = evidence.rule_id,
		    .reason = evidence.reason,
		    .conservative = evidence.conservative,
		});
	}

	[[nodiscard]] std::vector<PathBurden> analyze_paths(clang::Stmt const& body)
	{
		std::vector<PathBurden> paths;
		auto states = std::vector<PathState>{{}};
		auto path_index = std::size_t{1};
		auto fallthrough_states = analyze_statement(body, std::move(states), paths, path_index);

		for (auto const& state : fallthrough_states)
		{
			paths.push_back(build_path_burden(
			    path_name("path_" + std::to_string(path_index), state), state.events,
			    make_evidence(state.conservative ? "PATH-CONSERVATIVE" : "PATH-DFS",
			        state.conservative ? "fallthrough path includes conservative control flow"
			                           : "fallthrough path accumulated by DFS",
			        body.getSourceRange(), state.conservative,
			        state.conservative ? "conservative" : "certain"),
			    state.loop_body_observations));
			++path_index;
		}

		return paths;
	}

	[[nodiscard]] std::vector<PathState> analyze_statement(clang::Stmt const& statement,
	    std::vector<PathState> states, std::vector<PathBurden>& paths, std::size_t& path_index)
	{
		if (auto const* compound = llvm::dyn_cast<clang::CompoundStmt>(&statement))
		{
			for (auto const* child : compound->body())
			{
				if (states.empty())
				{
					break;
				}
				states = analyze_statement(*child, std::move(states), paths, path_index);
			}
			return states;
		}

		if (auto const* return_statement = llvm::dyn_cast<clang::ReturnStmt>(&statement))
		{
			append_events(states, collect_events(statement));
			for (auto const& state : states)
			{
				paths.push_back(
				    build_path_burden(path_name(*return_statement, path_index, state), state.events,
				        make_evidence(state.conservative ? "PATH-CONSERVATIVE" : "PATH-DFS",
				            state.conservative ? "return path includes conservative control flow"
				                               : "return path accumulated by DFS",
				            return_statement->getSourceRange(), state.conservative,
				            state.conservative ? "conservative" : "certain"),
				        state.loop_body_observations));
				++path_index;
			}
			return {};
		}

		if (llvm::isa<clang::CXXThrowExpr>(&statement))
		{
			append_events(states, collect_events(statement));
			return {};
		}

		if (auto const* if_statement = llvm::dyn_cast<clang::IfStmt>(&statement))
		{
			if (if_statement->getInit() != nullptr)
			{
				append_events(states, collect_events(*if_statement->getInit()));
			}
			if (if_statement->getCond() != nullptr)
			{
				append_events(states, collect_events(*if_statement->getCond()));
			}

			auto then_states = if_statement->getThen() == nullptr
			    ? states
			    : analyze_statement(*if_statement->getThen(), states, paths, path_index);
			auto else_states = if_statement->getElse() == nullptr
			    ? std::move(states)
			    : analyze_statement(*if_statement->getElse(), std::move(states), paths, path_index);
			then_states.insert(then_states.end(), else_states.begin(), else_states.end());
			return then_states;
		}

		if (auto const* for_statement = llvm::dyn_cast<clang::ForStmt>(&statement))
		{
			return analyze_loop(*for_statement, for_statement->getBody(), true, std::move(states));
		}

		if (auto const* while_statement = llvm::dyn_cast<clang::WhileStmt>(&statement))
		{
			return analyze_loop(
			    *while_statement, while_statement->getBody(), true, std::move(states));
		}

		if (auto const* do_statement = llvm::dyn_cast<clang::DoStmt>(&statement))
		{
			return analyze_loop(*do_statement, do_statement->getBody(), false, std::move(states));
		}

		if (auto const* range_statement = llvm::dyn_cast<clang::CXXForRangeStmt>(&statement))
		{
			return analyze_loop(
			    *range_statement, range_statement->getBody(), true, std::move(states));
		}

		if (auto const* switch_statement = llvm::dyn_cast<clang::SwitchStmt>(&statement))
		{
			return analyze_switch(*switch_statement, std::move(states), paths, path_index);
		}

		if (auto const* try_statement = llvm::dyn_cast<clang::CXXTryStmt>(&statement))
		{
			return analyze_try(*try_statement, states, paths, path_index);
		}

		if (is_conservative_control(statement))
		{
			append_events(states, collect_events(statement));
			for (auto& state : states)
			{
				state.conservative = true;
			}
			mark_conservative("AZTECA_PATH_CONSERVATIVE",
			    "loop, switch, or try statement is represented as a single conservative path",
			    statement.getBeginLoc());
			return states;
		}

		append_events(states, collect_events(statement));
		return states;
	}

	[[nodiscard]] std::vector<PathState> analyze_loop(clang::ForStmt const& statement,
	    clang::Stmt const* body, bool has_zero_iteration_path, std::vector<PathState> states)
	{
		std::vector<PathEvent> before_events;
		std::vector<PathEvent> after_one_iteration_events;
		if (statement.getInit() != nullptr)
		{
			append_events(before_events, collect_events(*statement.getInit()));
		}
		if (statement.getCond() != nullptr)
		{
			append_events(before_events, collect_events(*statement.getCond()));
		}
		if (statement.getInc() != nullptr)
		{
			append_events(after_one_iteration_events, collect_events(*statement.getInc()));
		}
		return analyze_loop_body(statement, body, before_events, after_one_iteration_events,
		    has_zero_iteration_path, std::move(states));
	}

	[[nodiscard]] std::vector<PathState> analyze_loop(clang::WhileStmt const& statement,
	    clang::Stmt const* body, bool has_zero_iteration_path, std::vector<PathState> states)
	{
		std::vector<PathEvent> before_events;
		if (statement.getCond() != nullptr)
		{
			append_events(before_events, collect_events(*statement.getCond()));
		}
		return analyze_loop_body(
		    statement, body, before_events, {}, has_zero_iteration_path, std::move(states));
	}

	[[nodiscard]] std::vector<PathState> analyze_loop(clang::DoStmt const& statement,
	    clang::Stmt const* body, bool has_zero_iteration_path, std::vector<PathState> states)
	{
		std::vector<PathEvent> after_one_iteration_events;
		if (statement.getCond() != nullptr)
		{
			append_events(after_one_iteration_events, collect_events(*statement.getCond()));
		}
		return analyze_loop_body(statement, body, {}, after_one_iteration_events,
		    has_zero_iteration_path, std::move(states));
	}

	[[nodiscard]] std::vector<PathState> analyze_loop(clang::CXXForRangeStmt const& statement,
	    clang::Stmt const* body, bool has_zero_iteration_path, std::vector<PathState> states)
	{
		std::vector<PathEvent> before_events;
		if (statement.getRangeInit() != nullptr)
		{
			append_events(before_events, collect_events(*statement.getRangeInit()));
		}
		return analyze_loop_body(
		    statement, body, before_events, {}, has_zero_iteration_path, std::move(states));
	}

	[[nodiscard]] std::vector<PathState> analyze_loop_body(clang::Stmt const& statement,
	    clang::Stmt const* body, std::vector<PathEvent> const& before_events,
	    std::vector<PathEvent> const& after_one_iteration_events, bool has_zero_iteration_path,
	    std::vector<PathState> states)
	{
		append_events(states, before_events);
		auto body_events = body == nullptr ? std::vector<PathEvent>{} : collect_events(*body);
		auto loop_body_observations = event_names(body_events);
		std::vector<PathState> result;

		if (has_zero_iteration_path)
		{
			auto zero_states = states;
			append_path_label(zero_states, "loop_zero_iterations");
			mark_path_conservative(zero_states);
			result.insert(result.end(), zero_states.begin(), zero_states.end());
		}

		auto one_or_more_states = std::move(states);
		append_path_label(one_or_more_states, "loop_one_or_more_iterations");
		append_events(one_or_more_states, body_events);
		append_events(one_or_more_states, after_one_iteration_events);
		append_loop_body_observations(one_or_more_states, loop_body_observations);
		mark_path_conservative(one_or_more_states);
		result.insert(result.end(), one_or_more_states.begin(), one_or_more_states.end());

		mark_conservative("AZTECA_PATH_CONSERVATIVE",
		    "loop statement is approximated as zero/one-or-more execution paths",
		    statement.getBeginLoc());
		return result;
	}

	[[nodiscard]] std::vector<PathState> analyze_switch(clang::SwitchStmt const& statement,
	    std::vector<PathState> states, std::vector<PathBurden>& paths, std::size_t& path_index)
	{
		if (statement.getCond() != nullptr)
		{
			append_events(states, collect_events(*statement.getCond()));
		}

		auto segments = switch_segments(statement);
		if (segments.empty())
		{
			append_events(states, collect_events(statement));
			mark_path_conservative(states);
			mark_conservative("AZTECA_PATH_CONSERVATIVE",
			    "switch statement could not be segmented and remains conservative",
			    statement.getBeginLoc());
			return states;
		}

		std::vector<PathState> result;
		auto has_default = false;
		for (auto segment_index = std::size_t{0}; segment_index < segments.size(); ++segment_index)
		{
			has_default = has_default || segments[segment_index].label == "switch_default";
			auto active_states = states;
			append_path_label(active_states, segments[segment_index].label);

			for (auto fallthrough_index = segment_index; fallthrough_index < segments.size();
			     ++fallthrough_index)
			{
				auto stopped_by_break = false;
				for (auto const* child : segments[fallthrough_index].statements)
				{
					if (llvm::isa<clang::BreakStmt>(child))
					{
						stopped_by_break = true;
						break;
					}
					active_states =
					    analyze_statement(*child, std::move(active_states), paths, path_index);
					if (active_states.empty())
					{
						break;
					}
				}

				if (stopped_by_break)
				{
					result.insert(result.end(), active_states.begin(), active_states.end());
					break;
				}

				if (active_states.empty())
				{
					break;
				}

				if (fallthrough_index + 1U == segments.size())
				{
					result.insert(result.end(), active_states.begin(), active_states.end());
				}
			}
		}

		if (!has_default)
		{
			auto no_match_states = states;
			append_path_label(no_match_states, "switch_no_match");
			result.insert(result.end(), no_match_states.begin(), no_match_states.end());
		}

		return result;
	}

	[[nodiscard]] std::vector<PathState> analyze_try(clang::CXXTryStmt const& statement,
	    std::vector<PathState> const& states, std::vector<PathBurden>& paths,
	    std::size_t& path_index)
	{
		std::vector<PathState> result;
		auto try_states = states;
		append_path_label(try_states, "try_no_exception");
		auto try_result =
		    analyze_statement(*statement.getTryBlock(), std::move(try_states), paths, path_index);
		result.insert(result.end(), try_result.begin(), try_result.end());

		for (auto handler_index = unsigned{0}; handler_index < statement.getNumHandlers();
		     ++handler_index)
		{
			auto const* handler = statement.getHandler(handler_index);
			auto catch_states = states;
			append_path_label(catch_states, catch_label(*handler, handler_index));
			mark_path_conservative(catch_states);
			auto catch_result = analyze_statement(
			    *handler->getHandlerBlock(), std::move(catch_states), paths, path_index);
			result.insert(result.end(), catch_result.begin(), catch_result.end());
		}

		mark_conservative("AZTECA_PATH_CONSERVATIVE",
		    "try/catch paths are split into no-exception and conservative catch paths",
		    statement.getBeginLoc());
		return result;
	}

	[[nodiscard]] std::string path_name(
	    clang::ReturnStmt const& return_statement, std::size_t index, PathState const& state) const
	{
		auto text = source_text(context_, return_statement.getSourceRange());
		if (text.empty())
		{
			return path_name("path_" + std::to_string(index), state);
		}

		return path_name(sanitize_identifier(text), state);
	}

	[[nodiscard]] static std::string path_name(std::string base_name, PathState const& state)
	{
		for (auto const& label : state.labels)
		{
			base_name += "__" + label;
		}
		return base_name;
	}

	[[nodiscard]] static std::vector<std::string> event_names(std::vector<PathEvent> const& events)
	{
		std::vector<std::string> names;
		names.reserve(events.size());
		for (auto const& event : events)
		{
			names.push_back(event.name);
		}
		deduplicate_strings(names);
		return names;
	}

	static void append_path_label(std::vector<PathState>& states, std::string const& label)
	{
		for (auto& state : states)
		{
			state.labels.push_back(label);
		}
	}

	static void mark_path_conservative(std::vector<PathState>& states)
	{
		for (auto& state : states)
		{
			state.conservative = true;
		}
	}

	static void append_loop_body_observations(
	    std::vector<PathState>& states, std::vector<std::string> const& observations)
	{
		for (auto& state : states)
		{
			state.loop_body_observations.insert(
			    state.loop_body_observations.end(), observations.begin(), observations.end());
			deduplicate_strings(state.loop_body_observations);
		}
	}

	[[nodiscard]] std::vector<SwitchSegment> switch_segments(
	    clang::SwitchStmt const& statement) const
	{
		std::vector<SwitchSegment> segments;
		auto const* body = statement.getBody();
		if (auto const* compound = llvm::dyn_cast_or_null<clang::CompoundStmt>(body))
		{
			for (auto const* child : compound->body())
			{
				append_switch_segment(*child, segments);
			}
			return segments;
		}

		if (body != nullptr)
		{
			append_switch_segment(*body, segments);
		}
		return segments;
	}

	void append_switch_segment(
	    clang::Stmt const& statement, std::vector<SwitchSegment>& segments) const
	{
		if (auto const* case_statement = llvm::dyn_cast<clang::CaseStmt>(&statement))
		{
			segments.push_back({.label = case_label(*case_statement), .statements = {}});
			if (case_statement->getSubStmt() != nullptr)
			{
				append_switch_segment(*case_statement->getSubStmt(), segments);
			}
			return;
		}

		if (auto const* default_statement = llvm::dyn_cast<clang::DefaultStmt>(&statement))
		{
			segments.push_back({.label = "switch_default", .statements = {}});
			if (default_statement->getSubStmt() != nullptr)
			{
				append_switch_segment(*default_statement->getSubStmt(), segments);
			}
			return;
		}

		if (!segments.empty())
		{
			segments.back().statements.push_back(&statement);
		}
	}

	[[nodiscard]] std::string case_label(clang::CaseStmt const& statement) const
	{
		auto text = statement.getLHS() == nullptr
		    ? std::string{}
		    : source_text(context_, statement.getLHS()->getSourceRange());
		if (text.empty())
		{
			text = "case";
		}
		return "switch_case_" + sanitize_identifier(text);
	}

	[[nodiscard]] std::string catch_label(
	    clang::CXXCatchStmt const& statement, unsigned handler_index) const
	{
		if (statement.getExceptionDecl() == nullptr)
		{
			return "catch_all";
		}
		auto type = sanitize_identifier(type_string(context_, statement.getCaughtType()));
		if (type.empty())
		{
			return "catch_" + std::to_string(handler_index + 1U);
		}
		return "catch_" + type;
	}

	[[nodiscard]] static bool is_conservative_control(clang::Stmt const& statement)
	{
		return llvm::isa<clang::ForStmt>(statement) || llvm::isa<clang::WhileStmt>(statement) ||
		    llvm::isa<clang::DoStmt>(statement) || llvm::isa<clang::CXXForRangeStmt>(statement);
	}

	void build_gtest_preview()
	{
		auto target_name = sanitize_identifier(replace_colons(plan_.target.qualified_name));
		plan_.gtest_preview.sample_test_path = "tests/" + target_name + ".sample_test.cpp";
		plan_.gtest_preview.lines.push_back(
		    "auto s = azteca_gen::scenario::" + target_name + "{};");

		for (auto const& field : plan_.receiver_state)
		{
			plan_.gtest_preview.lines.push_back("s.self." + field.name + " = /* value */;");
		}

		for (auto const& port : plan_.dependency_ports)
		{
			if (port.kind == DependencyKind::kQuery || port.kind == DependencyKind::kOperation)
			{
				plan_.gtest_preview.lines.push_back(
				    "s.when." + port.name + "(/* args */).returns(/* value */);");
			}
		}

		if (method_.getReturnType()->isVoidType())
		{
			plan_.gtest_preview.lines.emplace_back("s.call(/* args */);");
		}
		else
		{
			plan_.gtest_preview.lines.emplace_back("auto result = s.call(/* args */);");
			plan_.gtest_preview.lines.emplace_back("EXPECT_EQ(result, /* expected */);");
		}

		for (auto const& port : plan_.dependency_ports)
		{
			if (port.kind == DependencyKind::kEffect || port.kind == DependencyKind::kOperation)
			{
				plan_.gtest_preview.lines.push_back(
				    "s.effects." + port.name + ".expect_once(/* args */);");
			}
		}

		if (plan_.dependency_ports.empty())
		{
			plan_.gtest_preview.lines.emplace_back("s.effects.expect_none();");
		}
	}

	clang::ASTContext& context_;
	clang::CXXMethodDecl const& method_;
	ExtractionPlan plan_;
	std::map<clang::FieldDecl const*, std::size_t> receiver_field_indices_;
	std::set<clang::FieldDecl const*> dependency_fields_;
	std::set<std::string> macro_ranges_;
	std::map<clang::VarDecl const*, std::string> local_types_;
	std::map<clang::VarDecl const*, std::string> local_dependency_sources_;
};

CallEventVisitor::CallEventVisitor(PlanBuilder& builder, std::vector<PathEvent>& events)
    : builder_(builder), events_(events)
{
}

bool CallEventVisitor::VisitCXXMemberCallExpr(clang::CXXMemberCallExpr* call)
{
	auto port = builder_.dependency_port_for_call(*call);
	if (port.has_value())
	{
		events_.push_back({.kind = port->kind, .name = port->name});
	}
	return true;
}

bool CallEventVisitor::VisitCXXOperatorCallExpr(clang::CXXOperatorCallExpr* call)
{
	auto port = builder_.dependency_port_for_call(*call);
	if (port.has_value())
	{
		events_.push_back({.kind = port->kind, .name = port->name});
	}
	return true;
}

bool CallEventVisitor::VisitCallExpr(clang::CallExpr* call)
{
	if (llvm::isa<clang::CXXMemberCallExpr>(call) || llvm::isa<clang::CXXOperatorCallExpr>(call))
	{
		return true;
	}

	auto port = builder_.dependency_port_for_call(*call);
	if (port.has_value())
	{
		events_.push_back({.kind = port->kind, .name = port->name});
	}
	return true;
}

class MethodLookupVisitor : public clang::RecursiveASTVisitor<MethodLookupVisitor>
{
   public:
	MethodLookupVisitor(clang::ASTContext& context, ToolState& state)
	    : context_(context), state_(state)
	{
	}

	bool VisitCXXMethodDecl(clang::CXXMethodDecl* method)
	{
		if (!method_matches_spec(state_.spec, context_, *method))
		{
			return true;
		}

		if (llvm::isa<clang::CXXConstructorDecl>(method) ||
		    llvm::isa<clang::CXXDestructorDecl>(method))
		{
			state_.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_UNSUPPORTED_TARGET",
			    "constructors and destructors are outside Phase A inspect scope",
			    source_location(context_, method->getLocation()));
			return true;
		}

		if (method->isStatic())
		{
			state_.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_STATIC_METHOD",
			    "static member functions are outside Phase A inspect scope",
			    source_location(context_, method->getLocation()));
			return true;
		}

		if (auto const* function_template = method->getDescribedFunctionTemplate();
		    function_template != nullptr)
		{
			if (inspect_template_specializations(*function_template))
			{
				return true;
			}
			record_template_fallback(*method);
			return true;
		}

		if (method->isFunctionTemplateSpecialization())
		{
			if (state_.spec.template_arguments.empty() &&
			    state_.selected_template_without_explicit_args)
			{
				return true;
			}

			auto const* definition =
			    llvm::dyn_cast_or_null<clang::CXXMethodDecl>(method->getDefinition());
			if (definition == nullptr)
			{
				record_template_fallback(*method);
				return true;
			}

			PlanBuilder builder(context_, *definition);
			state_.plans.push_back(builder.build());
			if (state_.spec.template_arguments.empty())
			{
				state_.selected_template_without_explicit_args = true;
			}
			return true;
		}

		auto const* definition =
		    llvm::dyn_cast_or_null<clang::CXXMethodDecl>(method->getDefinition());
		if (definition == nullptr)
		{
			state_.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_METHOD_DECL_ONLY",
			    "matching method has no parsed definition",
			    source_location(context_, method->getLocation()));
			return true;
		}

		PlanBuilder builder(context_, *definition);
		state_.plans.push_back(builder.build());
		return true;
	}

   private:
	[[nodiscard]] bool inspect_template_specializations(
	    clang::FunctionTemplateDecl const& function_template)
	{
		for (auto const* specialization : function_template.specializations())
		{
			auto const* method = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(specialization);
			if (method == nullptr || !method_matches_spec(state_.spec, context_, *method))
			{
				continue;
			}

			if (state_.spec.template_arguments.empty() &&
			    state_.selected_template_without_explicit_args)
			{
				return true;
			}

			auto const* definition =
			    llvm::dyn_cast_or_null<clang::CXXMethodDecl>(method->getDefinition());
			if (definition == nullptr)
			{
				if (!state_.spec.template_arguments.empty())
				{
					record_template_fallback(*method);
					return true;
				}
				continue;
			}

			PlanBuilder builder(context_, *definition);
			state_.plans.push_back(builder.build());
			if (state_.spec.template_arguments.empty())
			{
				state_.selected_template_without_explicit_args = true;
			}
			return true;
		}

		return false;
	}

	void record_template_fallback(clang::CXXMethodDecl const& method)
	{
		if (state_.template_fallback_plan.has_value())
		{
			return;
		}

		PlanBuilder builder(context_, method);
		auto plan = builder.build();
		plan.result = "not-yet-implemented";
		plan.confidence = "low";
		plan.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_TEMPLATE_INSTANTIATION_NOT_FOUND",
		    "matching template method has no instantiated body for the requested template "
		    "arguments",
		    source_location(context_, method.getLocation()));
		state_.template_fallback_plan = std::move(plan);
	}

	clang::ASTContext& context_;
	ToolState& state_;
};

class InspectAstConsumer final : public clang::ASTConsumer
{
   public:
	explicit InspectAstConsumer(ToolState& state) : state_(state) {}

	void HandleTranslationUnit(clang::ASTContext& context) override
	{
		MethodLookupVisitor visitor(context, state_);
		visitor.TraverseDecl(context.getTranslationUnitDecl());
	}

   private:
	ToolState& state_;
};

class InspectFrontendAction final : public clang::ASTFrontendAction
{
   public:
	explicit InspectFrontendAction(ToolState& state) : state_(state) {}

	std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
	    clang::CompilerInstance& compiler_instance, llvm::StringRef input_file) override
	{
		(void)compiler_instance;
		(void)input_file;
		return std::make_unique<InspectAstConsumer>(state_);
	}

   private:
	ToolState& state_;
};

class InspectActionFactory final : public clang::tooling::FrontendActionFactory
{
   public:
	explicit InspectActionFactory(ToolState& state) : state_(state) {}

	std::unique_ptr<clang::FrontendAction> create() override
	{
		return std::make_unique<InspectFrontendAction>(state_);
	}

   private:
	ToolState& state_;
};

[[nodiscard]] std::vector<std::string> source_files(
    clang::tooling::CompilationDatabase const& database,
    std::optional<std::string> const& requested_source)
{
	if (requested_source.has_value())
	{
		return {*requested_source};
	}

	auto files = database.getAllFiles();
	std::ranges::sort(files);
	files.erase(std::unique(files.begin(), files.end()), files.end());
	return files;
}

[[nodiscard]] bool looks_like_constructor_or_destructor(MethodSpec const& spec)
{
	auto separator = spec.qualified_class_name.rfind("::");
	auto class_name = separator == std::string::npos
	    ? spec.qualified_class_name
	    : spec.qualified_class_name.substr(separator + 2);
	return spec.method_name == class_name || spec.method_name == "~" + class_name;
}

} // namespace

InspectResult inspect_method(InspectOptions const& options)
{
	InspectResult result;

	if (looks_like_constructor_or_destructor(options.method))
	{
		result.status = InspectStatus::kUserInputError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_UNSUPPORTED_TARGET",
		    "constructors and destructors are outside Phase A inspect scope");
		return result;
	}

	auto compile_commands_path = std::filesystem::path(options.build_dir) / "compile_commands.json";
	if (!std::filesystem::exists(compile_commands_path))
	{
		result.status = InspectStatus::kCompileDatabaseError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_COMPILE_DB_MISSING",
		    "compile_commands.json was not found in build directory");
		return result;
	}

	std::string error;
	auto database =
	    clang::tooling::CompilationDatabase::loadFromDirectory(options.build_dir, error);
	if (!database)
	{
		result.status = InspectStatus::kCompileDatabaseError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_COMPILE_DB_LOAD_FAILED",
		    error.empty() ? "failed to load compile_commands.json" : error);
		return result;
	}

	auto files = source_files(*database, options.source_file);
	if (files.empty())
	{
		result.status = InspectStatus::kCompileDatabaseError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_COMPILE_DB_EMPTY",
		    "compile database does not contain source files");
		return result;
	}

	ToolState state;
	state.spec = options.method;
	clang::tooling::ClangTool tool(*database, files);
	tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
	    "-Wno-unknown-warning-option", clang::tooling::ArgumentInsertPosition::END));

	InspectActionFactory factory(state);
	auto tool_result = tool.run(&factory);
	if (tool_result != 0)
	{
		result.status = InspectStatus::kCompileDatabaseError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_CLANG_PARSE_FAILED",
		    "Clang failed to parse at least one translation unit");
		return result;
	}

	if (state.plans.empty() && state.template_fallback_plan.has_value())
	{
		result.plan = std::move(*state.template_fallback_plan);
		result.status = result.plan->diagnostics.has_errors()
		    ? InspectStatus::kExtractionPlanningError
		    : InspectStatus::kSuccess;
		return result;
	}

	if (state.plans.empty())
	{
		result.status = InspectStatus::kMethodResolutionError;
		if (state.diagnostics.has_errors())
		{
			result.diagnostics = std::move(state.diagnostics);
		}
		else
		{
			result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_METHOD_NOT_FOUND",
			    "no CXXMethodDecl matched the requested method spec");
		}
		return result;
	}

	if (state.plans.size() > 1U)
	{
		result.status = InspectStatus::kMethodResolutionError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_METHOD_AMBIGUOUS",
		    "method matched multiple translation units; pass --source to disambiguate");
		return result;
	}

	result.plan = std::move(state.plans.front());
	if (result.plan->diagnostics.has_errors() || result.plan->result == "invalid-plan")
	{
		result.status = InspectStatus::kExtractionPlanningError;
	}
	else
	{
		result.status = InspectStatus::kSuccess;
	}
	return result;
}

} // namespace azteca
