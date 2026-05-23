#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "azteca/InspectFrontend.hpp"
#include "azteca/InspectReport.hpp"
#include "azteca/MethodSpec.hpp"

namespace
{

enum class OutputFormat : std::uint8_t
{
	kText,
	kJson,
};

struct CliOptions
{
	bool show_help{false};
	bool quiet{false};
	bool verbose{false};
	std::string command;
	std::string build_dir;
	std::string method_spec;
	std::optional<std::string> template_args;
	std::optional<std::string> source_file;
	OutputFormat format{OutputFormat::kText};
};

void print_help()
{
	std::cout << "Usage:\n"
	          << "  azteca --help\n"
	          << "  azteca inspect -p <build-dir> --method 'C::m(args...)' "
	          << "[--template-args 'T,...'] [--source <file>] [--format text|json] "
	          << "[--verbose|--quiet]\n";
}

[[nodiscard]] bool has_value(int index, int argc)
{
	return index + 1 < argc;
}

[[nodiscard]] std::optional<CliOptions> parse_cli(int argc, char** argv)
{
	CliOptions options;

	if (argc <= 1)
	{
		options.show_help = true;
		return options;
	}

	for (int index = 1; index < argc; ++index)
	{
		std::string argument = argv[index];

		if (argument == "--help" || argument == "-h")
		{
			options.show_help = true;
			return options;
		}

		if (options.command.empty())
		{
			options.command = argument;
			continue;
		}

		if (argument == "-p" || argument == "--build-dir")
		{
			if (!has_value(index, argc))
			{
				std::cerr << "missing value for " << argument << '\n';
				return std::nullopt;
			}
			++index;
			options.build_dir = argv[index];
		}
		else if (argument == "--method")
		{
			if (!has_value(index, argc))
			{
				std::cerr << "missing value for --method\n";
				return std::nullopt;
			}
			++index;
			options.method_spec = argv[index];
		}
		else if (argument == "--source")
		{
			if (!has_value(index, argc))
			{
				std::cerr << "missing value for --source\n";
				return std::nullopt;
			}
			++index;
			options.source_file = std::string(argv[index]);
		}
		else if (argument == "--template-args")
		{
			if (!has_value(index, argc))
			{
				std::cerr << "missing value for --template-args\n";
				return std::nullopt;
			}
			++index;
			options.template_args = std::string(argv[index]);
		}
		else if (argument == "--format")
		{
			if (!has_value(index, argc))
			{
				std::cerr << "missing value for --format\n";
				return std::nullopt;
			}
			++index;
			std::string format = argv[index];
			if (format == "text")
			{
				options.format = OutputFormat::kText;
			}
			else if (format == "json")
			{
				options.format = OutputFormat::kJson;
			}
			else
			{
				std::cerr << "unsupported format: " << format << '\n';
				return std::nullopt;
			}
		}
		else if (argument == "--verbose")
		{
			options.verbose = true;
		}
		else if (argument == "--quiet")
		{
			options.quiet = true;
		}
		else
		{
			std::cerr << "unknown option: " << argument << '\n';
			return std::nullopt;
		}
	}

	return options;
}

} // namespace

int main(int argc, char** argv)
{
	auto options = parse_cli(argc, argv);
	if (!options.has_value())
	{
		return static_cast<int>(azteca::InspectStatus::kUserInputError);
	}

	if (options->show_help)
	{
		print_help();
		return 0;
	}

	if (options->command != "inspect")
	{
		std::cerr << "unsupported command: " << options->command << '\n';
		return static_cast<int>(azteca::InspectStatus::kUserInputError);
	}

	if (options->build_dir.empty())
	{
		std::cerr << "inspect requires -p/--build-dir\n";
		return static_cast<int>(azteca::InspectStatus::kUserInputError);
	}

	auto parse_result = azteca::parse_method_spec(options->method_spec);
	if (!parse_result.spec.has_value())
	{
		std::cerr << "invalid --method: " << parse_result.error << '\n';
		return static_cast<int>(azteca::InspectStatus::kUserInputError);
	}

	if (options->template_args.has_value())
	{
		auto template_args = azteca::parse_template_arguments(*options->template_args);
		if (!template_args.arguments.has_value())
		{
			std::cerr << "invalid --template-args: " << template_args.error << '\n';
			return static_cast<int>(azteca::InspectStatus::kUserInputError);
		}

		auto const kTemplateArgumentsMatch =
		    parse_result.spec->template_arguments.size() == template_args.arguments->size() &&
		    std::equal(parse_result.spec->template_arguments.begin(),
		        parse_result.spec->template_arguments.end(), template_args.arguments->begin(),
		        [](std::string const& lhs, std::string const& rhs)
		        {
			        return azteca::normalize_type_for_match(lhs) ==
			            azteca::normalize_type_for_match(rhs);
		        });
		if (!parse_result.spec->template_arguments.empty() && !kTemplateArgumentsMatch)
		{
			std::cerr
			    << "invalid --template-args: values differ from --method template arguments\n";
			return static_cast<int>(azteca::InspectStatus::kUserInputError);
		}
		parse_result.spec->template_arguments = std::move(*template_args.arguments);
	}

	azteca::InspectOptions inspect_options{
	    .build_dir = options->build_dir,
	    .source_file = options->source_file,
	    .method = *parse_result.spec,
	};

	auto result = azteca::inspect_method(inspect_options);
	if (!result.plan.has_value())
	{
		for (auto const& diagnostic : result.diagnostics.entries())
		{
			std::cerr << azteca::to_string(diagnostic.severity) << ' ' << diagnostic.code << ": "
			          << diagnostic.message << '\n';
		}
		return static_cast<int>(result.status);
	}

	if (!options->quiet)
	{
		if (options->format == OutputFormat::kJson)
		{
			std::cout << azteca::render_json_report(*result.plan);
		}
		else
		{
			std::cout << azteca::render_text_report(
			    *result.plan, azteca::ReportOptions{.verbose = options->verbose});
		}
	}

	return static_cast<int>(result.status);
}
