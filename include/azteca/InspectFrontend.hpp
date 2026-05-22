#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "azteca/ExtractionPlan.hpp"
#include "azteca/MethodSpec.hpp"

namespace azteca
{

enum class InspectStatus : std::uint8_t
{
	kSuccess = 0,
	kUserInputError = 1,
	kCompileDatabaseError = 2,
	kMethodResolutionError = 3,
	kExtractionPlanningError = 4,
};

struct InspectOptions
{
	std::string build_dir;
	std::optional<std::string> source_file;
	MethodSpec method;
};

struct InspectResult
{
	InspectStatus status{InspectStatus::kSuccess};
	std::optional<ExtractionPlan> plan;
	Diagnostics diagnostics;
};

[[nodiscard]] InspectResult inspect_method(InspectOptions const& options);

} // namespace azteca
