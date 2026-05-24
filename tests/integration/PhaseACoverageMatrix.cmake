if(NOT DEFINED AZTECA_EXECUTABLE)
	message(FATAL_ERROR "AZTECA_EXECUTABLE is required")
endif()

if(NOT DEFINED PROJECT_SOURCE_DIR)
	message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

if(NOT DEFINED PROJECT_BINARY_DIR)
	message(FATAL_ERROR "PROJECT_BINARY_DIR is required")
endif()

# Coverage gap-fillers for Phase A inspect: assert that each "future" row in
# docs/planning/25_phase_a_inspect_coverage.md surfaces a stable, identified
# diagnostic instead of crashing or producing partial output.

set(simple_source "${PROJECT_SOURCE_DIR}/tests/fixtures/phase_a/simple")
set(simple_build "${PROJECT_BINARY_DIR}/test-work/phase_a/simple")

set(coverage_source "${PROJECT_SOURCE_DIR}/tests/fixtures/phase_a/coverage")
set(coverage_build "${PROJECT_BINARY_DIR}/test-work/phase_a/coverage")

execute_process(
	COMMAND
		${CMAKE_COMMAND}
		-S "${coverage_source}"
		-B "${coverage_build}"
		-G Ninja
		-DCMAKE_CXX_COMPILER=clang++-18
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	RESULT_VARIABLE _cfg_result
	OUTPUT_VARIABLE _cfg_out
	ERROR_VARIABLE _cfg_err
)
if(NOT _cfg_result EQUAL 0)
	message(FATAL_ERROR "coverage fixture configure failed:\n${_cfg_out}\n${_cfg_err}")
endif()

function(expect_failure_with phrase id source method context)
	execute_process(
		COMMAND
			"${AZTECA_EXECUTABLE}" inspect -p "${simple_build}" --source "${source}"
			--method "${method}"
		RESULT_VARIABLE _rc
		OUTPUT_VARIABLE _out
		ERROR_VARIABLE _err
	)
	if(_rc EQUAL 0)
		message(FATAL_ERROR "${context}: expected non-zero exit, got 0\nstdout:\n${_out}\nstderr:\n${_err}")
	endif()
	string(FIND "${_err}" "${id}" _id_idx)
	if(_id_idx EQUAL -1)
		message(FATAL_ERROR "${context}: stderr missing '${id}':\n${_err}")
	endif()
	string(FIND "${_err}" "${phrase}" _phrase_idx)
	if(_phrase_idx EQUAL -1)
		message(FATAL_ERROR "${context}: stderr missing '${phrase}':\n${_err}")
	endif()
endfunction()

# operator method target -> parse-time rejection (AZT-E0004)
expect_failure_with(
	"operator methods are not supported"
	"AZT-E0004"
	"${simple_source}/syntax_matrix.cpp"
	"SyntaxMatrix::operator+(SyntaxMatrix)"
	"operator method target"
)

# destructor target -> unsupported target (AZT-E0010)
expect_failure_with(
	"constructors and destructors are outside Phase A inspect scope"
	"AZT-E0010"
	"${simple_source}/syntax_matrix.cpp"
	"SyntaxMatrix::~SyntaxMatrix()"
	"destructor target"
)

# Coroutine body: inspect must succeed and surface the coroutine
# not_yet_implemented diagnostic in JSON.
execute_process(
	COMMAND
		"${AZTECA_EXECUTABLE}" inspect -p "${coverage_build}" --source
		"${coverage_source}/coroutine.cpp" --method "CoroHost::run(int)" --format json
	RESULT_VARIABLE _coro_rc
	OUTPUT_VARIABLE _coro_out
	ERROR_VARIABLE _coro_err
)
if(NOT _coro_rc EQUAL 0)
	message(FATAL_ERROR "coroutine inspect failed:\n${_coro_out}\n${_coro_err}")
endif()
string(JSON _coro_schema GET "${_coro_out}" schema_version)
if(NOT _coro_schema STREQUAL "2")
	message(FATAL_ERROR "coroutine inspect schema_version=${_coro_schema}")
endif()
string(JSON _coro_phase GET "${_coro_out}" azteca_phase)
if(NOT _coro_phase STREQUAL "A")
	message(FATAL_ERROR "coroutine inspect azteca_phase=${_coro_phase}")
endif()
string(FIND "${_coro_out}" "\"coroutine\"" _coro_idx)
if(_coro_idx EQUAL -1)
	message(FATAL_ERROR "coroutine inspect missing 'coroutine' construct:\n${_coro_out}")
endif()
string(FIND "${_coro_out}" "not_yet_implemented" _nyi_idx)
if(_nyi_idx EQUAL -1)
	message(FATAL_ERROR "coroutine inspect missing 'not_yet_implemented':\n${_coro_out}")
endif()

message(STATUS "Phase A coverage matrix gap-fillers: OK")
