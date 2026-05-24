#include "azteca/InspectReport.hpp"

#include <algorithm>
#include <sstream>

#include "JsonWriter.hpp"
#include "azteca/DiagnosticCatalog.hpp"

namespace azteca
{
namespace
{

[[nodiscard]] std::string to_json_string(FieldAccess access)
{
	switch (access)
	{
		case FieldAccess::kRead:
			return "read";
		case FieldAccess::kWrite:
			return "write";
		case FieldAccess::kReadWrite:
			return "read-write";
		case FieldAccess::kAddress:
			return "address";
	}

	return "read";
}

[[nodiscard]] std::string to_json_string(DependencyKind kind)
{
	switch (kind)
	{
		case DependencyKind::kRecursiveCandidate:
			return "recursive-candidate";
		case DependencyKind::kQuery:
			return "query";
		case DependencyKind::kEffect:
			return "effect";
		case DependencyKind::kOperation:
			return "operation";
	}

	return "query";
}

[[nodiscard]] std::string to_json_string(ConstructHandling handling)
{
	switch (handling)
	{
		case ConstructHandling::kSupported:
			return "supported";
		case ConstructHandling::kModeled:
			return "modeled";
		case ConstructHandling::kBoundary:
			return "boundary";
		case ConstructHandling::kConservative:
			return "conservative";
		case ConstructHandling::kNotYetImplemented:
			return "not-yet-implemented";
		case ConstructHandling::kNotMeaningful:
			return "not-meaningful";
	}

	return "not-yet-implemented";
}

[[nodiscard]] std::string to_json_string(EnvelopeRequirementKind kind)
{
	switch (kind)
	{
		case EnvelopeRequirementKind::kSelfState:
			return "self-state";
		case EnvelopeRequirementKind::kBaseState:
			return "base-state";
		case EnvelopeRequirementKind::kAddressableCell:
			return "addressable-cell";
		case EnvelopeRequirementKind::kObjectRef:
			return "object-ref";
		case EnvelopeRequirementKind::kDependencyBoundary:
			return "dependency-boundary";
		case EnvelopeRequirementKind::kDispatchTable:
			return "dispatch-table";
		case EnvelopeRequirementKind::kTypeTag:
			return "type-tag";
		case EnvelopeRequirementKind::kLifetimeState:
			return "lifetime-state";
		case EnvelopeRequirementKind::kByteView:
			return "byte-view";
		case EnvelopeRequirementKind::kGlobalEnvironment:
			return "global-environment";
		case EnvelopeRequirementKind::kExceptionModel:
			return "exception-model";
		case EnvelopeRequirementKind::kMacroSourceMap:
			return "macro-source-map";
	}

	return "self-state";
}

[[nodiscard]] std::string to_json_canonical_token(std::string const& value)
{
	std::string canonical = value;
	std::ranges::replace(canonical, '_', '-');
	std::ranges::replace(canonical, '/', '-');
	return canonical;
}

void append_string_array(report::JsonWriter& output, std::vector<std::string> const& values)
{
	output.begin_array();
	for (auto const& value : values)
	{
		output.string(value);
	}
	output.end_array();
}

void append_canonical_token_array(
    report::JsonWriter& output, std::vector<std::string> const& values)
{
	output.begin_array();
	for (auto const& value : values)
	{
		output.string(to_json_canonical_token(value));
	}
	output.end_array();
}

void append_location(report::JsonWriter& output, SourceLocation const& location)
{
	output.begin_object();
	output.key("file");
	output.string(location.file);
	output.key("line");
	output.unsigned_integer(location.line);
	output.key("column");
	output.unsigned_integer(location.column);
	output.end_object();
}

void append_source_range(report::JsonWriter& output, SourceRange const& range)
{
	output.begin_object();
	output.key("begin");
	append_location(output, range.begin);
	output.key("end");
	append_location(output, range.end);
	output.end_object();
}

void append_evidence(report::JsonWriter& output, PlanEvidence const& evidence)
{
	output.key("rule_id");
	output.string(evidence.rule_id);
	output.key("reason");
	output.string(evidence.reason);
	output.key("certainty");
	output.string(evidence.certainty);
	output.key("conservative");
	output.boolean(evidence.conservative);
	output.key("source_range");
	append_source_range(output, evidence.source_range);
}

void append_verbose_evidence(std::ostringstream& output, PlanEvidence const& evidence)
{
	if (evidence.rule_id.empty() && evidence.reason.empty() && !evidence.source_range.is_valid())
	{
		return;
	}

	output << "      rule: " << (evidence.rule_id.empty() ? "unknown" : evidence.rule_id);
	if (!evidence.reason.empty())
	{
		output << " - " << evidence.reason;
	}
	output << '\n';

	output << "      certainty: " << (evidence.certainty.empty() ? "unknown" : evidence.certainty);
	if (evidence.conservative)
	{
		output << " (conservative)";
	}
	output << '\n';

	if (evidence.source_range.is_valid())
	{
		output << "      source: " << evidence.source_range.to_string() << '\n';
	}
}

[[nodiscard]] std::vector<DependencyPort> ports_of_kind(
    ExtractionPlan const& plan, DependencyKind kind)
{
	std::vector<DependencyPort> ports;
	std::ranges::copy_if(plan.dependency_ports, std::back_inserter(ports),
	    [kind](DependencyPort const& port)
	    {
		    return port.kind == kind;
	    });
	return ports;
}

void append_port_lines(
    std::ostringstream& output, std::vector<DependencyPort> const& ports, bool verbose)
{
	if (ports.empty())
	{
		output << "  none\n";
		return;
	}

	for (auto const& port : ports)
	{
		output << "  - " << to_string(port.kind) << ' ' << port.name << '(';
		for (auto index = std::size_t{0}; index < port.argument_types.size(); ++index)
		{
			if (index != 0U)
			{
				output << ", ";
			}
			output << port.argument_types[index];
		}
		output << ')';
		if (!port.return_type.empty() && port.return_type != "void")
		{
			output << " -> " << port.return_type;
		}
		output << '\n';
		if (verbose)
		{
			append_verbose_evidence(output, port.evidence);
		}
	}
}

} // namespace

std::string render_text_report(ExtractionPlan const& plan)
{
	return render_text_report(plan, {});
}

std::string render_text_report(ExtractionPlan const& plan, ReportOptions const& options)
{
	std::ostringstream output;

	output << "Azteca can inspect " << plan.target.qualified_name << ".\n\n";
	output << "Azteca phase: " << plan.azteca_phase << "\n\n";
	output << "Extraction result: " << plan.result << "\n\n";
	output << "Confidence: " << plan.confidence << "\n\n";
	output << "Generated Google Test preview:\n";
	output << "  " << plan.gtest_preview.sample_test_path << "\n\n";

	output << "Receiver state:\n";
	if (plan.receiver_state.empty())
	{
		output << "  none\n";
	}
	else
	{
		for (auto const& field : plan.receiver_state)
		{
			output << "  - " << field.type << ' ' << field.name << ' ' << to_string(field.access)
			       << '\n';
			if (options.verbose)
			{
				append_verbose_evidence(output, field.evidence);
			}
		}
	}
	output << '\n';

	output << "Dependency observations:\n";
	append_port_lines(output, ports_of_kind(plan, DependencyKind::kQuery), options.verbose);
	output << '\n';

	output << "Observable effects:\n";
	append_port_lines(output, ports_of_kind(plan, DependencyKind::kEffect), options.verbose);
	output << '\n';

	output << "Operations:\n";
	append_port_lines(output, ports_of_kind(plan, DependencyKind::kOperation), options.verbose);
	output << '\n';

	output << "Recursive helper candidates:\n";
	append_port_lines(
	    output, ports_of_kind(plan, DependencyKind::kRecursiveCandidate), options.verbose);
	output << '\n';

	output << "Generated shapes:\n";
	if (plan.shape_candidates.empty())
	{
		output << "  none\n";
	}
	else
	{
		for (auto const& shape : plan.shape_candidates)
		{
			output << "  - " << shape.name << " from " << shape.source_dependency << '\n';
			if (options.verbose)
			{
				append_verbose_evidence(output, shape.evidence);
			}
			for (auto const& member : shape.observed_members)
			{
				output << "    - " << member << '\n';
			}
		}
	}
	output << '\n';

	output << "Object identity requirements:\n";
	if (plan.object_ref_requirements.empty())
	{
		output << "  none\n";
	}
	else
	{
		for (auto const& requirement : plan.object_ref_requirements)
		{
			output << "  - " << requirement.reason << ": " << requirement.expression << '\n';
			if (options.verbose)
			{
				append_verbose_evidence(output, requirement.evidence);
			}
		}
	}
	output << '\n';

	output << "Semantic features:\n";
	if (plan.semantic_features.empty())
	{
		output << "  none\n";
	}
	else
	{
		for (auto const& feature : plan.semantic_features)
		{
			output << "  - " << feature.name << " [" << to_string(feature.handling) << "]";
			if (!feature.detail.empty())
			{
				output << ": " << feature.detail;
			}
			output << '\n';
			if (options.verbose)
			{
				append_verbose_evidence(output, feature.evidence);
			}
		}
	}
	output << '\n';

	output << "Envelope requirements:\n";
	if (plan.envelope_requirements.empty())
	{
		output << "  none\n";
	}
	else
	{
		for (auto const& requirement : plan.envelope_requirements)
		{
			output << "  - " << to_string(requirement.kind) << ": " << requirement.reason;
			if (!requirement.source.empty())
			{
				output << " (" << requirement.source << ')';
			}
			output << '\n';
			if (options.verbose)
			{
				append_verbose_evidence(output, requirement.evidence);
			}
		}
	}
	output << '\n';

	output << "Modeled or boundary constructs:\n";
	if (plan.unsupported_or_modeled_constructs.empty())
	{
		output << "  none\n";
	}
	else
	{
		for (auto const& construct : plan.unsupported_or_modeled_constructs)
		{
			output << "  - " << construct.construct << " [" << to_string(construct.handling)
			       << "]: " << construct.reason << '\n';
			if (!construct.fallbacks.empty())
			{
				output << "    fallbacks: ";
				for (auto index = std::size_t{0}; index < construct.fallbacks.size(); ++index)
				{
					if (index != 0U)
					{
						output << ", ";
					}
					output << construct.fallbacks[index];
				}
				output << '\n';
			}
			if (options.verbose)
			{
				append_verbose_evidence(output, construct.evidence);
			}
		}
	}
	output << '\n';

	output << "Control flow summary:\n";
	output << "  if: " << (plan.control_flow_summary.has_if ? "yes" : "no") << '\n';
	output << "  switch: " << (plan.control_flow_summary.has_switch ? "yes" : "no") << '\n';
	output << "  loop: " << (plan.control_flow_summary.has_loop ? "yes" : "no") << '\n';
	output << "  range-for: " << (plan.control_flow_summary.has_range_for ? "yes" : "no") << '\n';
	output << "  try: " << (plan.control_flow_summary.has_try ? "yes" : "no") << '\n';
	output << "  throw: " << (plan.control_flow_summary.has_throw ? "yes" : "no") << '\n';
	output << "  return: " << (plan.control_flow_summary.has_return ? "yes" : "no") << '\n';
	if (plan.control_flow_summary.conservative)
	{
		for (auto const& reason : plan.control_flow_summary.conservative_reasons)
		{
			output << "  conservative: " << reason << '\n';
		}
	}
	output << '\n';

	output << "Path-wise test burden:\n";
	if (plan.paths.empty())
	{
		output << "  none\n";
	}
	else
	{
		for (auto const& path : plan.paths)
		{
			output << "  " << path.name << ":\n";
			if (options.verbose)
			{
				append_verbose_evidence(output, path.evidence);
			}
			output << "    observations: ";
			if (path.observations.empty())
			{
				output << "none";
			}
			else
			{
				for (auto index = std::size_t{0}; index < path.observations.size(); ++index)
				{
					if (index != 0U)
					{
						output << ", ";
					}
					output << path.observations[index];
				}
			}
			output << "\n    effects: ";
			if (path.effects.empty())
			{
				output << "none";
			}
			else
			{
				for (auto index = std::size_t{0}; index < path.effects.size(); ++index)
				{
					if (index != 0U)
					{
						output << ", ";
					}
					output << path.effects[index];
				}
			}
			output << "\n    operations: ";
			if (path.operations.empty())
			{
				output << "none";
			}
			else
			{
				for (auto index = std::size_t{0}; index < path.operations.size(); ++index)
				{
					if (index != 0U)
					{
						output << ", ";
					}
					output << path.operations[index];
				}
			}
			output << "\n    loop body observations: ";
			if (path.loop_body_observations.empty())
			{
				output << "none";
			}
			else
			{
				for (auto index = std::size_t{0}; index < path.loop_body_observations.size();
				     ++index)
				{
					if (index != 0U)
					{
						output << ", ";
					}
					output << path.loop_body_observations[index];
				}
			}
			output << "\n    required envelope: ";
			if (path.required_envelopes.empty())
			{
				output << "none";
			}
			else
			{
				for (auto index = std::size_t{0}; index < path.required_envelopes.size(); ++index)
				{
					if (index != 0U)
					{
						output << ", ";
					}
					output << path.required_envelopes[index];
				}
			}
			if (!path.conservative_reason.empty())
			{
				output << "\n    conservative reason: " << path.conservative_reason;
			}
			output << '\n';
		}
	}
	output << '\n';

	output << "Rule coverage:\n";
	for (auto const& coverage : plan.rule_coverage)
	{
		output << "  - " << coverage.rule_id << " [" << to_string(coverage.handling) << "] "
		       << (coverage.observed ? "observed" : "not observed");
		if (!coverage.note.empty())
		{
			output << ": " << coverage.note;
		}
		output << '\n';
	}
	output << '\n';

	output << "Google Test preview:\n";
	for (auto const& line : plan.gtest_preview.lines)
	{
		output << "  " << line << '\n';
	}
	output << '\n';

	if (!plan.diagnostics.entries().empty())
	{
		output << "Diagnostics:\n";
		for (auto const& diagnostic : plan.diagnostics.entries())
		{
			output << "  - " << to_string(diagnostic.severity) << " " << diagnostic.code << ": "
			       << diagnostic.message;
			if (diagnostic.location.is_valid())
			{
				output << " (" << diagnostic.location.to_string() << ')';
			}
			output << '\n';
		}
	}

	return output.str();
}

std::string render_json_report(ExtractionPlan const& plan)
{
	std::ostringstream stream;
	report::JsonWriter output{stream};

	auto append_port = [&output](DependencyPort const& port)
	{
		output.begin_object();
		output.key("kind");
		output.string(to_json_string(port.kind));
		output.key("name");
		output.string(port.name);
		output.key("original_callee");
		output.string(port.original_callee);
		output.key("return_type");
		output.string(port.return_type);
		output.key("argument_types");
		append_string_array(output, port.argument_types);
		output.key("location");
		append_location(output, port.location);
		append_evidence(output, port.evidence);
		output.end_object();
	};

	auto append_ports = [&output, &append_port](
	                        std::string_view key, std::vector<DependencyPort> const& ports)
	{
		output.key(key);
		output.begin_array();
		for (auto const& port : ports)
		{
			append_port(port);
		}
		output.end_array();
	};

	output.begin_object();
	output.key("schema_version");
	output.integer(plan.schema_version);
	output.key("azteca_phase");
	output.string(plan.azteca_phase);
	output.key("target");
	output.begin_object();
	output.key("qualified_name");
	output.string(plan.target.qualified_name);
	output.key("signature");
	output.string(plan.target.signature);
	output.key("source_file");
	output.string(plan.target.source_file);
	output.key("line");
	output.unsigned_integer(plan.target.line);
	output.end_object();
	output.key("result");
	output.string(plan.result);
	output.key("confidence");
	output.string(plan.confidence);

	output.key("receiver_state");
	output.begin_array();
	for (auto const& field : plan.receiver_state)
	{
		output.begin_object();
		output.key("name");
		output.string(field.name);
		output.key("type");
		output.string(field.type);
		output.key("access");
		output.string(to_json_string(field.access));
		output.key("mutable");
		output.boolean(field.is_mutable);
		output.key("access_specifier");
		output.string(field.access_specifier);
		output.key("location");
		append_location(output, field.location);
		append_evidence(output, field.evidence);
		output.end_object();
	}
	output.end_array();

	append_ports("dependency_observations", ports_of_kind(plan, DependencyKind::kQuery));
	append_ports("observable_effects", ports_of_kind(plan, DependencyKind::kEffect));
	append_ports("operations", ports_of_kind(plan, DependencyKind::kOperation));
	append_ports(
	    "recursive_helper_candidates", ports_of_kind(plan, DependencyKind::kRecursiveCandidate));

	output.key("shape_candidates");
	output.begin_array();
	for (auto const& shape : plan.shape_candidates)
	{
		output.begin_object();
		output.key("name");
		output.string(shape.name);
		output.key("source_dependency");
		output.string(shape.source_dependency);
		output.key("observed_members");
		append_string_array(output, shape.observed_members);
		append_evidence(output, shape.evidence);
		output.end_object();
	}
	output.end_array();

	output.key("object_ref_requirements");
	output.begin_array();
	for (auto const& requirement : plan.object_ref_requirements)
	{
		output.begin_object();
		output.key("requirement_reason");
		output.string(requirement.reason);
		output.key("expression");
		output.string(requirement.expression);
		output.key("location");
		append_location(output, requirement.location);
		append_evidence(output, requirement.evidence);
		output.end_object();
	}
	output.end_array();

	output.key("semantic_features");
	output.begin_array();
	for (auto const& feature : plan.semantic_features)
	{
		output.begin_object();
		output.key("name");
		output.string(feature.name);
		output.key("handling");
		output.string(to_json_string(feature.handling));
		output.key("detail");
		output.string(feature.detail);
		append_evidence(output, feature.evidence);
		output.end_object();
	}
	output.end_array();

	output.key("unsupported_or_modeled_constructs");
	output.begin_array();
	for (auto const& construct : plan.unsupported_or_modeled_constructs)
	{
		output.begin_object();
		output.key("construct");
		output.string(construct.construct);
		output.key("handling");
		output.string(to_json_string(construct.handling));
		output.key("construct_reason");
		output.string(construct.reason);
		output.key("fallbacks");
		append_string_array(output, construct.fallbacks);
		output.key("location");
		append_location(output, construct.location);
		append_evidence(output, construct.evidence);
		output.end_object();
	}
	output.end_array();

	output.key("control_flow_summary");
	output.begin_object();
	output.key("has_if");
	output.boolean(plan.control_flow_summary.has_if);
	output.key("has_switch");
	output.boolean(plan.control_flow_summary.has_switch);
	output.key("has_loop");
	output.boolean(plan.control_flow_summary.has_loop);
	output.key("has_range_for");
	output.boolean(plan.control_flow_summary.has_range_for);
	output.key("has_try");
	output.boolean(plan.control_flow_summary.has_try);
	output.key("has_throw");
	output.boolean(plan.control_flow_summary.has_throw);
	output.key("has_return");
	output.boolean(plan.control_flow_summary.has_return);
	output.key("conservative");
	output.boolean(plan.control_flow_summary.conservative);
	output.key("conservative_reasons");
	append_string_array(output, plan.control_flow_summary.conservative_reasons);
	output.end_object();

	output.key("envelope_requirements");
	output.begin_array();
	for (auto const& requirement : plan.envelope_requirements)
	{
		output.begin_object();
		output.key("kind");
		output.string(to_json_string(requirement.kind));
		output.key("requirement_reason");
		output.string(requirement.reason);
		output.key("source");
		output.string(requirement.source);
		append_evidence(output, requirement.evidence);
		output.end_object();
	}
	output.end_array();

	output.key("rule_coverage");
	output.begin_array();
	for (auto const& coverage : plan.rule_coverage)
	{
		output.begin_object();
		output.key("rule_id");
		output.string(coverage.rule_id);
		output.key("handling");
		output.string(to_json_string(coverage.handling));
		output.key("note");
		output.string(coverage.note);
		output.key("observed");
		output.boolean(coverage.observed);
		output.end_object();
	}
	output.end_array();

	output.key("paths");
	output.begin_array();
	for (auto const& path : plan.paths)
	{
		output.begin_object();
		output.key("name");
		output.string(path.name);
		output.key("observations");
		append_string_array(output, path.observations);
		output.key("effects");
		append_string_array(output, path.effects);
		output.key("operations");
		append_string_array(output, path.operations);
		output.key("loop_body_observations");
		append_string_array(output, path.loop_body_observations);
		output.key("required_envelopes");
		append_canonical_token_array(output, path.required_envelopes);
		output.key("conservative_reason");
		output.string(path.conservative_reason);
		append_evidence(output, path.evidence);
		output.end_object();
	}
	output.end_array();

	output.key("gtest_preview");
	output.begin_object();
	output.key("sample_test_path");
	output.string(plan.gtest_preview.sample_test_path);
	output.key("lines");
	append_string_array(output, plan.gtest_preview.lines);
	output.end_object();

	output.key("diagnostics");
	output.begin_array();
	for (auto const& diagnostic : plan.diagnostics.entries())
	{
		output.begin_object();
		output.key("severity");
		output.string(to_string(diagnostic.severity));
		output.key("code");
		output.string(diagnostic.code);
		if (auto public_id = public_diagnostic_id(diagnostic.code); public_id.has_value())
		{
			output.key("public_id");
			output.string(*public_id);
		}
		output.key("message");
		output.string(diagnostic.message);
		output.key("location");
		append_location(output, diagnostic.location);
		output.end_object();
	}
	output.end_array();
	output.end_object();
	stream << '\n';

	return stream.str();
}

} // namespace azteca
