#include <clang/AST/ParentMapContext.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <tuple>
#include <utility>

#include "../collect/PathAnalyzer.hpp"
#include "../gtest/GTestPreview.hpp"
#include "../resolve/MethodSelector.hpp"
#include "InspectSource.hpp"
#include "PlanBuilder.hpp"
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

namespace
{

class CallEventVisitor : public clang::RecursiveASTVisitor<CallEventVisitor>
{
   public:
	CallEventVisitor(PlanBuilder& builder, std::vector<inspect_collect::PathEvent>& events)
	    : builder_(builder), events_(events)
	{
	}

	bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr* call)
	{
		auto port = builder_.dependency_port_for_call(*call);
		if (port.has_value())
		{
			events_.push_back({.kind = port->kind, .name = port->name});
		}
		return true;
	}

	bool VisitCXXOperatorCallExpr(clang::CXXOperatorCallExpr* call)
	{
		auto port = builder_.dependency_port_for_call(*call);
		if (port.has_value())
		{
			events_.push_back({.kind = port->kind, .name = port->name});
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

		auto port = builder_.dependency_port_for_call(*call);
		if (port.has_value())
		{
			events_.push_back({.kind = port->kind, .name = port->name});
		}
		return true;
	}

   private:
	PlanBuilder& builder_;
	std::vector<inspect_collect::PathEvent>& events_;
};

} // namespace

[[nodiscard]] std::string PlanBuilder::overload_suffix(clang::FunctionDecl const& callee) const
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
		suffix += sanitize_identifier(type_string(context_, callee.getParamDecl(index)->getType()));
	}

	return suffix.empty() ? "args" : suffix;
}

[[nodiscard]] std::string PlanBuilder::stable_port_name(
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

[[nodiscard]] std::optional<DependencyPort> PlanBuilder::dependency_port_for_call(
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

[[nodiscard]] std::optional<DependencyPort> PlanBuilder::dependency_port_for_call(
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
		    .name = stable_port_name(replace_colons(method->getQualifiedNameAsString()), *method),
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

[[nodiscard]] std::vector<inspect_collect::PathEvent> PlanBuilder::collect_events(
    clang::Stmt const& statement)
{
	std::vector<inspect_collect::PathEvent> events;
	CallEventVisitor visitor(*this, events);
	visitor.TraverseStmt(const_cast<clang::Stmt*>(&statement));
	deduplicate_events(events);
	return events;
}

[[nodiscard]] std::vector<std::string> PlanBuilder::call_argument_types(
    clang::CallExpr const& call) const
{
	std::vector<std::string> argument_types;
	argument_types.reserve(call.getNumArgs());

	for (auto const* argument : call.arguments())
	{
		argument_types.push_back(type_string(context_, argument->getType()));
	}

	return argument_types;
}

[[nodiscard]] bool PlanBuilder::contains_this_argument(clang::CallExpr const& call)
{
	return std::ranges::any_of(call.arguments(),
	    [](clang::Expr const* argument)
	    {
		    return contains_raw_this_expr(argument);
	    });
}

[[nodiscard]] bool PlanBuilder::is_shape_call(clang::CXXMemberCallExpr const& call) const
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
	return variable != nullptr && local_dependency_sources_.contains(variable->getCanonicalDecl());
}

[[nodiscard]] clang::CXXMethodDecl const* PlanBuilder::member_function_pointer_target(
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

[[nodiscard]] FieldAccess PlanBuilder::classify_field_access(clang::MemberExpr const& member)
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

[[nodiscard]] bool PlanBuilder::is_dependency_object_base(clang::MemberExpr const& member)
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

void PlanBuilder::record_dependency_local(clang::VarDecl const& variable)
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

void PlanBuilder::add_shape_observation(clang::MemberExpr const& member)
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

[[nodiscard]] bool PlanBuilder::record_structured_binding_shape(
    clang::DecompositionDecl const& declaration)
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

[[nodiscard]] std::optional<PlanBuilder::StructuredBindingSource>
PlanBuilder::structured_binding_source(clang::DecompositionDecl const& declaration)
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

[[nodiscard]] clang::Expr const* PlanBuilder::unwrap_decomposition_initializer(
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

void PlanBuilder::record_constructor_local_shape(clang::CXXConstructExpr const& expression,
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

[[nodiscard]] clang::VarDecl const* PlanBuilder::local_constructor_target(
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

void PlanBuilder::add_dependency_port(DependencyPort port)
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

} // namespace azteca
