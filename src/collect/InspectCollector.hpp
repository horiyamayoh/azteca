#pragma once

#include <optional>
#include <string>
#include <vector>

#include "azteca/ExtractionPlan.hpp"

namespace azteca::inspect_collect
{

using PathEvent = OrderedPathEvent;

struct PathState
{
	std::vector<PathEvent> events;
	std::vector<std::string> labels;
	std::vector<std::string> loop_body_observations;
	bool conservative{false};
};

struct DependencyClassification
{
	DependencyKind kind{DependencyKind::kQuery};
	std::string rule_id;
	std::string reason;
};

[[nodiscard]] std::string remove_trailing_underscore(std::string value);
[[nodiscard]] std::string replace_colons(std::string value);
[[nodiscard]] std::string sanitize_identifier(std::string const& value);
[[nodiscard]] std::string operator_port_name(std::string const& original_symbol);

[[nodiscard]] DependencyKind classify_dependency_kind(
    bool returns_void_or_ignored, bool callee_is_const);
[[nodiscard]] DependencyClassification classify_member_object_call(
    bool returns_void_or_ignored, bool callee_is_const);
[[nodiscard]] DependencyClassification classify_static_member_call(bool returns_void_or_ignored);
[[nodiscard]] DependencyClassification classify_free_function_call(bool returns_void_or_ignored);

void record_shape_observation(ExtractionPlan& plan, std::string const& dependency_source,
    std::optional<std::string> const& local_type, std::string observed_member,
    PlanEvidence evidence);

[[nodiscard]] PathBurden build_path_burden(std::string name, std::vector<PathEvent> const& events,
    PlanEvidence evidence, std::vector<std::string> loop_body_observations = {});
void append_events(std::vector<PathEvent>& target, std::vector<PathEvent> const& source);
void append_events(std::vector<PathState>& states, std::vector<PathEvent> const& source);
void deduplicate_events(std::vector<PathEvent>& events);
void deduplicate_strings(std::vector<std::string>& values);

} // namespace azteca::inspect_collect
