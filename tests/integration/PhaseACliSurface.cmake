if(NOT DEFINED AZTECA_EXECUTABLE)
	message(FATAL_ERROR "AZTECA_EXECUTABLE is required")
endif()

if(NOT DEFINED PROJECT_SOURCE_DIR)
	message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
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

message(STATUS "Phase A CLI surface contract: OK")
