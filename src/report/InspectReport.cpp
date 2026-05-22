#include "azteca/InspectReport.hpp"

#include <algorithm>
#include <sstream>

namespace azteca
{
namespace
{

[[nodiscard]] std::string json_escape(std::string const& value)
{
	std::string escaped;
	escaped.reserve(value.size() + 8U);

	for (char character : value)
	{
		switch (character)
		{
			case '"':
				escaped += "\\\"";
				break;
			case '\\':
				escaped += "\\\\";
				break;
			case '\n':
				escaped += "\\n";
				break;
			case '\r':
				escaped += "\\r";
				break;
			case '\t':
				escaped += "\\t";
				break;
			default:
				escaped.push_back(character);
				break;
		}
	}

	return escaped;
}

void append_json_string(std::ostringstream& output, std::string const& value)
{
	output << '"' << json_escape(value) << '"';
}

void append_string_array(std::ostringstream& output, std::vector<std::string> const& values)
{
	output << '[';
	for (auto index = std::size_t{0}; index < values.size(); ++index)
	{
		if (index != 0U)
		{
			output << ", ";
		}
		append_json_string(output, values[index]);
	}
	output << ']';
}

void append_location(std::ostringstream& output, SourceLocation const& location)
{
	output << "{";
	output << "\"file\": ";
	append_json_string(output, location.file);
	output << ", \"line\": " << location.line;
	output << ", \"column\": " << location.column;
	output << "}";
}

void append_source_range(std::ostringstream& output, SourceRange const& range)
{
	output << "{";
	output << "\"begin\": ";
	append_location(output, range.begin);
	output << ", \"end\": ";
	append_location(output, range.end);
	output << "}";
}

void append_evidence(std::ostringstream& output, PlanEvidence const& evidence)
{
	output << "\"rule_id\": ";
	append_json_string(output, evidence.rule_id);
	output << ", \"reason\": ";
	append_json_string(output, evidence.reason);
	output << ", \"certainty\": ";
	append_json_string(output, evidence.certainty);
	output << ", \"conservative\": " << (evidence.conservative ? "true" : "false");
	output << ", \"source_range\": ";
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
	output << "Extraction result: " << plan.result << "\n\n";
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
			output << '\n';
		}
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
	std::ostringstream output;
	output << "{\n";
	output << "  \"schema_version\": " << plan.schema_version << ",\n";
	output << "  \"target\": {\n";
	output << "    \"qualified_name\": ";
	append_json_string(output, plan.target.qualified_name);
	output << ",\n    \"signature\": ";
	append_json_string(output, plan.target.signature);
	output << ",\n    \"source_file\": ";
	append_json_string(output, plan.target.source_file);
	output << ",\n    \"line\": " << plan.target.line << "\n";
	output << "  },\n";
	output << "  \"result\": ";
	append_json_string(output, plan.result);
	output << ",\n";

	output << "  \"receiver_state\": [\n";
	for (auto index = std::size_t{0}; index < plan.receiver_state.size(); ++index)
	{
		auto const& field = plan.receiver_state[index];
		output << "    {\"name\": ";
		append_json_string(output, field.name);
		output << ", \"type\": ";
		append_json_string(output, field.type);
		output << ", \"access\": ";
		append_json_string(output, to_string(field.access));
		output << ", \"mutable\": " << (field.is_mutable ? "true" : "false");
		output << ", \"access_specifier\": ";
		append_json_string(output, field.access_specifier);
		output << ", \"location\": ";
		append_location(output, field.location);
		output << ", ";
		append_evidence(output, field.evidence);
		output << "}";
		output << (index + 1U == plan.receiver_state.size() ? "\n" : ",\n");
	}
	output << "  ],\n";

	auto append_ports = [&output](std::string const& key, std::vector<DependencyPort> const& ports)
	{
		output << "  \"" << key << "\": [\n";
		for (auto index = std::size_t{0}; index < ports.size(); ++index)
		{
			auto const& port = ports[index];
			output << "    {\"kind\": ";
			append_json_string(output, to_string(port.kind));
			output << ", \"name\": ";
			append_json_string(output, port.name);
			output << ", \"original_callee\": ";
			append_json_string(output, port.original_callee);
			output << ", \"return_type\": ";
			append_json_string(output, port.return_type);
			output << ", \"argument_types\": ";
			append_string_array(output, port.argument_types);
			output << ", \"location\": ";
			append_location(output, port.location);
			output << ", ";
			append_evidence(output, port.evidence);
			output << "}";
			output << (index + 1U == ports.size() ? "\n" : ",\n");
		}
		output << "  ],\n";
	};

