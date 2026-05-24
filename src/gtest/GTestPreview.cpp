#include "GTestPreview.hpp"

#include "../collect/InspectCollector.hpp"

namespace azteca::gtest_preview
{

void build(ExtractionPlan& plan, clang::CXXMethodDecl const& method)
{
	using inspect_collect::replace_colons;
	using inspect_collect::sanitize_identifier;

	auto target_name = sanitize_identifier(replace_colons(plan.target.qualified_name));
	plan.gtest_preview.sample_test_path = "tests/" + target_name + ".sample_test.cpp";
	plan.gtest_preview.lines.push_back("auto s = azteca_gen::scenario::" + target_name + "{};");

	for (auto const& field : plan.receiver_state)
	{
		plan.gtest_preview.lines.push_back("s.self." + field.name + " = /* value */;");
	}

	for (auto const& port : plan.dependency_ports)
	{
		if (port.kind == DependencyKind::kQuery || port.kind == DependencyKind::kOperation)
		{
			plan.gtest_preview.lines.push_back(
			    "s.when." + port.name + "(/* args */).returns(/* value */);");
		}
	}

	if (method.getReturnType()->isVoidType())
	{
		plan.gtest_preview.lines.emplace_back("s.call(/* args */);");
	}
	else
	{
		plan.gtest_preview.lines.emplace_back("auto result = s.call(/* args */);");
		plan.gtest_preview.lines.emplace_back("EXPECT_EQ(result, /* expected */);");
	}

	for (auto const& port : plan.dependency_ports)
	{
		if (port.kind == DependencyKind::kEffect || port.kind == DependencyKind::kOperation)
		{
			plan.gtest_preview.lines.push_back(
			    "s.effects." + port.name + ".expect_once(/* args */);");
		}
	}

	if (plan.dependency_ports.empty())
	{
		plan.gtest_preview.lines.emplace_back("s.effects.expect_none();");
	}
}

} // namespace azteca::gtest_preview
