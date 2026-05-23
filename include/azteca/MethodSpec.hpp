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

struct MethodSpec
{
	std::string original;
	std::string qualified_class_name;
	std::string method_name;
	std::vector<std::string> parameter_types;
	bool is_const{false};
	bool is_volatile{false};
	RefQualifier ref_qualifier{RefQualifier::kNone};
};

struct MethodSpecParseResult
{
	std::optional<MethodSpec> spec;
	std::string error;
};

[[nodiscard]] MethodSpecParseResult parse_method_spec(std::string_view input);
[[nodiscard]] std::string to_string(RefQualifier qualifier);
[[nodiscard]] std::string normalize_type_for_match(std::string_view type);

} // namespace azteca
