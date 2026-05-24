if(NOT DEFINED AZTECA_EXECUTABLE)
	message(FATAL_ERROR "AZTECA_EXECUTABLE is required")
endif()

if(NOT DEFINED PROJECT_SOURCE_DIR)
	message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

if(NOT DEFINED PROJECT_BINARY_DIR)
	message(FATAL_ERROR "PROJECT_BINARY_DIR is required")
endif()

# Coverage observation gap-fillers for Phase A inspect.
#
# docs/planning/25_phase_a_inspect_coverage.md lists ~48 LR-* rules that
# Phase A inspect must surface. This module drives existing fixtures with
# additional inspect invocations whose only purpose is to make sure each
# coverage matrix row is *observed* in at least one stable golden, so that
# regressions on individual rules show up as golden diffs rather than
# silent loss of coverage.
#
# Goldens emitted by this module are stored alongside the primary Phase A
# goldens in tests/golden/phase_a/coverage_observations/.

set(fixture_source "${PROJECT_SOURCE_DIR}/tests/fixtures/phase_a/simple")
set(fixture_build "${PROJECT_BINARY_DIR}/test-work/phase_a/coverage_observations")
set(golden_root "${PROJECT_SOURCE_DIR}/tests/golden/phase_a/coverage_observations")

if(DEFINED CMAKE_CXX_COMPILER AND NOT CMAKE_CXX_COMPILER STREQUAL "")
	set(fixture_cxx_compiler "${CMAKE_CXX_COMPILER}")
else()
	set(fixture_cxx_compiler "clang++-18")
endif()

execute_process(
	COMMAND
		${CMAKE_COMMAND}
		-S "${fixture_source}"
		-B "${fixture_build}"
		-G Ninja
		-DCMAKE_CXX_COMPILER=${fixture_cxx_compiler}
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	RESULT_VARIABLE _cfg_result
	OUTPUT_VARIABLE _cfg_out
	ERROR_VARIABLE _cfg_err
)
if(NOT _cfg_result EQUAL 0)
	message(FATAL_ERROR "coverage observations fixture configure failed:\n${_cfg_out}\n${_cfg_err}")
endif()

# Run inspect, freeze a golden, and assert each requested LR-* id is
# observed=true in the JSON rule_coverage. Goldens are byte-compared after
# replacing the absolute fixture path with the <fixture> placeholder so the
# repo can host them.
function(observe source method golden_name)
	set(required_rules ${ARGN})

	execute_process(
		COMMAND
			"${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}"
			--source "${source}" --method "${method}" --format json
		RESULT_VARIABLE _rc
		OUTPUT_VARIABLE _out
		ERROR_VARIABLE _err
	)
	if(NOT _rc EQUAL 0)
		message(FATAL_ERROR "inspect failed for ${method}:\n${_out}\n${_err}")
	endif()

	string(JSON _schema ERROR_VARIABLE _parse_err GET "${_out}" schema_version)
	if(_parse_err)
		message(FATAL_ERROR "inspect output was not valid JSON for ${method}:\n${_parse_err}\n${_out}")
	endif()
	if(NOT _schema STREQUAL "2")
		message(FATAL_ERROR "inspect schema_version should be 2 for ${method} but was ${_schema}")
	endif()

	string(JSON _phase GET "${_out}" azteca_phase)
	if(NOT _phase STREQUAL "A")
		message(FATAL_ERROR "inspect azteca_phase should be 'A' for ${method} but was ${_phase}")
	endif()

	string(JSON _coverage_len ERROR_VARIABLE _len_err LENGTH "${_out}" rule_coverage)
	if(_len_err OR _coverage_len LESS 1)
		message(FATAL_ERROR "inspect rule_coverage missing for ${method}:\n${_out}")
	endif()

	foreach(_rule IN LISTS required_rules)
		set(_found FALSE)
		math(EXPR _last "${_coverage_len} - 1")
		foreach(_index RANGE 0 ${_last})
			string(JSON _entry GET "${_out}" rule_coverage ${_index})
			string(JSON _id GET "${_entry}" rule_id)
			string(JSON _observed GET "${_entry}" observed)
			# CMake string(JSON ... GET ...) returns ON/OFF for booleans.
			if(_id STREQUAL "${_rule}" AND _observed)
				set(_found TRUE)
				break()
			endif()
		endforeach()
		if(NOT _found)
			message(FATAL_ERROR
				"coverage assertion failed for ${method}: expected ${_rule} observed=true\n${_out}")
		endif()
	endforeach()

	string(REPLACE "${fixture_source}" "<fixture>" _normalized "${_out}")
	set(_golden_path "${golden_root}/${golden_name}")
	if(DEFINED ENV{AZTECA_ACCEPT_GOLDEN} AND NOT "$ENV{AZTECA_ACCEPT_GOLDEN}" STREQUAL "")
		file(WRITE "${_golden_path}" "${_normalized}")
	else()
		if(NOT EXISTS "${_golden_path}")
			message(FATAL_ERROR "missing golden for ${method}: ${_golden_path}")
		endif()
		file(READ "${_golden_path}" _expected)
		string(REGEX REPLACE "[ \t\r\n]" "" _normalized_compact "${_normalized}")
		string(REGEX REPLACE "[ \t\r\n]" "" _expected_compact "${_expected}")
		if(NOT _normalized_compact STREQUAL _expected_compact)
			message(FATAL_ERROR
				"golden mismatch for ${method} (${golden_name}):\n${_normalized}")
		endif()
	endif()
