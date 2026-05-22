#pragma once

#include <string>

#include "azteca/ExtractionPlan.hpp"

namespace azteca
{

[[nodiscard]] std::string render_text_report(ExtractionPlan const& plan);
[[nodiscard]] std::string render_json_report(ExtractionPlan const& plan);

} // namespace azteca
