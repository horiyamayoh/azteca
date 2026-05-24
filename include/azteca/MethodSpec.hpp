#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace azteca
{

enum class RefQualifier : std::uint8_t
{
	kNone,
	kLValue,
	kRValue,
};

enum class MethodSpecParseErrorKind : std::uint8_t
{
	kNone,
	kSyntax,
	kUnsupportedTarget,
};

struct MethodSpec
{
	std::string original;
	std::string qualified_class_name;
	std::string method_name;
	std::vector<std::string> template_arguments;
	std::vector<std::string> parameter_types;
	bool is_const{false};
	bool is_volatile{false};
	RefQualifier ref_qualifier{RefQualifier::kNone};
};

struct MethodSpecParseResult
{
	std::optional<MethodSpec> spec;
	std::string error;
	MethodSpecParseErrorKind error_kind{MethodSpecParseErrorKind::kNone};
};

struct TemplateArgumentParseResult
{
	std::optional<std::vector<std::string>> arguments;
	std::string error;
};

[[nodiscard]] MethodSpecParseResult parse_method_spec(std::string_view input);
[[nodiscard]] TemplateArgumentParseResult parse_template_arguments(std::string_view input);
[[nodiscard]] std::string to_string(RefQualifier qualifier);
[[nodiscard]] std::string normalize_type_for_match(std::string_view type);

} // namespace azteca
