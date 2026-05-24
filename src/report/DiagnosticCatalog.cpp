#include "azteca/DiagnosticCatalog.hpp"

#include <array>
#include <sstream>

namespace azteca
{
namespace
{

std::vector<DiagnosticInfo> const& catalog()
{
	static std::vector<DiagnosticInfo> const kEntries = {
	    {"AZT-E0001", "missing required option",
	        "A required command line option was not provided. See `azteca --help`."},
	    {"AZT-E0002", "unknown command",
	        "The subcommand is not recognized in Phase A. Supported: inspect, explain."},
	    {"AZT-E0003", "unknown diagnostic id",
	        "The id passed to `azteca explain` is not registered. Use the form AZT-Exxxx."},
	    {"AZT-E0004", "invalid --method spec",
	        "The --method value could not be parsed as a MethodSpec. See "
	        "docs/spec/phase_a_cli.md."},
	    {"AZT-E0005", "invalid --template-args",
	        "The --template-args value could not be parsed or did not match --method template "
	        "arguments."},
	    {"AZT-E0006", "invalid --format", "The --format value must be 'text' or 'json'."},
	    {"AZT-E0007", "compile database load failed",
	        "compile_commands.json was missing, unreadable, or did not contain a usable entry for "
	        "the --source file."},
	    {"AZT-E0008", "method not found",
	        "No CXXMethodDecl matched the --method spec. Check namespace, class, and signature."},
	    {"AZT-E0009", "method ambiguous",
	        "Multiple CXXMethodDecls matched the --method spec. Add --source or refine the "
	        "signature."},
	    {"AZT-E0010", "unsupported target",
	        "The target kind is not supported in Phase A (e.g. operator overload as the target "
	        "method, destructor, coroutine)."},
	    {"AZT-E0011", "plan validation failed",
	        "Internal MMIR/plan invariants were violated. This is a tool bug; please report with "
	        "the failing input."},
	    {"AZT-W0001", "conservative construct",
	        "A construct was handled conservatively. See `unsupported_or_modeled_constructs` and "
	        "`control_flow_summary.conservative_reasons` in the report."},
	};
	return kEntries;
}

} // namespace

std::vector<DiagnosticInfo> const& diagnostic_catalog()
{
	return catalog();
}

std::optional<DiagnosticInfo> find_diagnostic(std::string_view diagnostic_id)
{
	for (auto const& entry : catalog())
	{
		if (entry.id == diagnostic_id)
		{
			return entry;
		}
	}
	return std::nullopt;
}

std::string render_explain(DiagnosticInfo const& info)
{
	std::ostringstream output;
	output << info.id << ": " << info.summary << '\n' << '\n' << info.detail << '\n';
	return output.str();
}

std::optional<std::string_view> public_diagnostic_id(std::string_view internal_code)
{
	// Mapping from internal AZTECA_* codes (emitted by InspectFrontend / MMIR)
	// to public Phase A AZT-Exxxx ids. Keep this table append-only for schema_v2.
	struct Mapping
	{
		std::string_view internal;
		std::string_view public_id;
	};
	static constexpr std::array<Mapping, 15> kMap = {{
	    {"AZTECA_COMPILE_DB_MISSING", "AZT-E0007"},
	    {"AZTECA_COMPILE_DB_LOAD_FAILED", "AZT-E0007"},
	    {"AZTECA_COMPILE_DB_EMPTY", "AZT-E0007"},
	    {"AZTECA_CLANG_PARSE_FAILED", "AZT-E0007"},
	    {"AZTECA_METHOD_NOT_FOUND", "AZT-E0008"},
	    {"AZTECA_TEMPLATE_INSTANTIATION_NOT_FOUND", "AZT-E0008"},
	    {"AZTECA_METHOD_AMBIGUOUS", "AZT-E0009"},
	    {"AZTECA_UNSUPPORTED_TARGET", "AZT-E0010"},
	    {"AZTECA_STATIC_METHOD", "AZT-E0010"},
	    {"AZTECA_METHOD_DECL_ONLY", "AZT-E0010"},
	    {"AZTECA_MMIR_LOCATION_MISSING", "AZT-E0011"},
	    {"AZTECA_MMIR_FIELD_DECL_MISSING", "AZT-E0011"},
	    {"AZTECA_MMIR_BOUNDARY_CALLEE_MISSING", "AZT-E0011"},
	    {"AZTECA_MMIR_UNCLASSIFIED_CALL", "AZT-E0011"},
	    {"AZTECA_MMIR_BARE_THIS", "AZT-E0011"},
	}};
	for (auto const& entry : kMap)
	{
		if (entry.internal == internal_code)
		{
			return entry.public_id;
		}
	}
	return std::nullopt;
}

} // namespace azteca
