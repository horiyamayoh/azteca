#include "PlanBuilder.hpp"

#include <llvm/Support/Casting.h>

#include <algorithm>
#include <tuple>
#include <utility>

#include "../collect/PathAnalyzer.hpp"
#include "../gtest/GTestPreview.hpp"
#include "../resolve/MethodSelector.hpp"
#include "InspectSource.hpp"
#include "PlanBuilderAst.hpp"

namespace azteca
{

using inspect_collect::deduplicate_events;
using inspect_collect::deduplicate_strings;
using inspect_collect::operator_port_name;
using inspect_collect::remove_trailing_underscore;
using inspect_collect::replace_colons;
using inspect_collect::sanitize_identifier;
using inspect_frontend::access_to_string;
using inspect_frontend::contains_raw_this_expr;
using inspect_frontend::dependency_base_field;
using inspect_frontend::expression_contains;
using inspect_frontend::has_auto_type;
using inspect_frontend::has_explicit_this_base;
using inspect_frontend::has_overload_set;
using inspect_frontend::has_this_base;
using inspect_frontend::is_same_record;
using inspect_frontend::is_std_move_or_forward;
using inspect_frontend::result_is_ignored;
using inspect_frontend::returns_deref_this_identity;
using inspect_frontend::returns_this_identity;
using inspect_frontend::source_location;
using inspect_frontend::source_range;
using inspect_frontend::source_text;
using inspect_frontend::type_string;

PlanBuilder::PlanBuilder(clang::ASTContext& context, clang::CXXMethodDecl const& method)
    : context_(context), method_(method)
{
}

[[nodiscard]] ExtractionPlan PlanBuilder::build()
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
		inspect_collect::PathAnalyzer path_analyzer{
		    context_,
		    [this](std::string rule_id, std::string reason, clang::SourceRange range,
		        bool conservative, std::string certainty)
		    {
			    return make_evidence(std::move(rule_id), std::move(reason), range, conservative,
			        std::move(certainty));
		    },
		    [this](clang::Stmt const& statement)
		    {
			    return collect_events(statement);
		    },
		    [this](std::string code, std::string message, clang::SourceLocation location)
		    {
			    mark_conservative(std::move(code), std::move(message), location);
		    },
		};
		plan_.paths = path_analyzer.analyze(*body);
		deduplicate_strings(plan_.control_flow_summary.conservative_reasons);
		if (!validate_mmir(plan_.mmir, plan_.diagnostics))
		{
			plan_.result = "invalid-plan";
		}
	}
	else
	{
		plan_.result = "invalid-plan";
		plan_.diagnostics.add(DiagnosticSeverity::kWarning, "AZTECA_METHOD_NO_BODY",
		    "target method has no body available");
	}

	finalize_deterministic_order();
	gtest_preview::build(plan_, method_);
	return plan_;
}

bool PlanBuilder::VisitMemberExpr(clang::MemberExpr* member)
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
	add_envelope_requirement(
	    kIsBaseField ? EnvelopeRequirementKind::kBaseState : EnvelopeRequirementKind::kSelfState,
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
		    "address-taken field must be represented as a cell", field->getQualifiedNameAsString(),
		    evidence);
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
		    "anonymous struct or union receiver state is partial in Phase A", field->getLocation());
	}

	return true;
}

bool PlanBuilder::VisitDeclRefExpr(clang::DeclRefExpr* reference)
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
			auto evidence = make_evidence("LR-010",
			    "global variable reference is modeled as env state", reference->getSourceRange());
			add_mmir(MmirNodeKind::kGlobalRef, variable->getNameAsString(), reference->getExprLoc(),
			    reference->getSourceRange(), evidence, variable->getQualifiedNameAsString());
			add_semantic_feature("global_state", ConstructHandling::kModeled,
			    "global variable read/write should be represented by environment state", evidence);
			add_envelope_requirement(EnvelopeRequirementKind::kGlobalEnvironment,
			    "global state is external to receiver self", variable->getQualifiedNameAsString(),
			    evidence);
		}
	}

	return true;
}

bool PlanBuilder::VisitVarDecl(clang::VarDecl* variable)
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

bool PlanBuilder::VisitBinaryOperator(clang::BinaryOperator* binary_operator)
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

bool PlanBuilder::VisitConditionalOperator(clang::ConditionalOperator* conditional_operator)
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

bool PlanBuilder::VisitBinaryConditionalOperator(
    clang::BinaryConditionalOperator* conditional_operator)
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

bool PlanBuilder::VisitUnaryOperator(clang::UnaryOperator* unary_operator)
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
			    unary_operator->getOperatorLoc(), unary_operator->getSourceRange(), evidence, {},
			    kTarget);
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

