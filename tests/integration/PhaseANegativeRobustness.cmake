if(NOT DEFINED AZTECA_EXECUTABLE)
	message(FATAL_ERROR "AZTECA_EXECUTABLE is required")
endif()

if(NOT DEFINED PROJECT_SOURCE_DIR)
	message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

if(NOT DEFINED PROJECT_BINARY_DIR)
	message(FATAL_ERROR "PROJECT_BINARY_DIR is required")
endif()

# Build the simple fixture so we have a valid compile DB to attack against.
set(fixture_source "${PROJECT_SOURCE_DIR}/tests/fixtures/phase_a/simple")
set(fixture_build "${PROJECT_BINARY_DIR}/test-work/phase_a/simple")
set(malformed_source "${PROJECT_SOURCE_DIR}/tests/negative/phase_a_malformed")
set(malformed_build "${PROJECT_BINARY_DIR}/test-work/phase_a/malformed")
set(robustness_source "${PROJECT_SOURCE_DIR}/tests/fixtures/phase_a/robustness")
set(robustness_build "${PROJECT_BINARY_DIR}/test-work/phase_a/robustness")

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

execute_process(
	COMMAND
		${CMAKE_COMMAND}
		-S "${malformed_source}"
		-B "${malformed_build}"
		-G Ninja
		-DCMAKE_CXX_COMPILER=clang++-18
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	RESULT_VARIABLE malformed_configure_result
	OUTPUT_VARIABLE malformed_configure_output
	ERROR_VARIABLE malformed_configure_error
)

if(NOT malformed_configure_result EQUAL 0)
	message(FATAL_ERROR "malformed fixture configure failed:\n${malformed_configure_output}\n${malformed_configure_error}")
endif()

execute_process(
	COMMAND
		${CMAKE_COMMAND}
		-S "${robustness_source}"
		-B "${robustness_build}"
		-G Ninja
		-DCMAKE_CXX_COMPILER=clang++-18
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	RESULT_VARIABLE robustness_configure_result
	OUTPUT_VARIABLE robustness_configure_output
	ERROR_VARIABLE robustness_configure_error
)

if(NOT robustness_configure_result EQUAL 0)
	message(FATAL_ERROR "robustness fixture configure failed:\n${robustness_configure_output}\n${robustness_configure_error}")
endif()

# ---------------------------------------------------------------------------
# Shared assertions. Every CLI failure must:
#   - exit with the documented code (phase_a_cli.md §5)
#   - emit AZT-E* on stderr
#   - keep stdout empty when --format json was requested
# Hardening this contract prevents silent regressions to error reporting.
# ---------------------------------------------------------------------------
function(assert_failure_contract label expected_exit expected_diag
		actual_exit actual_stdout actual_stderr json_mode)
	if(NOT actual_exit EQUAL ${expected_exit})
		message(
			FATAL_ERROR
			"${label}: exit should be ${expected_exit} but was ${actual_exit}\n"
			"stdout:\n${actual_stdout}\nstderr:\n${actual_stderr}"
		)
	endif()

	string(FIND "${actual_stderr}" "${expected_diag}" diag_index)
	if(diag_index EQUAL -1)
		message(
			FATAL_ERROR
			"${label}: stderr missing diagnostic id '${expected_diag}'\nstderr:\n${actual_stderr}"
		)
	endif()

	if(json_mode AND NOT "${actual_stdout}" STREQUAL "")
		message(
			FATAL_ERROR
			"${label}: --format json failure must leave stdout empty but got:\n${actual_stdout}"
		)
	endif()
endfunction()

# ---------------------------------------------------------------------------
# 1. Nonexistent compile-db directory -> exit 2, AZT-E0007
# ---------------------------------------------------------------------------
set(missing_dir "${PROJECT_BINARY_DIR}/test-work/phase_a/_missing_dir_does_not_exist")
file(REMOVE_RECURSE "${missing_dir}")

foreach(fmt text json)
	execute_process(
		COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${missing_dir}"
				--method "Gauge::reading() const" --format ${fmt}
		RESULT_VARIABLE r OUTPUT_VARIABLE o ERROR_VARIABLE e
	)
	set(_json_mode FALSE)
	if(fmt STREQUAL "json")
		set(_json_mode TRUE)
	endif()
	assert_failure_contract(
		"nonexistent -p (${fmt})" 2 "AZT-E0007" ${r} "${o}" "${e}" ${_json_mode}
	)
endforeach()

