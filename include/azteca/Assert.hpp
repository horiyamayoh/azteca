#pragma once

#include <cstdio>
#include <cstdlib>
#include <string_view>

// AZTECA_ASSERT(condition, message)
//
// Phase A invariant check. Always-on (does not depend on NDEBUG). On failure,
// writes a stable AZT-E0011 stderr line including file:line and aborts.
// Use only for invariants that, if violated, indicate a tool bug — never for
// recoverable user-input errors (those must use the AZT-E0001..AZT-E0010 path).

namespace azteca::detail
{

[[noreturn]] inline void aztecaAssertFail(
    std::string_view expression, std::string_view message, char const* file, int line)
{
	std::fprintf(stderr, "AZT-E0011 invariant violated at %s:%d: (%.*s) %.*s\n", file, line,
	    static_cast<int>(expression.size()), expression.data(), static_cast<int>(message.size()),
	    message.data());
	std::abort();
}

} // namespace azteca::detail

#define AZTECA_ASSERT(condition, message) \
	do \
	{ \
		if (!(condition)) \
		{ \
			::azteca::detail::aztecaAssertFail(#condition, (message), __FILE__, __LINE__); \
		} \
	} while (false)
