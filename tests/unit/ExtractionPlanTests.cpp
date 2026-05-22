#include <gtest/gtest.h>

#include "azteca/InspectReport.hpp"

namespace
{

TEST(InspectReport, JsonContainsStablePhaseASchemaKeys)
{
	azteca::ExtractionPlan plan;
	plan.target.qualified_name = "Service::handle";
	plan.target.signature = "Result(Id)";
	plan.receiver_state.push_back({
	    .name = "enabled_",
	    .type = "bool",
	    .access = azteca::FieldAccess::kRead,
	    .is_mutable = false,
	    .access_specifier = "private",
	    .location = {},
	});
	plan.dependency_ports.push_back({
	    .kind = azteca::DependencyKind::kQuery,
	    .name = "repo_exists",
	    .original_callee = "Repo::exists",
	    .return_type = "bool",
	    .argument_types = {"Id"},
	    .location = {},
	});
	plan.paths.push_back({
	    .name = "return_ok",
	    .observations = {"repo_exists"},
	    .effects = {},
	    .operations = {},
	});
	plan.gtest_preview.sample_test_path = "tests/service_handle.sample_test.cpp";
	plan.gtest_preview.lines = {"auto s = azteca_gen::scenario::service_handle{};"};

	auto json = azteca::render_json_report(plan);

	EXPECT_NE(json.find("\"schema_version\": 1"), std::string::npos);
	EXPECT_NE(json.find("\"target\""), std::string::npos);
	EXPECT_NE(json.find("\"receiver_state\""), std::string::npos);
	EXPECT_NE(json.find("\"dependency_observations\""), std::string::npos);
	EXPECT_NE(json.find("\"observable_effects\""), std::string::npos);
	EXPECT_NE(json.find("\"operations\""), std::string::npos);
	EXPECT_NE(json.find("\"shape_candidates\""), std::string::npos);
	EXPECT_NE(json.find("\"object_ref_requirements\""), std::string::npos);
	EXPECT_NE(json.find("\"paths\""), std::string::npos);
	EXPECT_NE(json.find("\"gtest_preview\""), std::string::npos);
	EXPECT_NE(json.find("\"diagnostics\""), std::string::npos);
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
	});
	plan.gtest_preview.sample_test_path = "tests/account_withdraw.sample_test.cpp";
	plan.gtest_preview.lines = {"auto result = s.call(/* args */);"};

	auto text = azteca::render_text_report(plan);

	EXPECT_NE(text.find("Receiver state:"), std::string::npos);
	EXPECT_NE(text.find("Dependency observations:"), std::string::npos);
	EXPECT_NE(text.find("Path-wise test burden:"), std::string::npos);
	EXPECT_NE(text.find("Google Test preview:"), std::string::npos);
}

} // namespace
