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

bool SourceRange::is_valid() const noexcept
{
	return begin.is_valid();
}

std::string SourceRange::to_string() const
{
	if (!is_valid())
	{
		return "";
	}

	if (!end.is_valid())
	{
		return begin.to_string();
	}

	std::ostringstream output;
	output << begin.to_string() << '-' << end.line << ':' << end.column;
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
		case FieldAccess::kAddress:
			return "address";
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

std::string to_string(ConstructHandling handling)
{
	switch (handling)
	{
		case ConstructHandling::kSupported:
			return "supported";
		case ConstructHandling::kModeled:
			return "modeled";
		case ConstructHandling::kBoundary:
			return "boundary";
		case ConstructHandling::kConservative:
			return "conservative";
		case ConstructHandling::kNotYetImplemented:
			return "not_yet_implemented";
		case ConstructHandling::kNotMeaningful:
			return "not_meaningful_for_unit_extraction";
	}

	return "not_yet_implemented";
}

std::string to_string(EnvelopeRequirementKind kind)
{
	switch (kind)
	{
		case EnvelopeRequirementKind::kSelfState:
			return "self_state";
		case EnvelopeRequirementKind::kBaseState:
			return "base_state";
		case EnvelopeRequirementKind::kAddressableCell:
			return "addressable_cell";
		case EnvelopeRequirementKind::kObjectRef:
			return "object_ref";
		case EnvelopeRequirementKind::kDependencyBoundary:
			return "dependency_boundary";
		case EnvelopeRequirementKind::kDispatchTable:
			return "dispatch_table";
		case EnvelopeRequirementKind::kTypeTag:
			return "type_tag";
		case EnvelopeRequirementKind::kLifetimeState:
			return "lifetime_state";
		case EnvelopeRequirementKind::kByteView:
			return "byte_view";
		case EnvelopeRequirementKind::kGlobalEnvironment:
			return "global_environment";
		case EnvelopeRequirementKind::kExceptionModel:
			return "exception_model";
		case EnvelopeRequirementKind::kMacroSourceMap:
			return "macro_source_map";
	}

	return "self_state";
}

} // namespace azteca
