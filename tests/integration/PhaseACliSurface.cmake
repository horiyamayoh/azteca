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
set(fixture_build "${PROJECT_BINARY_DIR}/test-work/phase_a/cli_surface")

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
	RESULT_VARIABLE configure_result
	OUTPUT_VARIABLE configure_output
	ERROR_VARIABLE configure_error
)

if(NOT configure_result EQUAL 0)
	message(FATAL_ERROR "fixture configure failed:\n${configure_output}\n${configure_error}")
endif()

function(assert_or_update_golden golden_path actual_output context)
	if(DEFINED ENV{AZTECA_ACCEPT_GOLDEN} AND NOT "$ENV{AZTECA_ACCEPT_GOLDEN}" STREQUAL "")
		file(WRITE "${golden_path}" "${actual_output}")
		return()
	endif()

	if(NOT EXISTS "${golden_path}")
		message(FATAL_ERROR "golden missing: ${golden_path}\nactual:\n${actual_output}")
	endif()

	file(READ "${golden_path}" expected_output)
	if(NOT actual_output STREQUAL expected_output)
		message(FATAL_ERROR "${context} differed from golden:\n${actual_output}")
	endif()
endfunction()

function(assert_contains haystack needle context)
	string(FIND "${haystack}" "${needle}" found_index)
	if(found_index EQUAL -1)
		message(FATAL_ERROR "${context} did not contain '${needle}':\n${haystack}")
	endif()
endfunction()

function(assert_empty value context)
	if(NOT "${value}" STREQUAL "")
		message(FATAL_ERROR "${context} should be empty but was:\n${value}")
	endif()
endfunction()

# --- azteca --help -------------------------------------------------------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" --help
	RESULT_VARIABLE help_result
	OUTPUT_VARIABLE help_output
	ERROR_VARIABLE help_error
)

if(NOT help_result EQUAL 0)
	message(FATAL_ERROR "azteca --help failed (exit=${help_result}):\n${help_error}")
endif()

assert_or_update_golden(
	"${PROJECT_SOURCE_DIR}/tests/golden/phase_a/cli/help.txt"
	"${help_output}"
	"azteca --help output"
)

# --- azteca with no args also shows help (exit 0) ------------------------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}"
	RESULT_VARIABLE noargs_result
	OUTPUT_VARIABLE noargs_output
	ERROR_VARIABLE noargs_error
)

if(NOT noargs_result EQUAL 0)
	message(FATAL_ERROR "azteca (no args) exit was ${noargs_result}, expected 0")
endif()

if(NOT noargs_output STREQUAL help_output)
	message(FATAL_ERROR "azteca (no args) output differs from --help")
endif()

# --- unknown subcommand exits 1 ------------------------------------------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" no-such-command
	RESULT_VARIABLE unknown_result
	OUTPUT_VARIABLE unknown_output
	ERROR_VARIABLE unknown_error
)

if(NOT unknown_result EQUAL 1)
	message(FATAL_ERROR "azteca unknown subcommand exit was ${unknown_result}, expected 1")
endif()

if(NOT unknown_error MATCHES "AZT-E0002")
	message(FATAL_ERROR "azteca unknown subcommand stderr missing AZT-E0002:\n${unknown_error}")
endif()

# --- Phase A non-scope subcommands exit 1 with AZT-E0002 -----------------

foreach(phase_b_command extract scan build test diff record replay-transcript)
	execute_process(
		COMMAND "${AZTECA_EXECUTABLE}" "${phase_b_command}"
		RESULT_VARIABLE nonscope_result
		OUTPUT_VARIABLE nonscope_output
		ERROR_VARIABLE nonscope_error
	)

	if(NOT nonscope_result EQUAL 1)
		message(
			FATAL_ERROR
			"azteca ${phase_b_command} exit was ${nonscope_result}, expected 1"
		)
	endif()

	assert_empty("${nonscope_output}" "azteca ${phase_b_command} stdout")
	assert_contains("${nonscope_error}" "AZT-E0002" "azteca ${phase_b_command} stderr")
endforeach()

# --- inspect missing --build-dir exits 1 with AZT-E0001 ------------------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect --method "C::m()"
	RESULT_VARIABLE missing_bd_result
	OUTPUT_VARIABLE missing_bd_output
	ERROR_VARIABLE missing_bd_error
)

