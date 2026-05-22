#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "azteca/SourceLocation.hpp"

namespace azteca
{

enum class DiagnosticSeverity : std::uint8_t
{
	kInfo,
	kWarning,
	kError,
};

struct Diagnostic
{
	DiagnosticSeverity severity{DiagnosticSeverity::kInfo};
	std::string code;
	std::string message;
	SourceLocation location;
};

class Diagnostics
{
   public:
	void add(DiagnosticSeverity severity, std::string code, std::string message);
	void add(DiagnosticSeverity severity, std::string code, std::string message,
	    SourceLocation location);

	[[nodiscard]] bool has_errors() const noexcept;
	[[nodiscard]] std::vector<Diagnostic> const& entries() const noexcept;

   private:
	std::vector<Diagnostic> entries_;
};

[[nodiscard]] std::string to_string(DiagnosticSeverity severity);

} // namespace azteca
