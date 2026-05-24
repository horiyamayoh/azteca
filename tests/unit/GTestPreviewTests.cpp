#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "gtest/GTestPreview.hpp"

namespace
{

azteca::DependencyPort dependency(
    azteca::DependencyKind kind, std::string name, std::string return_type)
{
	azteca::DependencyPort port;
	port.kind = kind;
	port.name = std::move(name);
	port.return_type = std::move(return_type);
	port.argument_types = {"int"};
	return port;
}

TEST(GTestPreview, BuildsNonVoidPathTestsWithReceiverPortsEffectsAndUniqueNames)
{
	azteca::ExtractionPlan plan;

	azteca::ReceiverField receiver;
	receiver.name = "enabled_";
	plan.receiver_state.push_back(receiver);

	plan.dependency_ports.push_back(
	    dependency(azteca::DependencyKind::kQuery, "repo_exists", "bool"));
	plan.dependency_ports.push_back(
	    dependency(azteca::DependencyKind::kOperation, "repo_refresh", "int"));
	plan.dependency_ports.push_back(
	    dependency(azteca::DependencyKind::kEffect, "notifier_send", "void"));
	plan.dependency_ports.push_back(
	    dependency(azteca::DependencyKind::kRecursiveCandidate, "normalize", "int"));

	azteca::PathBurden first_path;
	first_path.name = "success path";
	first_path.observations = {"repo_exists"};
	first_path.effects = {"notifier_send"};
	first_path.operations = {"repo_refresh"};
	plan.paths.push_back(first_path);

	azteca::PathBurden second_path;
	second_path.name = "success path";
	plan.paths.push_back(second_path);

	azteca::gtest_preview::build(plan, "Sample::run", false);

	EXPECT_EQ(plan.gtest_preview.sample_test_path, "tests/sample_run.sample_test.cpp");
	EXPECT_EQ(plan.gtest_preview.lines,
	    (std::vector<std::string>{
	        "TEST(sample_run, success_path)",
	        "{",
	        "  auto s = azteca_gen::scenario::sample_run{};",
	        "  s.self.enabled_ = /* value */;",
	        "  // recursive helper candidate: normalize(int) -> int",
	        "  s.when.repo_exists(/* args */).returns(/* value */);",
	        "  s.when.repo_refresh(/* args */).returns(/* value */);",
	        "  auto result = s.call(/* args */);",
	        "  EXPECT_EQ(result, /* expected for success path */);",
	        "  s.effects.notifier_send.expect_once(/* args */);",
	        "  s.effects.repo_refresh.expect_once(/* args */);",
	        "}",
	        "TEST(sample_run, success_path__2)",
	        "{",
	        "  auto s = azteca_gen::scenario::sample_run{};",
	        "  s.self.enabled_ = /* value */;",
	        "  // recursive helper candidate: normalize(int) -> int",
	        "  auto result = s.call(/* args */);",
	        "  EXPECT_EQ(result, /* expected for success path */);",
	        "  s.effects.expect_none();",
	        "}",
	    }));
}

TEST(GTestPreview, BuildsVoidCallWithoutResultAssertion)
{
	azteca::ExtractionPlan plan;
	azteca::PathBurden path;
	path.name = "notify";
	plan.paths.push_back(path);

	azteca::gtest_preview::build(plan, "Sample::notify", true);

	EXPECT_EQ(plan.gtest_preview.sample_test_path, "tests/sample_notify.sample_test.cpp");
	EXPECT_EQ(plan.gtest_preview.lines,
	    (std::vector<std::string>{
	        "TEST(sample_notify, notify)",
	        "{",
	        "  auto s = azteca_gen::scenario::sample_notify{};",
	        "  s.call(/* args */);",
	        "  s.effects.expect_none();",
	        "}",
	    }));
}

} // namespace
