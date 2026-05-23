#include "azteca/MethodSpec.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace azteca
{
namespace
{

[[nodiscard]] std::string trim(std::string_view value)
{
	auto first = std::size_t{0};
	auto last = value.size();

	while (first != last && std::isspace(static_cast<unsigned char>(value[first])) != 0)
	{
		++first;
	}

	while (first != last && std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0)
	{
		--last;
	}

	return std::string{value.substr(first, last - first)};
}

[[nodiscard]] bool consume_suffix(std::string& value, std::string_view suffix)
{
	if (value.size() < suffix.size())
	{
		return false;
	}

	auto offset = value.size() - suffix.size();
	if (value.compare(offset, suffix.size(), suffix) != 0)
	{
		return false;
	}

	value.erase(offset);
	value = trim(value);
	return true;
}

[[nodiscard]] std::vector<std::string> split_parameters(std::string_view value)
{
	std::vector<std::string> result;
	std::string current;
	int angle_depth = 0;
	int paren_depth = 0;

	for (char character : value)
	{
		if (character == '<')
		{
			++angle_depth;
		}
		else if (character == '>' && angle_depth > 0)
		{
			--angle_depth;
		}
		else if (character == '(')
		{
			++paren_depth;
		}
		else if (character == ')' && paren_depth > 0)
		{
			--paren_depth;
		}

		if (character == ',' && angle_depth == 0 && paren_depth == 0)
		{
			result.push_back(trim(current));
			current.clear();
			continue;
		}

		current.push_back(character);
	}

	auto tail = trim(current);
	if (!tail.empty())
	{
		result.push_back(tail);
	}

	return result;
}

[[nodiscard]] bool contains_template_marker(std::string_view value)
{
	return value.find('<') != std::string_view::npos || value.find('>') != std::string_view::npos;
}

[[nodiscard]] MethodSpecParseResult parse_error(std::string message)
{
	MethodSpecParseResult result;
	result.error = std::move(message);
	return result;
}

} // namespace

TemplateArgumentParseResult parse_template_arguments(std::string_view input)
{
	auto text = trim(input);
	if (text.empty())
	{
		return {
		    .arguments = std::nullopt,
		    .error = "template argument list is empty",
		};
	}

	auto arguments = split_parameters(text);
	if (arguments.empty())
	{
		return {
		    .arguments = std::nullopt,
		    .error = "template argument list is empty",
		};
	}

	return {
	    .arguments = std::move(arguments),
	    .error = {},
	};
}

MethodSpecParseResult parse_method_spec(std::string_view input)
{
	auto text = trim(input);
	if (text.empty())
	{
		return parse_error("method spec is empty");
	}

	auto open_paren = text.find('(');
	auto close_paren = text.rfind(')');
	if (open_paren == std::string::npos || close_paren == std::string::npos ||
	    close_paren < open_paren)
	{
		return parse_error("method spec must contain a parameter list");
	}

	auto prefix = trim(std::string_view(text).substr(0, open_paren));
	auto parameters_text =
	    std::string_view(text).substr(open_paren + 1, close_paren - open_paren - 1);
	auto suffix = trim(std::string_view(text).substr(close_paren + 1));

	auto separator = prefix.rfind("::");
	if (separator == std::string::npos || separator == 0 || separator + 2 >= prefix.size())
	{
		return parse_error("method spec must be qualified as Class::method");
	}

	MethodSpec spec;
	spec.original = text;
	spec.qualified_class_name = prefix.substr(0, separator);
	spec.method_name = prefix.substr(separator + 2);

	if (spec.method_name.empty())
	{
		return parse_error("method name is empty");
	}

	if (auto open_angle = spec.method_name.find('<'); open_angle != std::string::npos)
	{
		auto close_angle = spec.method_name.rfind('>');
		if (close_angle == std::string::npos || close_angle + 1U != spec.method_name.size() ||
		    close_angle < open_angle)
		{
			return parse_error("template method spec has malformed template arguments");
		}

		auto template_arguments =
		    parse_template_arguments(std::string_view(spec.method_name)
		                                 .substr(open_angle + 1U, close_angle - open_angle - 1U));
		if (!template_arguments.arguments.has_value())
		{
			return parse_error(template_arguments.error);
		}
		spec.template_arguments = std::move(*template_arguments.arguments);
		spec.method_name.erase(open_angle);
	}

	if (spec.method_name.starts_with("operator") && spec.method_name.size() > 8U)
	{
		auto const kNext = static_cast<unsigned char>(spec.method_name[8]);
		if (std::isalnum(kNext) == 0 && spec.method_name[8] != '_')
		{
			return parse_error("operator methods are not supported by Phase A inspect");
		}
	}

	if (contains_template_marker(spec.method_name))
	{
		return parse_error("template method spec has malformed template arguments");
	}

	while (!suffix.empty())
	{
		if (consume_suffix(suffix, "&&"))
		{
			if (spec.ref_qualifier != RefQualifier::kNone)
			{
				return parse_error("duplicate ref qualifier in method spec");
			}
			spec.ref_qualifier = RefQualifier::kRValue;
		}
		else if (consume_suffix(suffix, "&"))
		{
			if (spec.ref_qualifier != RefQualifier::kNone)
			{
				return parse_error("duplicate ref qualifier in method spec");
			}
			spec.ref_qualifier = RefQualifier::kLValue;
		}
		else if (consume_suffix(suffix, "const"))
		{
			if (spec.is_const)
			{
				return parse_error("duplicate const qualifier in method spec");
			}
			spec.is_const = true;
		}
		else if (consume_suffix(suffix, "volatile"))
		{
			if (spec.is_volatile)
			{
				return parse_error("duplicate volatile qualifier in method spec");
			}
			spec.is_volatile = true;
		}
		else
		{
			return parse_error("unsupported method qualifier in spec: " + suffix);
		}
	}

	if (trim(parameters_text).empty())
	{
		spec.parameter_types = {};
	}
	else
	{
		spec.parameter_types = split_parameters(parameters_text);
	}

	MethodSpecParseResult result;
	result.spec = std::move(spec);
	return result;
}

std::string to_string(RefQualifier qualifier)
{
	switch (qualifier)
	{
		case RefQualifier::kNone:
			return "none";
		case RefQualifier::kLValue:
			return "&";
		case RefQualifier::kRValue:
			return "&&";
	}

	return "none";
}

std::string normalize_type_for_match(std::string_view type)
{
	std::string normalized;
	normalized.reserve(type.size());

	for (char character : type)
	{
		if (std::isspace(static_cast<unsigned char>(character)) == 0)
		{
			normalized.push_back(character);
		}
	}

	auto remove_token = [&normalized](std::string_view token)
	{
		std::string result;
		auto position = std::string::size_type{0};
		while (position < normalized.size())
		{
			if (normalized.compare(position, token.size(), token) == 0)
			{
				position += token.size();
				continue;
			}
			result.push_back(normalized[position]);
			++position;
		}
		normalized = result;
	};

	remove_token("class");
	remove_token("struct");
	return normalized;
}

} // namespace azteca
