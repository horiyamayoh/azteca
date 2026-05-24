#include "InspectCollector.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string_view>
#include <utility>

namespace azteca::inspect_collect
{

std::string remove_trailing_underscore(std::string value)
{
	if (!value.empty() && value.back() == '_')
	{
		value.pop_back();
	}
	return value;
}

std::string replace_colons(std::string value)
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

std::string sanitize_identifier(std::string const& value)
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

std::string operator_port_name(std::string const& original_symbol)
{
	auto name = sanitize_identifier("op_" + replace_colons(original_symbol));
	return name.empty() ? "op_operator" : name;
}

DependencyKind classify_dependency_kind(bool returns_void_or_ignored, bool callee_is_const)
{
	if (returns_void_or_ignored)
	{
		return DependencyKind::kEffect;
	}
	if (!callee_is_const)
	{
		return DependencyKind::kOperation;
	}
	return DependencyKind::kQuery;
}

DependencyClassification classify_member_object_call(
    bool returns_void_or_ignored, bool callee_is_const)
{
	auto kind = classify_dependency_kind(returns_void_or_ignored, callee_is_const);
	switch (kind)
	{
		case DependencyKind::kQuery:
			return {
			    .kind = kind,
			    .rule_id = "DEP-MEMBER-QUERY",
			    .reason = "const non-void member object call is a query",
			};
		case DependencyKind::kEffect:
			return {
			    .kind = kind,
			    .rule_id = "DEP-MEMBER-EFFECT",
			    .reason = "void or ignored-return member object call is an effect",
			};
		case DependencyKind::kOperation:
			return {
			    .kind = kind,
			    .rule_id = "DEP-MEMBER-OPERATION",
			    .reason = "non-const non-void member object call is an operation",
			};
		case DependencyKind::kRecursiveCandidate:
			break;
	}

	return {
	    .kind = DependencyKind::kQuery,
	    .rule_id = "DEP-MEMBER-QUERY",
	    .reason = "const non-void member object call is a query",
	};
}

DependencyClassification classify_static_member_call(bool returns_void_or_ignored)
{
	return {
	    .kind = returns_void_or_ignored ? DependencyKind::kEffect : DependencyKind::kQuery,
	    .rule_id = "LR-008",
	    .reason = "static member function call is a boundary candidate",
	};
}

DependencyClassification classify_free_function_call(bool returns_void_or_ignored)
{
	if (returns_void_or_ignored)
	{
		return {
		    .kind = DependencyKind::kEffect,
		    .rule_id = "LR-009",
		    .reason = "void or ignored-return free function call is an effect boundary",
		};
	}

	return {
	    .kind = DependencyKind::kQuery,
	    .rule_id = "LR-009",
	    .reason = "non-void free function call is a query boundary",
	};
}

namespace
{

[[nodiscard]] std::string shape_name_from_type(std::string type)
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

	std::string sanitized;
	sanitized.reserve(type.size());
	for (char character : type)
	{
		if (std::isalnum(static_cast<unsigned char>(character)) != 0)
		{
			sanitized.push_back(character);
		}
		else if (!sanitized.empty() && sanitized.back() != '_')
		{
			sanitized.push_back('_');
		}
	}

	while (!sanitized.empty() && sanitized.back() == '_')
	{
		sanitized.pop_back();
	}

	type = std::move(sanitized);
	if (type.empty())
	{
		return "DependencyShape";
	}

	type[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(type[0])));
	return type + "Shape";
}

} // namespace

void record_shape_observation(ExtractionPlan& plan, std::string const& dependency_source,
    std::optional<std::string> const& local_type, std::string observed_member,
    PlanEvidence evidence)
{
	auto shape_name =
	    local_type.has_value() ? shape_name_from_type(*local_type) : dependency_source + "_shape";
	auto shape_iterator = std::ranges::find_if(plan.shape_candidates,
	    [&shape_name, &dependency_source](ShapeCandidate const& candidate)
	    {
		    return candidate.name == shape_name && candidate.source_dependency == dependency_source;
	    });

	if (shape_iterator == plan.shape_candidates.end())
	{
		plan.shape_candidates.push_back({
		    .name = std::move(shape_name),
		    .source_dependency = dependency_source,
		    .observed_members = {std::move(observed_member)},
		    .evidence = std::move(evidence),
		});
		return;
	}

	if (std::ranges::find(shape_iterator->observed_members, observed_member) ==
	    shape_iterator->observed_members.end())
	{
		shape_iterator->observed_members.push_back(std::move(observed_member));
	}
}

PathBurden build_path_burden(std::string name, std::vector<PathEvent> const& events,
    PlanEvidence evidence, std::vector<std::string> loop_body_observations)
{
	PathBurden burden;
	burden.name = std::move(name);
	std::ranges::copy_if(events, std::back_inserter(burden.ordered_events),
	    [](PathEvent const& event)
	    {
		    return event.kind == DependencyKind::kQuery || event.kind == DependencyKind::kEffect ||
		        event.kind == DependencyKind::kOperation;
	    });

	for (auto const& event : events)
	{
		switch (event.kind)
		{
			case DependencyKind::kQuery:
				burden.observations.push_back(event.name);
				burden.required_envelopes.emplace_back("dependency_boundary");
				break;
			case DependencyKind::kEffect:
				burden.effects.push_back(event.name);
				burden.required_envelopes.emplace_back("dependency_boundary");
				break;
			case DependencyKind::kOperation:
				burden.operations.push_back(event.name);
				burden.required_envelopes.emplace_back("dependency_boundary");
				break;
			case DependencyKind::kRecursiveCandidate:
				break;
		}
	}

	deduplicate_strings(burden.observations);
	deduplicate_strings(burden.effects);
	deduplicate_strings(burden.operations);
	deduplicate_strings(loop_body_observations);
	burden.loop_body_observations = std::move(loop_body_observations);
	deduplicate_strings(burden.required_envelopes);
	if (evidence.conservative)
	{
		burden.required_envelopes.emplace_back("conservative_control_flow");
		burden.conservative_reason = evidence.reason;
	}
	burden.evidence = std::move(evidence);
	return burden;
}

void append_events(std::vector<PathEvent>& target, std::vector<PathEvent> const& source)
{
	target.insert(target.end(), source.begin(), source.end());
}

void append_events(std::vector<PathState>& states, std::vector<PathEvent> const& source)
{
	for (auto& state : states)
	{
		append_events(state.events, source);
	}
}

void deduplicate_events(std::vector<PathEvent>& events)
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

void deduplicate_strings(std::vector<std::string>& values)
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

} // namespace azteca::inspect_collect
