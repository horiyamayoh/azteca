#pragma once

#include <string>

namespace azteca
{

struct SourceLocation
{
	std::string file;
	unsigned line{0};
	unsigned column{0};

	[[nodiscard]] bool is_valid() const noexcept;
	[[nodiscard]] std::string to_string() const;
};

} // namespace azteca
