#include <gtest/gtest.h>

#include "azteca/InspectReport.hpp"

namespace
{

TEST(InspectReport, JsonContainsStablePhaseASchemaKeys)
{
	azteca::ExtractionPlan plan;
	plan.target.qualified_name = "Service::handle";
	plan.target.signature = "Result(Id)";
	auto evidence = azteca::PlanEvidence{
	    .rule_id = "LR-001",
	    .reason = "receiver field read",
	    .certainty = "certain",
	    .conservative = false,
	    .source_range = {},
	};
	plan.receiver_state.push_back({
	    .name = "enabled_",
	    .type = "bool",
	    .access = azteca::FieldAccess::kRead,
	    .is_mutable = false,
	    .access_specifier = "private",
	    .location = {},
	    .evidence = evidence,
	});
	plan.dependency_ports.push_back({
	    .kind = azteca::DependencyKind::kQuery,
	    .name = "repo_exists",
	    .original_callee = "Repo::exists",
	    .return_type = "bool",
	    .argument_types = {"Id"},
	    .location = {},
	    .evidence = evidence,
	});
	plan.paths.push_back({
	    .name = "return_ok",
	    .observations = {"repo_exists"},
	    .effects = {},
	    .operations = {},
	    .loop_body_observations = {},
	    .required_envelopes = {"dependency_boundary"},
	    .conservative_reason = {},
	    .evidence = evidence,
	});
	plan.gtest_preview.sample_test_path = "tests/service_handle.sample_test.cpp";
	plan.gtest_preview.lines = {"auto s = azteca_gen::scenario::service_handle{};"};

	auto json = azteca::render_json_report(plan);

	EXPECT_NE(json.find("\"schema_version\": 2"), std::string::npos);
	EXPECT_NE(json.find("\"target\""), std::string::npos);
	EXPECT_NE(json.find("\"confidence\""), std::string::npos);
	EXPECT_NE(json.find("\"receiver_state\""), std::string::npos);
	EXPECT_NE(json.find("\"dependency_observations\""), std::string::npos);
	EXPECT_NE(json.find("\"observable_effects\""), std::string::npos);
	EXPECT_NE(json.find("\"operations\""), std::string::npos);
	EXPECT_NE(json.find("\"loop_body_observations\""), std::string::npos);
	EXPECT_NE(json.find("\"shape_candidates\""), std::string::npos);
	EXPECT_NE(json.find("\"object_ref_requirements\""), std::string::npos);
	EXPECT_NE(json.find("\"semantic_features\""), std::string::npos);
	EXPECT_NE(json.find("\"unsupported_or_modeled_constructs\""), std::string::npos);
	EXPECT_NE(json.find("\"control_flow_summary\""), std::string::npos);
	EXPECT_NE(json.find("\"envelope_requirements\""), std::string::npos);
	EXPECT_NE(json.find("\"rule_coverage\""), std::string::npos);
	EXPECT_NE(json.find("\"paths\""), std::string::npos);
	EXPECT_NE(json.find("\"gtest_preview\""), std::string::npos);
	EXPECT_NE(json.find("\"diagnostics\""), std::string::npos);
	EXPECT_NE(json.find("\"rule_id\""), std::string::npos);
	EXPECT_NE(json.find("\"source_range\""), std::string::npos);
}

TEST(InspectReport, JsonUsesCanonicalKebabCaseValuesWithoutChangingText)
{
	azteca::ExtractionPlan plan;
	plan.target.qualified_name = "Account::withdraw";
	auto evidence = azteca::PlanEvidence{
	    .rule_id = "LR-JSON",
	    .reason = "constructed report coverage",
	    .certainty = "certain",
	    .conservative = false,
	    .source_range = {},
	};
	plan.receiver_state.push_back({
	    .name = "balance_",
	    .type = "int",
	    .access = azteca::FieldAccess::kReadWrite,
	    .is_mutable = false,
	    .access_specifier = "private",
	    .location = {},
	    .evidence = evidence,
	});
	plan.dependency_ports.push_back({
	    .kind = azteca::DependencyKind::kRecursiveCandidate,
	    .name = "normalize",
	    .original_callee = "Account::normalize",
	    .return_type = "int",
	    .argument_types = {"int"},
	    .location = {},
	    .evidence = evidence,
	});
	plan.semantic_features.push_back({
	    .name = "coroutine",
	    .handling = azteca::ConstructHandling::kNotYetImplemented,
	    .detail = "not handled in Phase A",
	    .evidence = evidence,
	});
	plan.unsupported_or_modeled_constructs.push_back({
	    .construct = "inline asm",
	    .handling = azteca::ConstructHandling::kNotMeaningful,
	    .reason = "cannot preserve unit semantics",
	    .fallbacks = {},
	    .location = {},
	    .evidence = evidence,
	});
	plan.envelope_requirements.push_back({
	    .kind = azteca::EnvelopeRequirementKind::kDependencyBoundary,
	    .reason = "dependency call",
	    .source = "repo",
	    .evidence = evidence,
	});
	plan.envelope_requirements.push_back({
	    .kind = azteca::EnvelopeRequirementKind::kObjectRef,
	    .reason = "object identity",
	    .source = "this",
	    .evidence = evidence,
	});
	plan.rule_coverage.push_back({
	    .rule_id = "LR-999",
	    .handling = azteca::ConstructHandling::kNotYetImplemented,
	    .note = "future rule",
	    .observed = false,
	});
	plan.paths.push_back({
	    .name = "path_1",
	    .observations = {},
	    .effects = {},
	    .operations = {},
	    .loop_body_observations = {},
	    .required_envelopes = {"dependency_boundary", "object_ref"},
	    .conservative_reason = {},
	    .evidence = evidence,
	});

	auto json = azteca::render_json_report(plan);

	EXPECT_NE(json.find("\"access\": \"read-write\""), std::string::npos);
	EXPECT_NE(json.find("\"kind\": \"recursive-candidate\""), std::string::npos);
	EXPECT_NE(json.find("\"handling\": \"not-yet-implemented\""), std::string::npos);
	EXPECT_NE(json.find("\"handling\": \"not-meaningful\""), std::string::npos);
	EXPECT_NE(json.find("\"kind\": \"dependency-boundary\""), std::string::npos);
	EXPECT_NE(json.find("\"kind\": \"object-ref\""), std::string::npos);
	EXPECT_NE(json.find("\"required_envelopes\": [\"dependency-boundary\", \"object-ref\"]"),
	    std::string::npos);

	auto text = azteca::render_text_report(plan);

	EXPECT_NE(text.find("int balance_ read/write"), std::string::npos);
	EXPECT_NE(text.find("recursive normalize(int) -> int"), std::string::npos);
	EXPECT_NE(text.find("[not_yet_implemented]"), std::string::npos);
	EXPECT_NE(text.find("[not_meaningful_for_unit_extraction]"), std::string::npos);
	EXPECT_NE(text.find("dependency_boundary"), std::string::npos);
	EXPECT_NE(text.find("object_ref"), std::string::npos);
}

TEST(InspectReport, TextShowsCoreInspectSections)
{
	azteca::ExtractionPlan plan;
	plan.target.qualified_name = "Account::withdraw";
	plan.result = "extracted";
	plan.receiver_state.push_back({
	    .name = "balance_",
	    .type = "int",
	    .access = azteca::FieldAccess::kReadWrite,
	    .is_mutable = false,
	    .access_specifier = "private",
	    .location = {},
	    .evidence = {},
	});
	plan.gtest_preview.sample_test_path = "tests/account_withdraw.sample_test.cpp";
	plan.gtest_preview.lines = {"auto result = s.call(/* args */);"};
	plan.paths.push_back({
	    .name = "path_1",
	    .observations = {},
	    .effects = {},
	    .operations = {},
	    .loop_body_observations = {},
	    .required_envelopes = {},
	    .conservative_reason = {},
	    .evidence = {},
	});

	auto text = azteca::render_text_report(plan);

	EXPECT_NE(text.find("Receiver state:"), std::string::npos);
	EXPECT_NE(text.find("Dependency observations:"), std::string::npos);
	EXPECT_NE(text.find("Semantic features:"), std::string::npos);
	EXPECT_NE(text.find("Envelope requirements:"), std::string::npos);
	EXPECT_NE(text.find("Control flow summary:"), std::string::npos);
	EXPECT_NE(text.find("Rule coverage:"), std::string::npos);
	EXPECT_NE(text.find("Path-wise test burden:"), std::string::npos);
	EXPECT_NE(text.find("loop body observations:"), std::string::npos);
	EXPECT_NE(text.find("Google Test preview:"), std::string::npos);
}

TEST(MmirValidation, RejectsUnclassifiedCall)
{
	azteca::MmirFunction function;
	function.target_name = "C::m";
	function.nodes.push_back({
	    .kind = azteca::MmirNodeKind::kCall,
	    .label = "unknown",
	    .location = {.file = "fixture.cpp", .line = 1, .column = 1},
	    .source_range =
	        {
	            .begin = {.file = "fixture.cpp", .line = 1, .column = 1},
	            .end = {.file = "fixture.cpp", .line = 1, .column = 8},
	        },
	    .semantic_id = {},
	    .original_symbol = {},
	    .rule_id = "CALL-UNCLASSIFIED",
	    .reason = "call was not classified",
	    .conservative = false,
	});

	azteca::Diagnostics diagnostics;
	EXPECT_FALSE(azteca::validate_mmir(function, diagnostics));
	ASSERT_FALSE(diagnostics.entries().empty());
	EXPECT_EQ(diagnostics.entries().front().code, "AZTECA_MMIR_UNCLASSIFIED_CALL");
}

TEST(MmirValidation, AcceptsClassifiedBoundaryCall)
{
	azteca::MmirFunction function;
	function.target_name = "C::m";
	function.nodes.push_back({
	    .kind = azteca::MmirNodeKind::kBoundaryCall,
	    .label = "repo_exists",
	    .location = {.file = "fixture.cpp", .line = 2, .column = 3},
	    .source_range =
	        {
	            .begin = {.file = "fixture.cpp", .line = 2, .column = 3},
	            .end = {.file = "fixture.cpp", .line = 2, .column = 19},
	        },
	    .semantic_id = "repo_exists",
	    .original_symbol = "Repo::exists",
	    .rule_id = "DEP-MEMBER-QUERY",
	    .reason = "const non-void member object call is a query",
	    .conservative = false,
	});

	azteca::Diagnostics diagnostics;
	EXPECT_TRUE(azteca::validate_mmir(function, diagnostics));
	EXPECT_TRUE(diagnostics.entries().empty());
}

} // namespace