endfunction()

# --- EdgeCases (edge_cases.cpp) ---
observe(
	"${fixture_source}/edge_cases.cpp" "EdgeCases::explicit_this_read() const"
	"edge_cases_explicit_this_read.inspect.json"
	"LR-002"
)
observe(
	"${fixture_source}/edge_cases.cpp" "EdgeCases::ternary(bool) const"
	"edge_cases_ternary.inspect.json"
	"LR-041"
)
observe(
	"${fixture_source}/edge_cases.cpp" "EdgeCases::default_argument(int)"
	"edge_cases_default_argument.inspect.json"
	"LR-042"
)
observe(
	"${fixture_source}/edge_cases.cpp" "EdgeCases::value_category_and_casts(const int*)"
	"edge_cases_value_category_and_casts.inspect.json"
	"LR-044" "LR-045"
)
observe(
	"${fixture_source}/edge_cases.cpp" "EdgeCases::branch_controls(int)"
	"edge_cases_branch_controls.inspect.json"
	"LR-046"
)
observe(
	"${fixture_source}/edge_cases.cpp" "EdgeCases::field_address()"
	"edge_cases_field_address.inspect.json"
	"LR-029"
)
observe(
	"${fixture_source}/edge_cases.cpp" "outer::Container::Inner::run() const"
	"outer_container_inner_run.inspect.json"
	"LR-047"
)

# --- SyntaxMatrix (syntax_matrix.cpp) ---
observe(
	"${fixture_source}/syntax_matrix.cpp" "SyntaxMatrix::base_global_static(int)"
	"syntax_matrix_base_global_static.inspect.json"
	"LR-008" "LR-010" "LR-011"
)
observe(
	"${fixture_source}/syntax_matrix.cpp" "SyntaxMatrix::dispatch(int) const"
	"syntax_matrix_dispatch.inspect.json"
	"LR-012"
)
observe(
	"${fixture_source}/syntax_matrix.cpp" "SyntaxMatrix::operator_path()"
	"syntax_matrix_operator_path.inspect.json"
	"LR-013"
)
observe(
	"${fixture_source}/syntax_matrix.cpp" "SyntaxMatrix::lambda_run(int)"
	"syntax_matrix_lambda_run.inspect.json"
	"LR-017" "LR-018"
)
observe(
	"${fixture_source}/syntax_matrix.cpp" "SyntaxMatrix::switch_loop(int)"
	"syntax_matrix_switch_loop.inspect.json"
	"LR-015" "LR-016" "LR-046"
)
observe(
	"${fixture_source}/syntax_matrix.cpp" "SyntaxMatrix::exception_run(int)"
	"syntax_matrix_exception_run.inspect.json"
	"LR-035" "LR-036"
)
observe(
	"${fixture_source}/syntax_matrix.cpp" "SyntaxMatrix::identity_and_type(MatrixBase*)"
	"syntax_matrix_identity_and_type.inspect.json"
	"LR-020" "LR-023" "LR-024"
)
observe(
	"${fixture_source}/syntax_matrix.cpp" "SyntaxMatrix::first_byte()"
	"syntax_matrix_first_byte.inspect.json"
	"LR-025"
)
observe(
	"${fixture_source}/syntax_matrix.cpp" "SyntaxMatrix::release()"
	"syntax_matrix_release.inspect.json"
	"LR-026"
)
observe(
	"${fixture_source}/syntax_matrix.cpp" "SyntaxMatrix::reset_in_place()"
	"syntax_matrix_reset_in_place.inspect.json"
	"LR-027" "LR-028" "LR-049"
)

# --- TemplateExample (hardening.cpp) ---
observe(
	"${fixture_source}/hardening.cpp" "TemplateExample::target<int>(int)"
	"template_example_target.inspect.json"
	"LR-033"
)

message(STATUS "Phase A coverage observations: OK")