if(NOT missing_bd_result EQUAL 1)
	message(FATAL_ERROR "azteca inspect missing -p exit was ${missing_bd_result}, expected 1")
endif()

if(NOT missing_bd_error MATCHES "AZT-E0001")
	message(FATAL_ERROR "azteca inspect missing -p stderr missing AZT-E0001:\n${missing_bd_error}")
endif()

# --- inspect missing --method exits 1 with AZT-E0001 ---------------------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p .
	RESULT_VARIABLE missing_method_result
	OUTPUT_VARIABLE missing_method_output
	ERROR_VARIABLE missing_method_error
)

if(NOT missing_method_result EQUAL 1)
	message(
		FATAL_ERROR
		"azteca inspect missing --method exit was ${missing_method_result}, expected 1"
	)
endif()

assert_empty("${missing_method_output}" "azteca inspect missing --method stdout")
assert_contains("${missing_method_error}" "AZT-E0001" "azteca inspect missing --method stderr")
assert_contains(
	"${missing_method_error}" "inspect requires --method" "azteca inspect missing --method stderr"
)

# --- inspect missing --template-args value exits 1 with AZT-E0001 -------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p . --method "C::m()" --template-args
	RESULT_VARIABLE missing_template_args_result
	OUTPUT_VARIABLE missing_template_args_output
	ERROR_VARIABLE missing_template_args_error
)

if(NOT missing_template_args_result EQUAL 1)
	message(
		FATAL_ERROR
		"azteca inspect missing --template-args exit was ${missing_template_args_result}, expected 1"
	)
endif()

if(NOT missing_template_args_error MATCHES "AZT-E0001")
	message(
		FATAL_ERROR
		"azteca inspect missing --template-args stderr missing AZT-E0001:\n${missing_template_args_error}"
	)
endif()

# --- inspect invalid --format exits 1 with AZT-E0006 ---------------------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p . --method "C::m()" --format yaml
	RESULT_VARIABLE bad_format_result
	OUTPUT_VARIABLE bad_format_output
	ERROR_VARIABLE bad_format_error
)

if(NOT bad_format_result EQUAL 1)
	message(FATAL_ERROR "azteca inspect bad --format exit was ${bad_format_result}, expected 1")
endif()

if(NOT bad_format_error MATCHES "AZT-E0006")
	message(FATAL_ERROR "azteca inspect bad --format stderr missing AZT-E0006:\n${bad_format_error}")
endif()

# --- inspect invalid --method exits 1 with AZT-E0004 ---------------------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p . --method "((not a spec"
	RESULT_VARIABLE bad_method_result
	OUTPUT_VARIABLE bad_method_output
	ERROR_VARIABLE bad_method_error
)

if(NOT bad_method_result EQUAL 1)
	message(FATAL_ERROR "azteca inspect bad --method exit was ${bad_method_result}, expected 1")
endif()

if(NOT bad_method_error MATCHES "AZT-E0004")
	message(FATAL_ERROR "azteca inspect bad --method stderr missing AZT-E0004:\n${bad_method_error}")
endif()

# --- explain known id succeeds -------------------------------------------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" explain AZT-E0008
	RESULT_VARIABLE explain_ok_result
	OUTPUT_VARIABLE explain_ok_output
	ERROR_VARIABLE explain_ok_error
)

if(NOT explain_ok_result EQUAL 0)
	message(FATAL_ERROR "azteca explain AZT-E0008 exit was ${explain_ok_result}, expected 0")
endif()

if(NOT explain_ok_output MATCHES "AZT-E0008")
	message(FATAL_ERROR "azteca explain AZT-E0008 missing id in stdout:\n${explain_ok_output}")
endif()

assert_or_update_golden(
	"${PROJECT_SOURCE_DIR}/tests/golden/phase_a/cli/explain_AZT-E0008.txt"
	"${explain_ok_output}"
	"azteca explain AZT-E0008 output"
)

# --- explain known internal alias succeeds --------------------------------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" explain AZTECA_PATH_CONSERVATIVE
	RESULT_VARIABLE explain_alias_result
	OUTPUT_VARIABLE explain_alias_output
	ERROR_VARIABLE explain_alias_error
)