# ---------------------------------------------------------------------------
# 2. Malformed compile_commands.json -> exit 2, AZT-E0007
# ---------------------------------------------------------------------------
set(broken_dir "${PROJECT_BINARY_DIR}/test-work/phase_a/_broken_db")
file(MAKE_DIRECTORY "${broken_dir}")
file(WRITE "${broken_dir}/compile_commands.json" "{ this is not valid json\n")

foreach(fmt text json)
	execute_process(
		COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${broken_dir}"
				--method "Gauge::reading() const" --format ${fmt}
		RESULT_VARIABLE r OUTPUT_VARIABLE o ERROR_VARIABLE e
	)
	set(_json_mode FALSE)
	if(fmt STREQUAL "json")
		set(_json_mode TRUE)
	endif()
	assert_failure_contract(
		"malformed compile_commands.json (${fmt})" 2 "AZT-E0007" ${r} "${o}" "${e}" ${_json_mode}
	)
endforeach()

# ---------------------------------------------------------------------------
# 3. Method not found in valid DB -> exit 3, AZT-E0008
# ---------------------------------------------------------------------------
foreach(fmt text json)
	execute_process(
		COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}"
				--method "NoSuchClass::no_such_method(int)" --format ${fmt}
		RESULT_VARIABLE r OUTPUT_VARIABLE o ERROR_VARIABLE e
	)
	set(_json_mode FALSE)
	if(fmt STREQUAL "json")
		set(_json_mode TRUE)
	endif()
	assert_failure_contract(
		"method not found (${fmt})" 3 "AZT-E0008" ${r} "${o}" "${e}" ${_json_mode}
	)
endforeach()

# ---------------------------------------------------------------------------
# 4. Method spec containing control characters / weird whitespace must be
#    rejected at parse time (exit 1, AZT-E0004) — never crash, never reach
#    Clang. This hardens against shell-injected garbage in --method.
#    Note: parameter list content is intentionally lenient (parsed as types
#    later), so we only assert hard rejection on structurally broken specs.
# ---------------------------------------------------------------------------
foreach(bad_spec
		"   "
		"::"
		"Foo::"
		"Foo::(")
	execute_process(
		COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}"
				--method "${bad_spec}" --format json
		RESULT_VARIABLE r OUTPUT_VARIABLE o ERROR_VARIABLE e
	)
	assert_failure_contract(
		"bad --method spec '${bad_spec}'" 1 "AZT-E0004" ${r} "${o}" "${e}" TRUE
	)
endforeach()

# ---------------------------------------------------------------------------
# 5. --source not present in compile_commands.json -> exit 2, AZT-E0007.
# ---------------------------------------------------------------------------
execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}"
			--source "${fixture_source}/include/real_project/api.hpp"
			--method "real::project::Runner::inspect(int)" --format json
	RESULT_VARIABLE source_not_found_result
	OUTPUT_VARIABLE source_not_found_stdout
	ERROR_VARIABLE source_not_found_stderr
)
assert_failure_contract(
	"--source not in compile DB" 2 "AZT-E0007" ${source_not_found_result}
	"${source_not_found_stdout}" "${source_not_found_stderr}" TRUE
)

# ---------------------------------------------------------------------------
# 6. Alias/type spelling mismatch -> exit 3, AZT-E0008.
# ---------------------------------------------------------------------------
execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}"
			--source "${fixture_source}/real_project.cpp"
			--method "real::project::Runner::inspect(NoSuchAlias)" --format json
	RESULT_VARIABLE alias_mismatch_result
	OUTPUT_VARIABLE alias_mismatch_stdout
	ERROR_VARIABLE alias_mismatch_stderr
)
assert_failure_contract(
	"alias/type spelling mismatch" 3 "AZT-E0008" ${alias_mismatch_result}
	"${alias_mismatch_stdout}" "${alias_mismatch_stderr}" TRUE
)

# ---------------------------------------------------------------------------
# 7. Malformed macro-heavy TU -> exit 2, AZT-E0007.
# ---------------------------------------------------------------------------
execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${malformed_build}"
			--source "${malformed_source}/malformed_macro.cpp"
			--method "BrokenMacroHost::run(int)" --format json
	RESULT_VARIABLE malformed_result
	OUTPUT_VARIABLE malformed_stdout
	ERROR_VARIABLE malformed_stderr
)
assert_failure_contract(
	"malformed macro-heavy TU" 2 "AZT-E0007" ${malformed_result}
	"${malformed_stdout}" "${malformed_stderr}" TRUE
)