	append_ports("dependency_observations", ports_of_kind(plan, DependencyKind::kQuery));
	append_ports("observable_effects", ports_of_kind(plan, DependencyKind::kEffect));
	append_ports("operations", ports_of_kind(plan, DependencyKind::kOperation));

	output << "  \"recursive_helper_candidates\": [\n";
	auto recursive_ports = ports_of_kind(plan, DependencyKind::kRecursiveCandidate);
	for (auto index = std::size_t{0}; index < recursive_ports.size(); ++index)
	{
		auto const& port = recursive_ports[index];
		output << "    {\"name\": ";
		append_json_string(output, port.name);
		output << ", \"original_callee\": ";
		append_json_string(output, port.original_callee);
		output << ", \"return_type\": ";
		append_json_string(output, port.return_type);
		output << ", \"argument_types\": ";
		append_string_array(output, port.argument_types);
		output << ", ";
		append_evidence(output, port.evidence);
		output << "}";
		output << (index + 1U == recursive_ports.size() ? "\n" : ",\n");
	}
	output << "  ],\n";

	output << "  \"shape_candidates\": [\n";
	for (auto index = std::size_t{0}; index < plan.shape_candidates.size(); ++index)
	{
		auto const& shape = plan.shape_candidates[index];
		output << "    {\"name\": ";
		append_json_string(output, shape.name);
		output << ", \"source_dependency\": ";
		append_json_string(output, shape.source_dependency);
		output << ", \"observed_members\": ";
		append_string_array(output, shape.observed_members);
		output << ", ";
		append_evidence(output, shape.evidence);
		output << "}";
		output << (index + 1U == plan.shape_candidates.size() ? "\n" : ",\n");
	}
	output << "  ],\n";

	output << "  \"object_ref_requirements\": [\n";
	for (auto index = std::size_t{0}; index < plan.object_ref_requirements.size(); ++index)
	{
		auto const& requirement = plan.object_ref_requirements[index];
		output << "    {\"reason\": ";
		append_json_string(output, requirement.reason);
		output << ", \"expression\": ";
		append_json_string(output, requirement.expression);
		output << ", \"location\": ";
		append_location(output, requirement.location);
		output << ", ";
		append_evidence(output, requirement.evidence);
		output << "}";
		output << (index + 1U == plan.object_ref_requirements.size() ? "\n" : ",\n");
	}
	output << "  ],\n";

	output << "  \"paths\": [\n";
	for (auto index = std::size_t{0}; index < plan.paths.size(); ++index)
	{
		auto const& path = plan.paths[index];
		output << "    {\"name\": ";
		append_json_string(output, path.name);
		output << ", \"observations\": ";
		append_string_array(output, path.observations);
		output << ", \"effects\": ";
		append_string_array(output, path.effects);
		output << ", \"operations\": ";
		append_string_array(output, path.operations);
		output << ", ";
		append_evidence(output, path.evidence);
		output << "}";
		output << (index + 1U == plan.paths.size() ? "\n" : ",\n");
	}
	output << "  ],\n";

	output << R"(  "gtest_preview": {"sample_test_path": )";
	append_json_string(output, plan.gtest_preview.sample_test_path);
	output << ", \"lines\": ";
	append_string_array(output, plan.gtest_preview.lines);
	output << "},\n";

	output << "  \"diagnostics\": [\n";
	for (auto index = std::size_t{0}; index < plan.diagnostics.entries().size(); ++index)
	{
		auto const& diagnostic = plan.diagnostics.entries()[index];
		output << "    {\"severity\": ";
		append_json_string(output, to_string(diagnostic.severity));
		output << ", \"code\": ";
		append_json_string(output, diagnostic.code);
		output << ", \"message\": ";
		append_json_string(output, diagnostic.message);
		output << ", \"location\": ";
		append_location(output, diagnostic.location);
		output << "}";
		output << (index + 1U == plan.diagnostics.entries().size() ? "\n" : ",\n");
	}
	output << "  ]\n";
	output << "}\n";

	return output.str();
}

} // namespace azteca
