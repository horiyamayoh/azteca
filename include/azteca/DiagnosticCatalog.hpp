#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace azteca
{

// Phase A diagnostic id registry. Public contract: ids and severities are
// frozen for schema_v2; the text body can be improved without a major bump.
//
// Format: AZT-Exxxx (errors) / AZT-Wxxxx (warnings).

struct DiagnosticInfo
{
	std::string_view id;
	std::string_view summary;
	std::string_view detail;
};

[[nodiscard]] std::vector<DiagnosticInfo> const& diagnostic_catalog();

[[nodiscard]] std::optional<DiagnosticInfo> find_diagnostic(std::string_view diagnostic_id);

[[nodiscard]] std::string render_explain(DiagnosticInfo const& info);

// Map an internal AZTECA_* diagnostic code (emitted by InspectFrontend / MMIR)
// to a public AZT-Exxxx / AZT-Wxxxx id. Returns std::nullopt if unmapped.
[[nodiscard]] std::optional<std::string_view> public_diagnostic_id(std::string_view internal_code);

} // namespace azteca
