#include "azteca/InspectFrontend.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

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
	Diagnostics diagnostics;
};

struct PathEvent
{
	DependencyKind kind{DependencyKind::kQuery};
	std::string name;
};

struct PathState
{
	std::vector<PathEvent> events;
	bool conservative{false};
};

[[nodiscard]] std::string remove_trailing_underscore(std::string value)
{
	if (!value.empty() && value.back() == '_')
	{
		value.pop_back();
	}
	return value;
}

[[nodiscard]] std::string replace_colons(std::string value)
{
	std::string result;
	result.reserve(value.size());

	for (auto index = std::size_t{0}; index < value.size(); ++index)
	{
		if (value[index] == ':' && index + 1U < value.size() && value[index + 1U] == ':')
		{
			result.push_back('_');
			++index;
			continue;
		}

		result.push_back(value[index]);
	}

	return result;
}

[[nodiscard]] std::string sanitize_identifier(std::string const& value)
{
	std::string result;
	result.reserve(value.size());

	for (char character : value)
	{
		if (std::isalnum(static_cast<unsigned char>(character)) != 0)
		{
			result.push_back(
			    static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
		}
		else if (!result.empty() && result.back() != '_')
		{
			result.push_back('_');
		}
	}

	while (!result.empty() && result.back() == '_')
	{
		result.pop_back();
	}

	if (result.empty())
	{
		return "path";
	}

	return result;
}

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
		plan_.target.qualified_name = method_.getQualifiedNameAsString();
		plan_.target.signature = method_signature(context_, method_);
		plan_.target.source_file = source_location(context_, method_.getLocation()).file;
		plan_.target.line = source_location(context_, method_.getLocation()).line;
		plan_.result = "extracted";
		plan_.mmir.target_name = plan_.target.qualified_name;

		if (auto const* body = method_.getBody())
		{
			TraverseStmt(const_cast<clang::Stmt*>(body));
			remove_dependency_object_fields();
			plan_.paths = analyze_paths(*body);
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

		std::ranges::sort(plan_.receiver_state,
		    [](ReceiverField const& lhs, ReceiverField const& rhs)
		    {
			    return lhs.name < rhs.name;
		    });

		std::ranges::sort(plan_.dependency_ports,
		    [](DependencyPort const& lhs, DependencyPort const& rhs)
		    {
			    if (lhs.kind != rhs.kind)
			    {
				    return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
			    }
			    return lhs.name < rhs.name;
		    });

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

		auto access = classify_field_access(*member);
		auto const* rule_id = access == FieldAccess::kRead ? "LR-001" : "LR-003";
		auto const* reason =
		    access == FieldAccess::kRead ? "receiver field read" : "receiver field mutation";
		auto evidence = make_evidence(rule_id, reason, member->getSourceRange());
		add_mmir(MmirNodeKind::kFieldRef, field->getNameAsString(), member->getExprLoc(),
		    member->getSourceRange(), evidence, field->getQualifiedNameAsString());
		add_receiver_field(*field, access, evidence);

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
		else if (llvm::isa<clang::VarDecl>(reference->getDecl()))
		{
			add_mmir(MmirNodeKind::kLocalRef, reference->getDecl()->getNameAsString(),
			    reference->getExprLoc(), reference->getSourceRange(),
			    make_evidence(
			        "MMIR-LOCAL", "local variable reference", reference->getSourceRange()),
			    reference->getDecl()->getQualifiedNameAsString());
		}

		return true;
	}

	bool VisitVarDecl(clang::VarDecl* variable)
	{
		if (variable->isLocalVarDecl())
		{
			local_types_[variable->getCanonicalDecl()] = type_string(context_, variable->getType());
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
		return true;
	}

	bool VisitUnaryOperator(clang::UnaryOperator* unary_operator)
	{
		add_mmir(MmirNodeKind::kUnary,
		    clang::UnaryOperator::getOpcodeStr(unary_operator->getOpcode()).str(),
		    unary_operator->getOperatorLoc(), unary_operator->getSourceRange(),
		    make_evidence("LR-006", "unary expression", unary_operator->getSourceRange()));
		return true;
	}

	bool VisitIfStmt(clang::IfStmt* if_statement)
	{
		add_mmir(MmirNodeKind::kIf, "if", if_statement->getIfLoc(), if_statement->getSourceRange(),
		    make_evidence("LR-005", "if statement", if_statement->getSourceRange()));
		return true;
	}

	bool VisitReturnStmt(clang::ReturnStmt* return_statement)
	{
		add_mmir(MmirNodeKind::kReturn, "return", return_statement->getReturnLoc(),
		    return_statement->getSourceRange(),
		    make_evidence("LR-004", "return statement", return_statement->getSourceRange()));
		if (returns_this_identity(return_statement->getRetValue()))
		{
			add_object_ref_requirement("return this",
			    source_text(context_, return_statement->getSourceRange()),
			    return_statement->getReturnLoc(), return_statement->getSourceRange(),
			    "OBJ-RETURN-THIS");
		}
		return true;
	}

	bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr* call)
	{
		auto port = dependency_port_for_call(*call);
		if (port.has_value())
		{
			add_dependency_port(*port);
			add_mmir(MmirNodeKind::kBoundaryCall, port->name, call->getExprLoc(),
			    call->getSourceRange(), port->evidence, port->name, port->original_callee);
		}
		else if (!is_shape_call(*call))
		{
			add_unclassified_call(*call);
		}

		if (contains_this_argument(*call))
		{
			add_object_ref_requirement("this passed to dependency",
			    source_text(context_, call->getSourceRange()), call->getExprLoc(),
			    call->getSourceRange(), "OBJ-THIS-ESCAPE");
		}

		return true;
	}

	bool VisitCallExpr(clang::CallExpr* call)
	{
		if (llvm::isa<clang::CXXMemberCallExpr>(call))
		{
			return true;
		}

		auto port = dependency_port_for_call(*call);
		if (port.has_value())
		{
			add_dependency_port(*port);
			add_mmir(MmirNodeKind::kBoundaryCall, port->name, call->getExprLoc(),
			    call->getSourceRange(), port->evidence, port->name, port->original_callee);
		}
		else
		{
			add_unclassified_call(*call);
		}

		if (contains_this_argument(*call))
		{
			add_object_ref_requirement("this passed to dependency",
			    source_text(context_, call->getSourceRange()), call->getExprLoc(),
			    call->getSourceRange(), "OBJ-THIS-ESCAPE");
		}

		return true;
	}

	[[nodiscard]] std::optional<DependencyPort> dependency_port_for_call(
	    clang::CXXMemberCallExpr const& call)
	{
		auto const* callee = call.getMethodDecl();
		if (callee == nullptr)
		{
			return std::nullopt;
		}

		if (is_same_record(callee->getParent(), method_.getParent()) && !callee->isVirtual())
		{
			return DependencyPort{
			    .kind = DependencyKind::kRecursiveCandidate,
			    .name = callee->getNameAsString(),
			    .original_callee = callee->getQualifiedNameAsString(),
			    .return_type = type_string(context_, callee->getReturnType()),
			    .argument_types = call_argument_types(call),
			    .location = source_location(context_, call.getExprLoc()),
			    .evidence = make_evidence("DEP-SAME-CLASS-HELPER",
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

		auto kind = DependencyKind::kQuery;
		auto rule_id = std::string{"DEP-MEMBER-QUERY"};
		auto reason = std::string{"const non-void member object call is a query"};
		if (callee->getReturnType()->isVoidType() || result_is_ignored(context_, call))
		{
			kind = DependencyKind::kEffect;
			rule_id = "DEP-MEMBER-EFFECT";
			reason = "void or ignored-return member object call is an effect";
		}
		else if (!callee->isConst())
		{
			kind = DependencyKind::kOperation;
			rule_id = "DEP-MEMBER-OPERATION";
			reason = "non-const non-void member object call is an operation";
		}

		auto base_name = remove_trailing_underscore(base_field->getNameAsString());
		return DependencyPort{
		    .kind = kind,
		    .name = base_name + "_" + callee->getNameAsString(),
		    .original_callee = callee->getQualifiedNameAsString(),
		    .return_type = type_string(context_, callee->getReturnType()),
		    .argument_types = call_argument_types(call),
		    .location = source_location(context_, call.getExprLoc()),
		    .evidence = make_evidence(std::move(rule_id), std::move(reason), call.getSourceRange()),
		};
	}

	[[nodiscard]] std::optional<DependencyPort> dependency_port_for_call(
	    clang::CallExpr const& call)
	{
		auto const* callee = call.getDirectCallee();
		if (callee == nullptr || llvm::isa<clang::CXXMethodDecl>(callee))
		{
			return std::nullopt;
		}

		auto kind = DependencyKind::kQuery;
		auto rule_id = std::string{"DEP-FREE-QUERY"};
		auto reason = std::string{"non-void free function call is a query boundary"};
		if (callee->getReturnType()->isVoidType() || result_is_ignored(context_, call))
		{
			kind = DependencyKind::kEffect;
			rule_id = "DEP-FREE-EFFECT";
			reason = "void or ignored-return free function call is an effect boundary";
		}

		return DependencyPort{
		    .kind = kind,
		    .name = sanitize_identifier(replace_colons(callee->getQualifiedNameAsString())),
		    .original_callee = callee->getQualifiedNameAsString(),
		    .return_type = type_string(context_, callee->getReturnType()),
		    .argument_types = call_argument_types(call),
		    .location = source_location(context_, call.getExprLoc()),
		    .evidence = make_evidence(std::move(rule_id), std::move(reason), call.getSourceRange()),
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

		plan_.diagnostics.add(DiagnosticSeverity::kWarning, std::move(code), std::move(message),
		    source_location(context_, location));
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
			    return contains_this_expr(argument);
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
		auto shape_name = type == local_types_.end() ? dependency->second + "_shape"
		                                             : shape_name_from_type(type->second);
		auto observed_member = member.getMemberDecl()->getNameAsString();

		auto shape_iterator = std::ranges::find_if(plan_.shape_candidates,
		    [&shape_name, &dependency](ShapeCandidate const& candidate)
		    {
			    return candidate.name == shape_name &&
			        candidate.source_dependency == dependency->second;
		    });

		if (shape_iterator == plan_.shape_candidates.end())
		{
			plan_.shape_candidates.push_back({
			    .name = std::move(shape_name),
			    .source_dependency = dependency->second,
			    .observed_members = {observed_member},
			    .evidence = make_evidence("SHAPE-DEPENDENCY-RETURN",
			        "member access on dependency return local suggests a shape candidate",
			        member.getSourceRange()),
			});
			return;
		}

		if (std::ranges::find(shape_iterator->observed_members, observed_member) ==
		    shape_iterator->observed_members.end())
		{
			shape_iterator->observed_members.push_back(std::move(observed_member));
		}
	}

	[[nodiscard]] static std::string shape_name_from_type(std::string type)
	{
		static constexpr auto kTokens = std::array<std::string_view, 3>{"const", "&", "*"};
		for (std::string_view token : kTokens)
		{
			auto position = std::string::size_type{0};
			while ((position = type.find(token, position)) != std::string::npos)
			{
				type.erase(position, token.size());
			}
		}

		auto namespace_separator = type.rfind("::");
		if (namespace_separator != std::string::npos)
		{
			type = type.substr(namespace_separator + 2);
		}

		type = sanitize_identifier(type);
		if (type.empty())
		{
			return "DependencyShape";
		}

		type[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(type[0])));
		return type + "Shape";
	}

	void add_dependency_port(DependencyPort port)
	{
		auto duplicate = std::ranges::any_of(plan_.dependency_ports,
		    [&port](DependencyPort const& existing)
		    {
			    return existing.kind == port.kind && existing.name == port.name &&
			        existing.original_callee == port.original_callee;
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
			plan_.object_ref_requirements.push_back({
			    .reason = std::move(reason),
			    .expression = std::move(expression),
			    .location = std::move(source),
			    .evidence = make_evidence(std::move(rule_id),
			        "this identity is observable and requires object_ref", range),
			});
			add_mmir(MmirNodeKind::kObjectRefRequirement, "object_ref", location, range,
			    plan_.object_ref_requirements.back().evidence, "object_ref");
		}
	}

	void add_unclassified_call(clang::CallExpr const& call)
	{
		auto const* callee = call.getDirectCallee();
		auto original_symbol =
		    callee == nullptr ? std::string{} : callee->getQualifiedNameAsString();
		add_mmir(MmirNodeKind::kCall,
		    original_symbol.empty() ? "unclassified call" : original_symbol, call.getExprLoc(),
		    call.getSourceRange(),
		    make_evidence("CALL-UNCLASSIFIED", "call was not classified by Phase A planner",
		        call.getSourceRange()),
		    "", std::move(original_symbol));
	}

	void add_mmir(MmirNodeKind kind, std::string label, clang::SourceLocation location,
	    clang::SourceRange range, PlanEvidence const& evidence, std::string semantic_id = {},
	    std::string original_symbol = {})
	{
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

		if (paths.empty())
		{
			for (auto const& state : fallthrough_states)
			{
				paths.push_back(build_path("path_" + std::to_string(path_index), state.events,
				    make_evidence(state.conservative ? "PATH-CONSERVATIVE" : "PATH-DFS",
				        state.conservative ? "fallthrough path includes conservative control flow"
				                           : "fallthrough path accumulated by DFS",
				        body.getSourceRange(), state.conservative,
				        state.conservative ? "conservative" : "certain")));
				++path_index;
			}
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
				paths.push_back(build_path(path_name(*return_statement, path_index), state.events,
				    make_evidence(state.conservative ? "PATH-CONSERVATIVE" : "PATH-DFS",
				        state.conservative ? "return path includes conservative control flow"
				                           : "return path accumulated by DFS",
				        return_statement->getSourceRange(), state.conservative,
				        state.conservative ? "conservative" : "certain")));
				++path_index;
			}
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

	[[nodiscard]] std::string path_name(
	    clang::ReturnStmt const& return_statement, std::size_t index) const
	{
		auto text = source_text(context_, return_statement.getSourceRange());
		if (text.empty())
		{
			return "path_" + std::to_string(index);
		}

		return sanitize_identifier(text);
	}

	[[nodiscard]] static bool is_conservative_control(clang::Stmt const& statement)
	{
		return llvm::isa<clang::ForStmt>(statement) || llvm::isa<clang::WhileStmt>(statement) ||
		    llvm::isa<clang::DoStmt>(statement) || llvm::isa<clang::CXXForRangeStmt>(statement) ||
		    llvm::isa<clang::SwitchStmt>(statement) || llvm::isa<clang::CXXTryStmt>(statement);
	}

	[[nodiscard]] static PathBurden build_path(
	    std::string name, std::vector<PathEvent> const& events, PlanEvidence evidence)
	{
		PathBurden burden;
		burden.name = std::move(name);

		for (auto const& event : events)
		{
			switch (event.kind)
			{
				case DependencyKind::kQuery:
					burden.observations.push_back(event.name);
					break;
				case DependencyKind::kEffect:
					burden.effects.push_back(event.name);
					break;
				case DependencyKind::kOperation:
					burden.operations.push_back(event.name);
					break;
				case DependencyKind::kRecursiveCandidate:
					break;
			}
		}

		deduplicate_strings(burden.observations);
		deduplicate_strings(burden.effects);
		deduplicate_strings(burden.operations);
		burden.evidence = std::move(evidence);
		return burden;
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

		plan_.gtest_preview.lines.emplace_back("auto result = s.call(/* args */);");
		plan_.gtest_preview.lines.emplace_back("EXPECT_EQ(result, /* expected */);");

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

	static void append_events(std::vector<PathEvent>& target, std::vector<PathEvent> const& source)
	{
		target.insert(target.end(), source.begin(), source.end());
		deduplicate_events(target);
	}

	static void append_events(std::vector<PathState>& states, std::vector<PathEvent> const& source)
	{
		for (auto& state : states)
		{
			append_events(state.events, source);
		}
	}

	static void deduplicate_events(std::vector<PathEvent>& events)
	{
		std::set<std::pair<DependencyKind, std::string>> seen;
		std::vector<PathEvent> filtered;
		filtered.reserve(events.size());

		for (auto const& event : events)
		{
			auto key = std::make_pair(event.kind, event.name);
			if (seen.insert(key).second)
			{
				filtered.push_back(event);
			}
		}

		events = std::move(filtered);
	}

	static void deduplicate_strings(std::vector<std::string>& values)
	{
		std::set<std::string> seen;
		std::vector<std::string> filtered;
		filtered.reserve(values.size());

		for (auto const& value : values)
		{
			if (seen.insert(value).second)
			{
				filtered.push_back(value);
			}
		}

		values = std::move(filtered);
	}

	clang::ASTContext& context_;
	clang::CXXMethodDecl const& method_;
	ExtractionPlan plan_;
	std::map<clang::FieldDecl const*, std::size_t> receiver_field_indices_;
	std::set<clang::FieldDecl const*> dependency_fields_;
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

bool CallEventVisitor::VisitCallExpr(clang::CallExpr* call)
{
	if (llvm::isa<clang::CXXMemberCallExpr>(call))
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

		if (method->getTemplatedKind() != clang::FunctionDecl::TK_NonTemplate)
		{
			state_.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_TEMPLATE_METHOD",
			    "template methods are outside Phase A inspect scope",
			    source_location(context_, method->getLocation()));
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
