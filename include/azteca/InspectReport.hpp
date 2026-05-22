#pragma once

#include <string>

#include "azteca/ExtractionPlan.hpp"

namespace azteca
{

struct ReportOptions
{
	bool verbose{false};
};

[[nodiscard]] std::string render_text_report(ExtractionPlan const& plan);
[[nodiscard]] std::string render_text_report(
    ExtractionPlan const& plan, ReportOptions const& options);
[[nodiscard]] std::string render_json_report(ExtractionPlan const& plan);

} // namespace azteca
