#include <llvm/Support/Casting.h>

#include <algorithm>
#include <tuple>
#include <utility>

#include "../collect/PathAnalyzer.hpp"
#include "../gtest/GTestPreview.hpp"
#include "../plan/RuleCoverageRegistry.hpp"
#include "../resolve/MethodSelector.hpp"
#include "InspectSource.hpp"
#include "PlanBuilder.hpp"
#include "PlanBuilderAst.hpp"

namespace
{

[[nodiscard]] bool contains_nontrivial_default_initializer(clang::Stmt const* statement)
{
	if (statement == nullptr)
	{
		return false;
	}

	if (llvm::isa<clang::CallExpr>(statement))
	{
		return true;
	}

	return std::ranges::any_of(statement->children(),
	    [](clang::Stmt const* child)
	    {
		    return contains_nontrivial_default_initializer(child);
	    });
}

} // namespace

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

void PlanBuilder::initialize_rule_coverage()
{
	plan_.rule_coverage = plan::phase_a_rule_coverage();
}

void PlanBuilder::mark_rule_observed(std::string const& rule_id)
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

void PlanBuilder::finalize_deterministic_order()
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

void PlanBuilder::record_method_qualifiers()
{
	if (method_.isConst() || method_.isVolatile() || method_.getRefQualifier() != clang::RQ_None)
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
		add_semantic_feature("method_qualifier", ConstructHandling::kSupported, details, evidence);
	}

	auto const* proto = method_.getType()->getAs<clang::FunctionProtoType>();
	if (proto != nullptr && proto->isNothrow())
	{
		auto evidence =
		    make_evidence("LR-019", "target method has noexcept-compatible exception specification",
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

		auto evidence = make_evidence("LR-042", "target method parameter has a default argument",
		    parameter->getSourceRange());
		add_semantic_feature("default_argument", ConstructHandling::kSupported,
		    "target parameter default argument is visible in inspect metadata", evidence);
	}

	if (llvm::isa<clang::ClassTemplateSpecializationDecl>(method_.getParent()))
	{
		auto evidence = make_evidence("LR-033", "target belongs to a class template specialization",
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
		    "target is an instantiated function template specialization", method_.getSourceRange());
		add_semantic_feature("template_specialization", ConstructHandling::kSupported,
		    "function template specialization is inspected from its instantiated body", evidence);
	}
}

[[nodiscard]] PlanEvidence PlanBuilder::make_evidence(std::string rule_id, std::string reason,
    clang::SourceRange range, bool conservative, std::string certainty) const
{
	return inspect_frontend::make_evidence(
	    context_, std::move(rule_id), std::move(reason), range, conservative, std::move(certainty));
}

void PlanBuilder::mark_conservative(
    std::string code, std::string message, clang::SourceLocation location)
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

void PlanBuilder::add_semantic_feature(
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

void PlanBuilder::add_construct(std::string construct, ConstructHandling handling,
    std::string reason, std::vector<std::string> fallbacks, clang::SourceLocation location,
    PlanEvidence evidence)
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

void PlanBuilder::add_envelope_requirement(
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

void PlanBuilder::record_macro_if_needed(clang::SourceRange range)
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

	auto evidence = make_evidence("LR-034", "macro-expanded source range is tracked conservatively",
	    range, true, "conservative");
	add_semantic_feature("macro_expansion", ConstructHandling::kConservative,
	    "source spelling may differ from AST semantics", evidence);
	add_construct("macro expansion", ConstructHandling::kConservative,
	    "macro expansion requires source-map-aware regeneration",
	    {"use AST semantics for inspect", "treat complex macro spelling as conservative"},
	    range.getBegin(), evidence);
	add_envelope_requirement(EnvelopeRequirementKind::kMacroSourceMap,
	    "macro source mapping is required for faithful diagnostics", rendered_range, evidence);
}

void PlanBuilder::add_receiver_field(
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
		record_default_member_initializer_if_needed(field);
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

void PlanBuilder::record_default_member_initializer_if_needed(clang::FieldDecl const& field)
{
	auto const* initializer = field.getInClassInitializer();
	if (initializer == nullptr || !contains_nontrivial_default_initializer(initializer))
	{
		return;
	}

	auto evidence = make_evidence("LR-040",
	    "non-trivial default member initializer requires construction-aware receiver setup",
	    initializer->getSourceRange(), true, "conservative");
	add_semantic_feature("default_member_initializer", ConstructHandling::kNotYetImplemented,
	    "receiver default member initializer is not lowered in Phase A", evidence);
	add_construct("default member initializer", ConstructHandling::kNotYetImplemented,
	    "receiver default member initializer requires future construction-aware lowering",
	    {"explicit self setup", "future construction-aware self model"}, field.getLocation(),
	    evidence);
}

void PlanBuilder::remove_dependency_object_fields()
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

void PlanBuilder::add_object_ref_requirement(std::string reason, std::string expression,
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

void PlanBuilder::add_unclassified_call(clang::CallExpr const& call)
{
	auto const* callee = call.getDirectCallee();
	auto original_symbol = callee == nullptr ? std::string{} : callee->getQualifiedNameAsString();
	if (original_symbol.empty())
	{
		original_symbol = "unknown_call";
	}

	auto evidence =
	    make_evidence("CALL-BOUNDARY", "call is represented as a conservative dependency boundary",
	        call.getSourceRange(), true, "conservative");
	add_mmir(MmirNodeKind::kBoundaryCall, original_symbol, call.getExprLoc(), call.getSourceRange(),
	    evidence, sanitize_identifier(original_symbol), original_symbol);
	add_construct("call boundary", ConstructHandling::kBoundary,
	    "callee could not be lowered directly during Phase A inspect",
	    {"dependency transcript boundary", "recursive extraction when a body is available"},
	    call.getExprLoc(), evidence);
	add_envelope_requirement(EnvelopeRequirementKind::kDependencyBoundary,
	    "unclassified call is preserved as an explicit boundary", original_symbol, evidence);
}

void PlanBuilder::add_mmir(MmirNodeKind kind, std::string label, clang::SourceLocation location,
    clang::SourceRange range, PlanEvidence const& evidence, std::string semantic_id,
    std::string original_symbol)
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

} // namespace azteca