bool PlanBuilder::VisitIfStmt(clang::IfStmt* if_statement)
{
	plan_.control_flow_summary.has_if = true;
	add_mmir(MmirNodeKind::kIf, "if", if_statement->getIfLoc(), if_statement->getSourceRange(),
	    make_evidence("LR-005", "if statement", if_statement->getSourceRange()));
	return true;
}

bool PlanBuilder::VisitSwitchStmt(clang::SwitchStmt* switch_statement)
{
	plan_.control_flow_summary.has_switch = true;
	auto evidence =
	    make_evidence("LR-015", "switch statement is segmented into case/default paths in Phase A",
	        switch_statement->getSourceRange());
	add_mmir(MmirNodeKind::kSwitch, "switch", switch_statement->getSwitchLoc(),
	    switch_statement->getSourceRange(), evidence);
	add_semantic_feature("switch_control_flow", ConstructHandling::kSupported,
	    "switch case/default labels are represented as path branches", evidence);
	return true;
}

bool PlanBuilder::VisitForStmt(clang::ForStmt* statement)
{
	return record_loop(*statement, statement->getForLoc(), "for loop", "LR-015");
}

bool PlanBuilder::VisitWhileStmt(clang::WhileStmt* statement)
{
	return record_loop(*statement, statement->getWhileLoc(), "while loop", "LR-015");
}

bool PlanBuilder::VisitDoStmt(clang::DoStmt* statement)
{
	return record_loop(*statement, statement->getDoLoc(), "do loop", "LR-015");
}

bool PlanBuilder::VisitCXXForRangeStmt(clang::CXXForRangeStmt* statement)
{
	plan_.control_flow_summary.has_range_for = true;
	return record_loop(*statement, statement->getBeginLoc(), "range-for loop", "LR-016");
}

bool PlanBuilder::VisitBreakStmt(clang::BreakStmt* statement)
{
	auto evidence = make_evidence(
	    "LR-046", "break statement is tracked in control flow", statement->getSourceRange());
	add_mmir(MmirNodeKind::kBreak, "break", statement->getBreakLoc(), statement->getSourceRange(),
	    evidence);
	add_semantic_feature("break_continue", ConstructHandling::kSupported,
	    "break statement is reported as control-flow intent", evidence);
	return true;
}

bool PlanBuilder::VisitContinueStmt(clang::ContinueStmt* statement)
{
	auto evidence = make_evidence(
	    "LR-046", "continue statement is tracked in control flow", statement->getSourceRange());
	add_mmir(MmirNodeKind::kContinue, "continue", statement->getContinueLoc(),
	    statement->getSourceRange(), evidence);
	add_semantic_feature("break_continue", ConstructHandling::kSupported,
	    "continue statement is reported as control-flow intent", evidence);
	return true;
}

bool PlanBuilder::VisitReturnStmt(clang::ReturnStmt* return_statement)
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

