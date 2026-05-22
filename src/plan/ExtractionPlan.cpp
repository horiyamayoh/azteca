#include "azteca/ExtractionPlan.hpp"

#include <algorithm>
#include <sstream>

namespace azteca
{

bool SourceLocation::is_valid() const noexcept
{
	return !file.empty() && line != 0U;
}

std::string SourceLocation::to_string() const
{
	if (!is_valid())
	{
		return "";
	}

	std::ostringstream output;
	output << file << ':' << line << ':' << column;
	return output.str();
}

void Diagnostics::add(DiagnosticSeverity severity, std::string code, std::string message)
{
	add(severity, std::move(code), std::move(message), {});
}

void Diagnostics::add(
    DiagnosticSeverity severity, std::string code, std::string message, SourceLocation location)
{
	entries_.push_back({
	    .severity = severity,
	    .code = std::move(code),
	    .message = std::move(message),
	    .location = std::move(location),
	});
}

bool Diagnostics::has_errors() const noexcept
{
	return std::ranges::any_of(entries_,
	    [](Diagnostic const& diagnostic)
	    {
		    return diagnostic.severity == DiagnosticSeverity::kError;
	    });
}

std::vector<Diagnostic> const& Diagnostics::entries() const noexcept
{
	return entries_;
}

std::string to_string(DiagnosticSeverity severity)
{
	switch (severity)
	{
		case DiagnosticSeverity::kInfo:
			return "info";
		case DiagnosticSeverity::kWarning:
			return "warning";
		case DiagnosticSeverity::kError:
			return "error";
	}

	return "info";
}

std::string to_string(FieldAccess access)
{
	switch (access)
	{
		case FieldAccess::kRead:
			return "read";
		case FieldAccess::kWrite:
			return "write";
		case FieldAccess::kReadWrite:
			return "read/write";
	}

	return "read";
}

std::string to_string(DependencyKind kind)
{
	switch (kind)
	{
		case DependencyKind::kRecursiveCandidate:
			return "recursive";
		case DependencyKind::kQuery:
			return "query";
		case DependencyKind::kEffect:
			return "effect";
		case DependencyKind::kOperation:
			return "operation";
	}

	return "query";
}

} // namespace azteca
