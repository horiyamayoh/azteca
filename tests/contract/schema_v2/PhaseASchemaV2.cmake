if(NOT DEFINED AZTECA_EXECUTABLE)
	message(FATAL_ERROR "AZTECA_EXECUTABLE is required")
endif()

if(NOT DEFINED PROJECT_SOURCE_DIR)
	message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

if(NOT DEFINED PROJECT_BINARY_DIR)
	message(FATAL_ERROR "PROJECT_BINARY_DIR is required")
endif()

# Validate Phase A schema_v2 contract:
#   - all top-level required keys present
#   - schema_version == 2
#   - azteca_phase == "A"
#   - result enum
#   - confidence enum
#
# Validates against:
#   1. all tests/golden/phase_a/*.inspect.json files
#   2. live azteca inspect output for the canonical service fixture

set(required_keys
	schema_version
	azteca_phase
	target
	result
	confidence
	receiver_state
	dependency_observations
	observable_effects
	operations
	recursive_helper_candidates
	shape_candidates
	object_ref_requirements
	semantic_features
	unsupported_or_modeled_constructs
	control_flow_summary
	envelope_requirements
	rule_coverage
	paths
	gtest_preview
	diagnostics
)

set(result_enum extracted extracted-with-conservative-notes invalid-plan)
set(confidence_enum high medium low)

function(validate_phase_a_json json_text label)
	foreach(key IN LISTS required_keys)
		string(JSON _val ERROR_VARIABLE _err GET "${json_text}" ${key})
		if(_err)
			message(FATAL_ERROR "${label}: missing required key '${key}': ${_err}")
		endif()
	endforeach()

	string(JSON sv GET "${json_text}" schema_version)
	if(NOT sv STREQUAL "2")
		message(FATAL_ERROR "${label}: schema_version must be 2, got ${sv}")
	endif()

	string(JSON ph GET "${json_text}" azteca_phase)
	if(NOT ph STREQUAL "A")
		message(FATAL_ERROR "${label}: azteca_phase must be \"A\", got ${ph}")
	endif()

	string(JSON res GET "${json_text}" result)
	list(FIND result_enum "${res}" res_idx)
	if(res_idx EQUAL -1)
		message(FATAL_ERROR "${label}: result not in enum: ${res}")
	endif()

	string(JSON conf GET "${json_text}" confidence)
	list(FIND confidence_enum "${conf}" conf_idx)
	if(conf_idx EQUAL -1)
		message(FATAL_ERROR "${label}: confidence not in enum: ${conf}")
	endif()
endfunction()

# 1. Validate all golden JSON files.
file(GLOB golden_files "${PROJECT_SOURCE_DIR}/tests/golden/phase_a/*.inspect.json")
if(NOT golden_files)
	message(FATAL_ERROR "no Phase A JSON goldens found")
endif()

foreach(golden_path IN LISTS golden_files)
	file(READ "${golden_path}" golden_text)
	validate_phase_a_json("${golden_text}" "golden ${golden_path}")
endforeach()

# 2. Validate live output for the canonical service fixture.
set(fixture_source "${PROJECT_SOURCE_DIR}/tests/fixtures/phase_a/simple")
set(fixture_build "${PROJECT_BINARY_DIR}/test-work/phase_a/simple")

if(NOT EXISTS "${fixture_build}/compile_commands.json")
	execute_process(
		COMMAND
			${CMAKE_COMMAND}
			-S "${fixture_source}"
			-B "${fixture_build}"
			-G Ninja
			-DCMAKE_CXX_COMPILER=clang++-18
			-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
		RESULT_VARIABLE cfg_result
		OUTPUT_VARIABLE cfg_output
		ERROR_VARIABLE cfg_error
	)
	if(NOT cfg_result EQUAL 0)
		message(FATAL_ERROR "fixture configure failed:\n${cfg_output}\n${cfg_error}")
	endif()
endif()

execute_process(
	COMMAND
		"${AZTECA_EXECUTABLE}" inspect
		-p "${fixture_build}"
		--source "${fixture_source}/service.cpp"
		--method "Service::handle(Id)"
		--format json
	RESULT_VARIABLE live_result
	OUTPUT_VARIABLE live_output
	ERROR_VARIABLE live_error
)

if(NOT live_result EQUAL 0)
	message(FATAL_ERROR "live inspect failed:\n${live_output}\n${live_error}")
endif()

validate_phase_a_json("${live_output}" "live azteca inspect (Service::handle)")

message(STATUS "Phase A schema_v2 contract: OK (${CMAKE_MATCH_COUNT} files)")