bool PlanBuilder::VisitCXXMemberCallExpr(clang::CXXMemberCallExpr* call)
{
	if (auto const* callee = call->getMethodDecl();
	    llvm::isa_and_nonnull<clang::CXXDestructorDecl>(callee))
	{
		auto evidence = make_evidence(
		    "LR-027", "explicit destructor call requires lifetime state", call->getSourceRange());
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
			    "virtual dispatch uses receiver identity", port->original_callee, port->evidence);
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

bool PlanBuilder::VisitCallExpr(clang::CallExpr* call)
{
	if (llvm::isa<clang::CXXMemberCallExpr>(call) || llvm::isa<clang::CXXOperatorCallExpr>(call))
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

bool PlanBuilder::VisitCXXOperatorCallExpr(clang::CXXOperatorCallExpr* call)
{
	auto const* callee = call->getDirectCallee();
	if (auto const* method = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(callee);
	    method != nullptr && method->getParent() != nullptr && method->getParent()->isLambda())
	{
		auto evidence = make_evidence("LR-017",
		    "lambda call operator is local logic in Phase A inspect", call->getSourceRange());
		add_mmir(MmirNodeKind::kLambda, "lambda_call", call->getExprLoc(), call->getSourceRange(),
		    evidence);
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
	    call->getSourceRange(), evidence, sanitize_identifier(original_symbol), original_symbol);
	add_semantic_feature("overloaded_operator", ConstructHandling::kBoundary,
	    "operator call is resolved and tracked as a dependency candidate", evidence);
	add_construct("overloaded operator", ConstructHandling::kBoundary,
	    "operator semantics are preserved through resolved callee metadata",
	    {"direct lowering for built-in operators", "dependency boundary for overloaded operators"},
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

bool PlanBuilder::VisitLambdaExpr(clang::LambdaExpr* lambda)
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

bool PlanBuilder::VisitDependentScopeDeclRefExpr(clang::DependentScopeDeclRefExpr* expression)
{
	auto evidence =
	    make_evidence("LR-033", "dependent name cannot be resolved without template instantiation",
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

bool PlanBuilder::VisitCXXDependentScopeMemberExpr(clang::CXXDependentScopeMemberExpr* expression)
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

bool PlanBuilder::VisitCXXThrowExpr(clang::CXXThrowExpr* throw_expression)
{
	plan_.control_flow_summary.has_throw = true;
	auto evidence = make_evidence("LR-035", "throw expression is part of unit-observable semantics",
	    throw_expression->getSourceRange());
	add_mmir(MmirNodeKind::kThrow, "throw", throw_expression->getThrowLoc(),
	    throw_expression->getSourceRange(), evidence);
	add_semantic_feature("exception_throw", ConstructHandling::kSupported,
	    "throw expression is preserved as exception intent", evidence);
	add_envelope_requirement(EnvelopeRequirementKind::kExceptionModel,
	    "exception behavior is unit-observable", "throw", evidence);
	return true;
}

bool PlanBuilder::VisitCXXTryStmt(clang::CXXTryStmt* try_statement)
{
	plan_.control_flow_summary.has_try = true;
	plan_.control_flow_summary.conservative = true;
	plan_.control_flow_summary.conservative_reasons.emplace_back(
	    "try/catch is represented conservatively in Phase A");
	auto evidence =
	    make_evidence("LR-036", "try/catch is represented as conservative exception control flow",
	        try_statement->getSourceRange(), true, "conservative");
	add_mmir(MmirNodeKind::kTry, "try", try_statement->getTryLoc(), try_statement->getSourceRange(),
	    evidence);
	add_semantic_feature("try_catch", ConstructHandling::kConservative,
	    "try/catch paths are summarized conservatively", evidence);
	add_envelope_requirement(EnvelopeRequirementKind::kExceptionModel,
	    "try/catch requires exception-aware lowering", "try", evidence);
	return true;
}

bool PlanBuilder::VisitCXXDynamicCastExpr(clang::CXXDynamicCastExpr* cast)
{
	auto evidence = make_evidence("LR-023",
	    "dynamic_cast involving receiver state requires type_tag modeling", cast->getSourceRange());
	add_mmir(
	    MmirNodeKind::kCast, "dynamic_cast", cast->getBeginLoc(), cast->getSourceRange(), evidence);
	add_semantic_feature("dynamic_type", ConstructHandling::kModeled,
	    "dynamic_cast is represented through explicit dynamic type model", evidence);
	add_envelope_requirement(EnvelopeRequirementKind::kTypeTag, "dynamic_cast requires type_tag",
	    source_text(context_, cast->getSourceRange()), evidence);
	add_construct("dynamic_cast", ConstructHandling::kModeled,
	    "RTTI-dependent decision is modeled explicitly instead of requiring a fake object",
	    {"type_tag model", "live validation"}, cast->getBeginLoc(), evidence);
	return true;
}

bool PlanBuilder::VisitCXXReinterpretCastExpr(clang::CXXReinterpretCastExpr* cast)
{
	auto evidence =
	    make_evidence("LR-025", "reinterpret_cast is represented as byte or dependency boundary",
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

bool PlanBuilder::VisitCXXTypeidExpr(clang::CXXTypeidExpr* typeid_expression)
{
	auto evidence =
	    make_evidence("LR-024", "typeid expression requires explicit type information when dynamic",
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

bool PlanBuilder::VisitCXXDeleteExpr(clang::CXXDeleteExpr* delete_expression)
{
	auto evidence = make_evidence("LR-026", "delete expression requires lifetime intent modeling",
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
	    {"lifetime_state", "delete effect", "live validation"}, delete_expression->getBeginLoc(),
	    evidence);
	return true;
}

bool PlanBuilder::VisitCXXNewExpr(clang::CXXNewExpr* new_expression)
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

bool PlanBuilder::VisitCXXConstructExpr(clang::CXXConstructExpr* construct_expression)
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

bool PlanBuilder::VisitCXXDefaultArgExpr(clang::CXXDefaultArgExpr* expression)
{
	auto const* parameter = expression->getParam();
	auto parameter_name =
	    parameter == nullptr ? std::string{"<unknown>"} : parameter->getNameAsString();
	auto evidence = make_evidence(
	    "LR-042", "default argument use is supplied by Clang Sema", expression->getSourceRange());
	add_mmir(MmirNodeKind::kDefaultArgument, parameter_name, expression->getUsedLocation(),
	    expression->getSourceRange(), evidence);
	add_semantic_feature("default_argument", ConstructHandling::kSupported,
	    "default argument is visible as a semantic call argument", evidence);
	return true;
}

bool PlanBuilder::VisitCXXStaticCastExpr(clang::CXXStaticCastExpr* cast)
{
	return record_supported_cast(*cast, cast->getBeginLoc(), "static_cast");
}

bool PlanBuilder::VisitCXXFunctionalCastExpr(clang::CXXFunctionalCastExpr* cast)
{
	return record_supported_cast(*cast, cast->getBeginLoc(), "functional_cast");
}

bool PlanBuilder::VisitCStyleCastExpr(clang::CStyleCastExpr* cast)
{
	return record_supported_cast(*cast, cast->getBeginLoc(), "c_style_cast");
}

bool PlanBuilder::VisitCXXConstCastExpr(clang::CXXConstCastExpr* cast)
{
	auto evidence =
	    make_evidence("LR-045", "const_cast may change mutability and is represented as a boundary",
	        cast->getSourceRange(), true, "conservative");
	add_mmir(
	    MmirNodeKind::kCast, "const_cast", cast->getBeginLoc(), cast->getSourceRange(), evidence);
	add_semantic_feature("cast_expression", ConstructHandling::kBoundary,
	    "const_cast requires mutability-aware lowering", evidence);
	add_construct("const_cast", ConstructHandling::kBoundary,
	    "const removal is not approximated through fake object access",
	    {"mutability-aware boundary", "live validation"}, cast->getBeginLoc(), evidence);
	return true;
}

bool PlanBuilder::VisitUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr* expression)
{
	auto evidence = make_evidence("LR-039",
	    "unevaluated type/value context is tracked without dependency effects",
	    expression->getSourceRange());
	add_mmir(MmirNodeKind::kUnevaluatedContext, "unevaluated_context", expression->getBeginLoc(),
	    expression->getSourceRange(), evidence);
	add_semantic_feature("unevaluated_context", ConstructHandling::kSupported,
	    "sizeof/alignof style expression is tracked without creating effects", evidence);
	return true;
}

bool PlanBuilder::VisitCXXNoexceptExpr(clang::CXXNoexceptExpr* expression)
{
	auto evidence = make_evidence(
	    "LR-039", "noexcept operand is an unevaluated context", expression->getSourceRange());
	add_mmir(MmirNodeKind::kUnevaluatedContext, "noexcept_expr", expression->getBeginLoc(),
	    expression->getSourceRange(), evidence);
	add_semantic_feature("unevaluated_context", ConstructHandling::kSupported,
	    "noexcept expression is tracked without dependency effects", evidence);
	return true;
}

bool PlanBuilder::VisitDecompositionDecl(clang::DecompositionDecl* declaration)
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
	    {"decomposition lowering", "shape field expansion"}, declaration->getBeginLoc(), evidence);
	return true;
}

bool PlanBuilder::VisitCoroutineBodyStmt(clang::CoroutineBodyStmt* statement)
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

bool PlanBuilder::record_loop(clang::Stmt const& statement, clang::SourceLocation location,
    std::string_view label, std::string rule_id)
{
	plan_.control_flow_summary.has_loop = true;
	plan_.control_flow_summary.conservative = true;
	plan_.control_flow_summary.conservative_reasons.emplace_back(
	    std::string(label) + " is represented conservatively in Phase A");
	auto evidence = make_evidence(std::move(rule_id),
	    std::string(label) + " is a conservative path region in Phase A",
	    statement.getSourceRange(), true, "conservative");
	add_mmir(
	    MmirNodeKind::kLoop, std::string(label), location, statement.getSourceRange(), evidence);
	add_semantic_feature("loop_control_flow", ConstructHandling::kConservative,
	    "loop body dependencies are summarized on a conservative path", evidence);
	return true;
}

bool PlanBuilder::record_supported_cast(
    clang::ExplicitCastExpr const& cast, clang::SourceLocation location, std::string label)
{
	auto evidence =
	    make_evidence("LR-044", "cast expression is resolved by Clang Sema", cast.getSourceRange());
	add_mmir(MmirNodeKind::kCast, std::move(label), location, cast.getSourceRange(), evidence);
	add_semantic_feature("cast_expression", ConstructHandling::kSupported,
	    "cast expression is represented from resolved AST type information", evidence);
	return true;
}

} // namespace azteca
