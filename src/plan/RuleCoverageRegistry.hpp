#pragma once

#include <vector>

#include "azteca/ExtractionPlan.hpp"

namespace azteca::plan
{

[[nodiscard]] std::vector<RuleCoverage> phase_a_rule_coverage();

} // namespace azteca::plan