if(NOT explain_alias_result EQUAL 0)
	message(
		FATAL_ERROR
		"azteca explain AZTECA_PATH_CONSERVATIVE exit was ${explain_alias_result}, expected 0"
	)
endif()

if(NOT explain_alias_output MATCHES "AZT-W0001")
	message(
		FATAL_ERROR
		"azteca explain AZTECA_PATH_CONSERVATIVE missing public id in stdout:\n${explain_alias_output}"
	)
endif()

# --- explain unknown id fails with AZT-E0003 -----------------------------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" explain AZT-Z9999
	RESULT_VARIABLE explain_bad_result
	OUTPUT_VARIABLE explain_bad_output
	ERROR_VARIABLE explain_bad_error
)

if(NOT explain_bad_result EQUAL 1)
	message(FATAL_ERROR "azteca explain unknown exit was ${explain_bad_result}, expected 1")
endif()

if(NOT explain_bad_error MATCHES "AZT-E0003")
	message(FATAL_ERROR "azteca explain unknown stderr missing AZT-E0003:\n${explain_bad_error}")
endif()

# --- explain without argument fails with AZT-E0001 -----------------------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" explain
	RESULT_VARIABLE explain_noarg_result
	OUTPUT_VARIABLE explain_noarg_output
	ERROR_VARIABLE explain_noarg_error
)

if(NOT explain_noarg_result EQUAL 1)
	message(FATAL_ERROR "azteca explain (no arg) exit was ${explain_noarg_result}, expected 1")
endif()

if(NOT explain_noarg_error MATCHES "AZT-E0001")
	message(FATAL_ERROR "azteca explain (no arg) stderr missing AZT-E0001:\n${explain_noarg_error}")
endif()

# --- --quiet success suppresses stdout and stderr -------------------------

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/service.cpp" --method "Service::handle(Id)" --format json --quiet
	RESULT_VARIABLE quiet_success_result
	OUTPUT_VARIABLE quiet_success_output
	ERROR_VARIABLE quiet_success_error
)

if(NOT quiet_success_result EQUAL 0)
	message(
		FATAL_ERROR
		"azteca inspect --quiet success exit was ${quiet_success_result}, expected 0:\n"
		"${quiet_success_output}\n${quiet_success_error}"
	)
endif()

assert_empty("${quiet_success_output}" "azteca inspect --quiet success stdout")
assert_empty("${quiet_success_error}" "azteca inspect --quiet success stderr")

# --- JSON purity: failure path keeps stdout empty (diagnostics on stderr) ---

set(_purity_dir "${CMAKE_CURRENT_BINARY_DIR}/phase_a_purity")
file(MAKE_DIRECTORY "${_purity_dir}")
file(WRITE "${_purity_dir}/compile_commands.json" "[]\n")

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${_purity_dir}" --method "X::y()" --format json
	RESULT_VARIABLE purity_result
	OUTPUT_VARIABLE purity_stdout
	ERROR_VARIABLE purity_stderr
)

if(purity_result EQUAL 0)
	message(FATAL_ERROR "JSON failure path unexpectedly succeeded")
endif()

if(NOT "${purity_stdout}" STREQUAL "")
	message(
		FATAL_ERROR
		"--format json failure must keep stdout empty but got:\n${purity_stdout}"
	)
endif()

if(NOT purity_stderr MATCHES "AZT-E")
	message(FATAL_ERROR "--format json failure stderr missing AZT-E* id:\n${purity_stderr}")
endif()

# --- --quiet JSON failure keeps stdout empty and reports AZT-E* on stderr -

execute_process(
	COMMAND
		"${AZTECA_EXECUTABLE}" inspect -p "${_purity_dir}" --method "X::y()" --format json
		--quiet
	RESULT_VARIABLE quiet_failure_result
	OUTPUT_VARIABLE quiet_failure_stdout
	ERROR_VARIABLE quiet_failure_stderr
)

if(quiet_failure_result EQUAL 0)
	message(FATAL_ERROR "--quiet JSON failure path unexpectedly succeeded")
endif()

assert_empty("${quiet_failure_stdout}" "--quiet --format json failure stdout")
assert_contains("${quiet_failure_stderr}" "AZT-E" "--quiet --format json failure stderr")

message(STATUS "Phase A CLI surface contract: OK")