# ---------------------------------------------------------------------------
# 7b. Project-wide inspect may skip an unrelated malformed TU when a matching
#     target is found elsewhere in the compile DB. The skipped parse is still
#     surfaced as AZT-W0002 in JSON diagnostics.
# ---------------------------------------------------------------------------
execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${robustness_build}"
			--method "RobustTarget::run(int)" --format json
	RESULT_VARIABLE robust_project_result
	OUTPUT_VARIABLE robust_project_stdout
	ERROR_VARIABLE robust_project_stderr
)
if(NOT robust_project_result EQUAL 0)
	message(
		FATAL_ERROR
		"project-wide inspect should skip unrelated parse failure and exit 0 but exited "
		"${robust_project_result}:\nstdout:\n${robust_project_stdout}\nstderr:\n${robust_project_stderr}"
	)
endif()
if(NOT robust_project_stderr STREQUAL "")
	message(FATAL_ERROR "project-wide inspect success should keep stderr empty:\n${robust_project_stderr}")
endif()
string(JSON robust_schema ERROR_VARIABLE robust_parse_error GET "${robust_project_stdout}" "schema_version")
if(robust_parse_error)
	message(FATAL_ERROR "project-wide robustness output was not valid JSON:\n${robust_parse_error}\n${robust_project_stdout}")
endif()
if(NOT robust_schema STREQUAL "2")
	message(FATAL_ERROR "project-wide robustness schema_version should be 2 but was ${robust_schema}")
endif()
string(FIND "${robust_project_stdout}" "AZTECA_CLANG_PARSE_SKIPPED" skipped_internal_index)
if(skipped_internal_index EQUAL -1)
	message(FATAL_ERROR "project-wide robustness output missing skipped parse diagnostic:\n${robust_project_stdout}")
endif()
string(FIND "${robust_project_stdout}" "AZT-W0002" skipped_public_index)
if(skipped_public_index EQUAL -1)
	message(FATAL_ERROR "project-wide robustness output missing AZT-W0002:\n${robust_project_stdout}")
endif()

# Directly inspecting the broken TU remains an exit-2 compile database error.
execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${robustness_build}"
			--source "${robustness_source}/broken.cpp"
			--method "BrokenTranslationUnit::run()" --format json
	RESULT_VARIABLE robust_source_result
	OUTPUT_VARIABLE robust_source_stdout
	ERROR_VARIABLE robust_source_stderr
)
assert_failure_contract(
	"requested broken TU" 2 "AZT-E0007" ${robust_source_result}
	"${robust_source_stdout}" "${robust_source_stderr}" TRUE
)

# ---------------------------------------------------------------------------
# 8. Repeated --format value: last one wins or rejected — must not crash.
#    Either AZT-E0001/0006 (rejection) or exit 0 (last-wins) is acceptable;
#    a crash (exit > 127 or negative) is not.
# ---------------------------------------------------------------------------
execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}"
			--method "Gauge::reading() const"
			--format text --format json
	RESULT_VARIABLE repeated_result
	OUTPUT_VARIABLE repeated_stdout
	ERROR_VARIABLE repeated_stderr
)
if(repeated_result LESS 0 OR repeated_result GREATER 127)
	message(
		FATAL_ERROR
		"repeated --format must not crash but exit was ${repeated_result}\n"
		"stdout:\n${repeated_stdout}\nstderr:\n${repeated_stderr}"
	)
endif()

# ---------------------------------------------------------------------------
# 9. JSON output on success must be parseable. Re-confirm here so this
#    test fails fast if a regression makes JSON malformed for any reason.
# ---------------------------------------------------------------------------
execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}"
			--source "${fixture_source}/simple_scenarios.cpp"
			--method "Gauge::reading() const" --format json
	RESULT_VARIABLE ok_result OUTPUT_VARIABLE ok_stdout ERROR_VARIABLE ok_stderr
)
if(NOT ok_result EQUAL 0)
	message(FATAL_ERROR "sanity inspect failed (exit=${ok_result}):\n${ok_stderr}")
endif()
string(JSON _schema ERROR_VARIABLE _parse_err GET "${ok_stdout}" "schema_version")
if(_parse_err)
	message(FATAL_ERROR "sanity inspect produced invalid JSON: ${_parse_err}\n${ok_stdout}")
endif()
if(NOT _schema STREQUAL "2")
	message(FATAL_ERROR "sanity inspect schema_version should be 2 but was ${_schema}")
endif()

message(STATUS "Phase A negative robustness contract: OK")
