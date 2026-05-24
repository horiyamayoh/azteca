if(NOT DEFINED AZTECA_EXECUTABLE)
	message(FATAL_ERROR "AZTECA_EXECUTABLE is required")
endif()

if(NOT DEFINED PROJECT_SOURCE_DIR)
	message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

if(NOT DEFINED PROJECT_BINARY_DIR)
	message(FATAL_ERROR "PROJECT_BINARY_DIR is required")
endif()

set(fixture_source "${PROJECT_SOURCE_DIR}/tests/fixtures/phase_a/simple")
set(fixture_build "${PROJECT_BINARY_DIR}/test-work/phase_a/simple")

execute_process(
	COMMAND
		${CMAKE_COMMAND}
		-S "${fixture_source}"
		-B "${fixture_build}"
		-G Ninja
		-DCMAKE_CXX_COMPILER=clang++-18
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	RESULT_VARIABLE configure_result
	OUTPUT_VARIABLE configure_output
	ERROR_VARIABLE configure_error
)

if(NOT configure_result EQUAL 0)
	message(FATAL_ERROR "fixture configure failed:\n${configure_output}\n${configure_error}")
endif()

function(assert_contains haystack needle context)
	string(FIND "${haystack}" "${needle}" found_index)
	if(found_index EQUAL -1)
		message(FATAL_ERROR "${context} did not contain '${needle}':\n${haystack}")
	endif()
endfunction()

function(assert_not_contains haystack needle context)
	string(FIND "${haystack}" "${needle}" found_index)
	if(NOT found_index EQUAL -1)
		message(FATAL_ERROR "${context} unexpectedly contained '${needle}':\n${haystack}")
	endif()
endfunction()

# ---------------------------------------------------------------------------
# Helper: run inspect --format json and return output; fail on non-zero exit.
# ---------------------------------------------------------------------------
macro(run_inspect_json source_file method result_var output_var error_var)
	execute_process(
		COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}"
				--source "${source_file}" --method "${method}" --format json
		RESULT_VARIABLE ${result_var}
		OUTPUT_VARIABLE ${output_var}
		ERROR_VARIABLE ${error_var}
	)
	if(NOT ${result_var} EQUAL 0)
		message(
			FATAL_ERROR
			"inspect failed for '${method}':\n${${output_var}}\n${${error_var}}"
		)
	endif()
endmacro()

# ---------------------------------------------------------------------------
# 1. Service::handle — paths count, envelope, gtest_preview content
# ---------------------------------------------------------------------------
run_inspect_json(
	"${fixture_source}/service.cpp"
	"Service::handle(Id)"
	service_result service_json service_err
)

# paths array must have exactly 4 entries (3 early-return branches + success).
string(JSON paths_length LENGTH "${service_json}" "paths")
if(NOT paths_length EQUAL 4)
	message(
		FATAL_ERROR
		"Service::handle paths count should be 4 but was ${paths_length}:\n${service_json}"
	)
endif()

# Each path name must be present.
foreach(expected_path "return_disabled" "return_not_found" "return_denied" "return_ok")
	assert_contains("${service_json}" "\"${expected_path}\"" "Service::handle paths")
endforeach()

# envelope_requirements must flag dependency-boundary because the method calls
# external dependency objects (Repo, Policy, Notifier).
string(JSON envelope_reqs GET "${service_json}" "envelope_requirements")
assert_contains("${envelope_reqs}" "dependency-boundary" "Service::handle envelope_requirements")

# gtest_preview lines must include the scenario constructor, self field setup,
# stub setup for both queries, the call, an EXPECT_EQ assertion, and effect
# expectation — these are the minimal building blocks for a useful generated
# test.
string(JSON gtest_preview_obj GET "${service_json}" "gtest_preview")
string(JSON gtest_lines GET "${gtest_preview_obj}" "lines")
foreach(expected_preview_fragment
		"azteca_gen::scenario::service_handle"
		"s.self.enabled_"
		"s.when.repo_exists"
		"s.when.policy_allow"
		"s.call("
		"EXPECT_EQ(result"
		"s.effects.notifier_send")
	assert_contains("${gtest_lines}" "${expected_preview_fragment}" "Service::handle gtest_preview lines")
endforeach()

# confidence must be high — all calls in this method are classifiable.
string(JSON confidence GET "${service_json}" "confidence")
if(NOT confidence STREQUAL "high")
	message(FATAL_ERROR "Service::handle confidence should be 'high' but was '${confidence}'")
endif()

