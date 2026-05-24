#include "GTestPreview.hpp"

#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "../collect/InspectCollector.hpp"

namespace azteca::gtest_preview
{
namespace
{

[[nodiscard]] std::string identifier_token(std::string const& value)
{
	using inspect_collect::sanitize_identifier;

	auto sanitized = sanitize_identifier(value);
	if (sanitized.empty())
	{
		return "path";
	}

	if (std::isdigit(static_cast<unsigned char>(sanitized.front())) != 0)
	{
		return "path_" + sanitized;
	}

	return sanitized;
}

[[nodiscard]] std::string dependency_signature(DependencyPort const& port)
{
	std::string signature = port.name + "(";
	for (auto index = std::size_t{0}; index < port.argument_types.size(); ++index)
	{
		if (index != 0U)
		{
			signature += ", ";
		}
		signature += port.argument_types[index];
	}
	signature += ")";
	if (!port.return_type.empty() && port.return_type != "void")
	{
		signature += " -> " + port.return_type;
	}
	return signature;
}

[[nodiscard]] std::optional<DependencyPort> find_port(
    ExtractionPlan const& plan, DependencyKind kind, std::string const& name)
{
	for (auto const& port : plan.dependency_ports)
	{
		if (port.kind == kind && port.name == name)
		{
			return port;
		}
	}

	return std::nullopt;
}

void append_receiver_setup(ExtractionPlan& plan)
{
	for (auto const& field : plan.receiver_state)
	{
		plan.gtest_preview.lines.push_back("  s.self." + field.name + " = /* value */;");
	}
}

void append_recursive_notes(ExtractionPlan& plan)
{
	for (auto const& port : plan.dependency_ports)
	{
		if (port.kind == DependencyKind::kRecursiveCandidate)
		{
			plan.gtest_preview.lines.push_back(
			    "  // recursive helper candidate: " + dependency_signature(port));
		}
	}
}

void append_path_observations(ExtractionPlan& plan, PathBurden const& path)
{
	for (auto const& observation : path.observations)
	{
		if (auto port = find_port(plan, DependencyKind::kQuery, observation); port.has_value())
		{
			plan.gtest_preview.lines.push_back(
			    "  s.when." + port->name + "(/* args */).returns(/* value */);");
		}
	}

	for (auto const& operation : path.operations)
	{
		if (auto port = find_port(plan, DependencyKind::kOperation, operation); port.has_value())
		{
			plan.gtest_preview.lines.push_back(
			    "  s.when." + port->name + "(/* args */).returns(/* value */);");
		}
	}
}

void append_call_and_result(ExtractionPlan& plan, PathBurden const& path, bool returns_void)
{
	if (returns_void)
	{
		plan.gtest_preview.lines.emplace_back("  s.call(/* args */);");
		return;
	}

	plan.gtest_preview.lines.emplace_back("  auto result = s.call(/* args */);");
	plan.gtest_preview.lines.push_back(
	    "  EXPECT_EQ(result, /* expected for " + path.name + " */);");
}

void append_path_effects(ExtractionPlan& plan, PathBurden const& path)
{
	auto emitted_effect = false;
	for (auto const& effect : path.effects)
	{
		if (auto port = find_port(plan, DependencyKind::kEffect, effect); port.has_value())
		{
			plan.gtest_preview.lines.push_back(
			    "  s.effects." + port->name + ".expect_once(/* args */);");
			emitted_effect = true;
		}
	}

	for (auto const& operation : path.operations)
	{
		if (auto port = find_port(plan, DependencyKind::kOperation, operation); port.has_value())
		{
			plan.gtest_preview.lines.push_back(
			    "  s.effects." + port->name + ".expect_once(/* args */);");
			emitted_effect = true;
		}
	}

	if (!emitted_effect)
	{
		plan.gtest_preview.lines.emplace_back("  s.effects.expect_none();");
	}
}

[[nodiscard]] std::string unique_test_name(
    PathBurden const& path, std::map<std::string, unsigned>& used_names)
{
	auto name = identifier_token(path.name);
	auto& use_count = used_names[name];
	++use_count;
	if (use_count == 1U)
	{
		return name;
	}

	return name + "__" + std::to_string(use_count);
}

void append_test_for_path(ExtractionPlan& plan, std::string const& target_name,
    PathBurden const& path, bool returns_void, std::map<std::string, unsigned>& used_names)
{
	plan.gtest_preview.lines.push_back(
	    "TEST(" + target_name + ", " + unique_test_name(path, used_names) + ")");
	plan.gtest_preview.lines.emplace_back("{");
	plan.gtest_preview.lines.push_back("  auto s = azteca_gen::scenario::" + target_name + "{};");
	append_receiver_setup(plan);
	append_recursive_notes(plan);
	append_path_observations(plan, path);
	append_call_and_result(plan, path, returns_void);
	append_path_effects(plan, path);
	plan.gtest_preview.lines.emplace_back("}");
}

} // namespace

void build(ExtractionPlan& plan, std::string_view qualified_name, bool returns_void)
{
	using inspect_collect::replace_colons;
	using inspect_collect::sanitize_identifier;

	auto target_name = sanitize_identifier(replace_colons(std::string{qualified_name}));
	plan.gtest_preview.sample_test_path = "tests/" + target_name + ".sample_test.cpp";
	plan.gtest_preview.lines.clear();

	if (plan.paths.empty())
	{
		PathBurden fallback_path;
		fallback_path.name = "path_1";
		std::map<std::string, unsigned> used_names;
		append_test_for_path(plan, target_name, fallback_path, returns_void, used_names);
		return;
	}

	std::map<std::string, unsigned> used_names;
	for (auto const& path : plan.paths)
	{
		append_test_for_path(plan, target_name, path, returns_void, used_names);
	}
}

void build(ExtractionPlan& plan, clang::CXXMethodDecl const& method)
{
	build(plan, plan.target.qualified_name, method.getReturnType()->isVoidType());
}

} // namespace azteca::gtest_preview
