#include <gtest/gtest.h>

#include "collect/InspectCollector.hpp"

namespace
{

TEST(InspectCollector, ClassifiesMemberObjectCalls)
{
	auto query = azteca::inspect_collect::classify_member_object_call(false, true);
	EXPECT_EQ(query.kind, azteca::DependencyKind::kQuery);
	EXPECT_EQ(query.rule_id, "DEP-MEMBER-QUERY");

	auto operation = azteca::inspect_collect::classify_member_object_call(false, false);
	EXPECT_EQ(operation.kind, azteca::DependencyKind::kOperation);
	EXPECT_EQ(operation.rule_id, "DEP-MEMBER-OPERATION");

	auto effect = azteca::inspect_collect::classify_member_object_call(true, true);
	EXPECT_EQ(effect.kind, azteca::DependencyKind::kEffect);
	EXPECT_EQ(effect.rule_id, "DEP-MEMBER-EFFECT");
}

TEST(InspectCollector, ProducesStableOperatorPortNames)
{
	EXPECT_EQ(azteca::inspect_collect::operator_port_name("ns::Value::operator+="),
	    "op_ns_value_operator");
	EXPECT_EQ(azteca::inspect_collect::sanitize_identifier("Repo::load(int)"), "repo_load_int");
}

TEST(InspectCollector, BuildsPathBurdenFromEvents)
{
	auto evidence = azteca::PlanEvidence{
	    .rule_id = "PATH-CONSERVATIVE",
	    .reason = "loop path",
	    .certainty = "conservative",
	    .conservative = true,
	    .source_range = {},
	};
	auto burden = azteca::inspect_collect::build_path_burden("loop",
	    {
	        {.kind = azteca::DependencyKind::kQuery, .name = "repo_load"},
	        {.kind = azteca::DependencyKind::kQuery, .name = "repo_load"},
	        {.kind = azteca::DependencyKind::kEffect, .name = "bus_publish"},
	        {.kind = azteca::DependencyKind::kOperation, .name = "repo_save"},
	    },
	    evidence, {"repo_load", "repo_load"});

	EXPECT_EQ(burden.observations, std::vector<std::string>{"repo_load"});
	EXPECT_EQ(burden.effects, std::vector<std::string>{"bus_publish"});
	EXPECT_EQ(burden.operations, std::vector<std::string>{"repo_save"});
	EXPECT_EQ(burden.ordered_events,
	    (std::vector<azteca::OrderedPathEvent>{
	        {.kind = azteca::DependencyKind::kQuery, .name = "repo_load"},
	        {.kind = azteca::DependencyKind::kQuery, .name = "repo_load"},
	        {.kind = azteca::DependencyKind::kEffect, .name = "bus_publish"},
	        {.kind = azteca::DependencyKind::kOperation, .name = "repo_save"},
	    }));
	EXPECT_EQ(burden.loop_body_observations, std::vector<std::string>{"repo_load"});
	EXPECT_EQ(burden.required_envelopes,
	    (std::vector<std::string>{"dependency_boundary", "conservative_control_flow"}));
	EXPECT_EQ(burden.conservative_reason, "loop path");
}

} // namespace