# ---------------------------------------------------------------------------
# 2. Gauge::reading — single-path, self_state-only method
# ---------------------------------------------------------------------------
run_inspect_json(
	"${fixture_source}/simple_scenarios.cpp"
	"Gauge::reading() const"
	gauge_read_result gauge_read_json gauge_read_err
)

# A simple getter has exactly 1 path.
string(JSON paths_length LENGTH "${gauge_read_json}" "paths")
if(NOT paths_length EQUAL 1)
	message(
		FATAL_ERROR
		"Gauge::reading paths count should be 1 but was ${paths_length}:\n${gauge_read_json}"
	)
endif()

# The only path has no observations, effects, or operations.
string(JSON path_obs GET "${gauge_read_json}" "paths" "0" "observations")
string(JSON path_obs_len LENGTH "${gauge_read_json}" "paths" "0" "observations")
if(NOT path_obs_len EQUAL 0)
	message(FATAL_ERROR "Gauge::reading sole path should have no observations but got: ${path_obs}")
endif()

string(JSON path_eff_len LENGTH "${gauge_read_json}" "paths" "0" "effects")
if(NOT path_eff_len EQUAL 0)
	message(FATAL_ERROR "Gauge::reading sole path should have no effects")
endif()

# envelope_requirements should NOT include dependency-boundary.
string(JSON gauge_envelopes GET "${gauge_read_json}" "envelope_requirements")
assert_not_contains(
	"${gauge_envelopes}" "dependency-boundary" "Gauge::reading envelope_requirements"
)

# receiver_state must record a read on value_.
string(JSON rcv_state GET "${gauge_read_json}" "receiver_state")
assert_contains("${rcv_state}" "value_" "Gauge::reading receiver_state")
assert_contains("${rcv_state}" "\"read\"" "Gauge::reading receiver_state access")

# ---------------------------------------------------------------------------
# 3. Gauge::record — single-path, field-write method
# ---------------------------------------------------------------------------
run_inspect_json(
	"${fixture_source}/simple_scenarios.cpp"
	"Gauge::record(int)"
	gauge_write_result gauge_write_json gauge_write_err
)

# Also 1 path.
string(JSON paths_length LENGTH "${gauge_write_json}" "paths")
if(NOT paths_length EQUAL 1)
	message(
		FATAL_ERROR
		"Gauge::record paths count should be 1 but was ${paths_length}:\n${gauge_write_json}"
	)
endif()

# receiver_state must record a write on value_.
string(JSON rcv_state GET "${gauge_write_json}" "receiver_state")
assert_contains("${rcv_state}" "value_" "Gauge::record receiver_state")
assert_contains("${rcv_state}" "\"write\"" "Gauge::record receiver_state access")

# ---------------------------------------------------------------------------
# 4. Disambiguation — --source must resolve AmbiguousTarget::run to the
#    correct TU; inspection must succeed (exit 0).
# ---------------------------------------------------------------------------
execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}"
			--source "${fixture_source}/ambiguous_a.cpp"
			--method "AmbiguousTarget::run(int)" --format json
	RESULT_VARIABLE disambig_a_result
	OUTPUT_VARIABLE disambig_a_json
	ERROR_VARIABLE disambig_a_err
)

if(NOT disambig_a_result EQUAL 0)
	message(
		FATAL_ERROR
		"disambiguation with --source ambiguous_a.cpp should exit 0 "
		"but exited ${disambig_a_result}:\n${disambig_a_json}\n${disambig_a_err}"
	)
endif()

string(JSON parsed_schema ERROR_VARIABLE json_parse_error GET "${disambig_a_json}" "schema_version")
if(json_parse_error)
	message(
		FATAL_ERROR
		"disambiguation inspect produced invalid JSON:\n${json_parse_error}\n${disambig_a_json}"
	)
endif()
if(NOT parsed_schema STREQUAL "2")
	message(
		FATAL_ERROR
		"disambiguation inspect schema_version should be 2 but was ${parsed_schema}"
	)
endif()

# Without --source, the same method must remain ambiguous (exit 3).
execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}"
			--method "AmbiguousTarget::run(int)" --format json
	RESULT_VARIABLE disambig_no_src_result
	OUTPUT_VARIABLE disambig_no_src_out
	ERROR_VARIABLE disambig_no_src_err
)

if(NOT disambig_no_src_result EQUAL 3)
	message(
		FATAL_ERROR
		"ambiguous method without --source should exit 3 "
		"but exited ${disambig_no_src_result}:\n${disambig_no_src_err}"
	)
endif()
