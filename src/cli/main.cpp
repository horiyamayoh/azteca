#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "azteca/DiagnosticCatalog.hpp"
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
	std::cout
	    << "azteca - extract C++ non-static member function logic for Google Test (Phase A)\n"
	    << "\n"
	    << "Usage:\n"
	    << "  azteca --help\n"
	    << "  azteca inspect -p <build-dir> --method 'C::m(args...)'\n"
	    << "                 [--template-args 'T,...'] [--source <file>]\n"
	    << "                 [--format text|json] [--verbose|--quiet]\n"
	    << "  azteca explain <diagnostic-id>\n"
	    << "\n"
	    << "Phase A subcommands:\n"
	    << "  inspect   Display the Extraction Plan for the target method.\n"
	    << "  explain   Describe a diagnostic id (AZT-E* / AZT-W*).\n"
	    << "\n"
	    << "Common options:\n"
	    << "  -p, --build-dir <dir>     Directory containing compile_commands.json (required).\n"
	    << "  --method <spec>           Method spec, e.g. 'C::m(int)' (required for inspect).\n"
	    << "  --template-args <csv>     Override template arguments in --method.\n"
	    << "  --source <file>           Translation unit file for disambiguation.\n"
	    << "  --format <text|json>      Output format (default: text).\n"
	    << "  --verbose                 Emit per-element evidence (text only).\n"
	    << "  --quiet                   Suppress stdout; communicate via exit code.\n"
	    << "  -h, --help                Show this help and exit.\n"
	    << "\n"
	    << "Exit codes:\n"
	    << "  0  success\n"
	    << "  1  user input error\n"
	    << "  2  compile database error\n"
	    << "  3  method resolution error\n"
	    << "  4  extraction planning error\n"
	    << "\n"
	    << "Phase A contract: docs/spec/phase_a_cli.md\n";
}

void cli_error(std::string_view diagnostic_id, std::string_view message)
{
	std::cerr << diagnostic_id << ": " << message << '\n';
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

		// For `explain`, accept a single positional argument as the diagnostic id.
		if (options.command == "explain" && options.method_spec.empty() && !argument.empty() &&
		    argument.front() != '-')
		{
			options.method_spec = argument;
			continue;
		}

		if (argument == "-p" || argument == "--build-dir")
		{
			if (!has_value(index, argc))
			{
				cli_error("AZT-E0001", std::string{"missing value for "} + argument);
				return std::nullopt;
			}
			++index;
			options.build_dir = argv[index];
		}
		else if (argument == "--method")
		{
			if (!has_value(index, argc))
			{
				cli_error("AZT-E0001", "missing value for --method");
				return std::nullopt;
			}
			++index;
			options.method_spec = argv[index];
		}
		else if (argument == "--source")
		{
			if (!has_value(index, argc))
			{
				cli_error("AZT-E0001", "missing value for --source");
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
				cli_error("AZT-E0001", "missing value for --format");
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
				cli_error("AZT-E0006", std::string{"unsupported format: "} + format);
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
			cli_error("AZT-E0001", std::string{"unknown option: "} + argument);
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

	if (options->command == "explain")
	{
		if (options->method_spec.empty())
		{
			cli_error("AZT-E0001", "explain requires a diagnostic id argument");
			return static_cast<int>(azteca::InspectStatus::kUserInputError);
		}
		auto info = azteca::find_diagnostic(options->method_spec);
		if (!info.has_value())
		{
			cli_error("AZT-E0003", std::string{"unknown diagnostic id: "} + options->method_spec);
			return static_cast<int>(azteca::InspectStatus::kUserInputError);
		}
		std::cout << azteca::render_explain(*info);
		return 0;
	}

	if (options->command != "inspect")
	{
		cli_error("AZT-E0002", std::string{"unknown command: "} + options->command);
		return static_cast<int>(azteca::InspectStatus::kUserInputError);
	}

	if (options->build_dir.empty())
	{
		cli_error("AZT-E0001", "inspect requires -p/--build-dir");
		return static_cast<int>(azteca::InspectStatus::kUserInputError);
	}

	auto parse_result = azteca::parse_method_spec(options->method_spec);
	if (!parse_result.spec.has_value())
	{
		cli_error("AZT-E0004", std::string{"invalid --method: "} + parse_result.error);
		return static_cast<int>(azteca::InspectStatus::kUserInputError);
	}

	if (options->template_args.has_value())
	{
		auto template_args = azteca::parse_template_arguments(*options->template_args);
		if (!template_args.arguments.has_value())
		{
			cli_error("AZT-E0005", std::string{"invalid --template-args: "} + template_args.error);
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
			cli_error("AZT-E0005",
			    "invalid --template-args: values differ from --method template arguments");
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
			auto public_id = azteca::public_diagnostic_id(diagnostic.code);
			if (public_id.has_value())
			{
				std::cerr << *public_id << ' ';
			}
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
