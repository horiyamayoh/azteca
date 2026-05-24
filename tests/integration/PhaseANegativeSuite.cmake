if(NOT DEFINED AZTECA_EXECUTABLE)
	message(FATAL_ERROR "AZTECA_EXECUTABLE is required")
endif()
if(NOT DEFINED PROJECT_SOURCE_DIR)
	message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()
if(NOT DEFINED PROJECT_BINARY_DIR)
	message(FATAL_ERROR "PROJECT_BINARY_DIR is required")
endif()

# ---------------------------------------------------------------------------
# Build the simple fixture (provides ambiguous_a/ambiguous_b -> AZT-E0009) and
# the negative fixture (declaration_only.cpp -> AZT-E0010).
# ---------------------------------------------------------------------------
set(simple_source "${PROJECT_SOURCE_DIR}/tests/fixtures/phase_a/simple")
set(simple_build  "${PROJECT_BINARY_DIR}/test-work/phase_a/simple")
set(negative_source "${PROJECT_SOURCE_DIR}/tests/negative/phase_a")
set(negative_build  "${PROJECT_BINARY_DIR}/test-work/phase_a/negative")

foreach(_pair "${simple_source}|${simple_build}" "${negative_source}|${negative_build}")
	string(REPLACE "|" ";" _pair "${_pair}")
	list(GET _pair 0 _src)
	list(GET _pair 1 _bld)
	execute_process(
		COMMAND
			${CMAKE_COMMAND} -S "${_src}" -B "${_bld}" -G Ninja
			-DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
		RESULT_VARIABLE _r OUTPUT_VARIABLE _o ERROR_VARIABLE _e
	)
	if(NOT _r EQUAL 0)
		message(FATAL_ERROR "fixture configure failed for ${_src}:\n${_o}\n${_e}")
	endif()
endforeach()

# ---------------------------------------------------------------------------
# Helper: run azteca and assert exit code, stderr diagnostic id, and (for
# --format json) that stdout is empty.
# ---------------------------------------------------------------------------
function(assert_inspect_failure label build_dir method format expected_exit expected_diag)
	set(_extra ${ARGN})
	execute_process(
		COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${build_dir}"
				--method "${method}" --format ${format} ${_extra}
		RESULT_VARIABLE _r OUTPUT_VARIABLE _o ERROR_VARIABLE _e
	)
	if(NOT _r EQUAL ${expected_exit})
		message(FATAL_ERROR
			"${label}: exit should be ${expected_exit} but was ${_r}\nstdout:\n${_o}\nstderr:\n${_e}")
	endif()
	string(FIND "${_e}" "${expected_diag}" _idx)
	if(_idx EQUAL -1)
		message(FATAL_ERROR
			"${label}: stderr missing '${expected_diag}'\nstderr:\n${_e}")
	endif()
	if(format STREQUAL "json" AND NOT "${_o}" STREQUAL "")
		message(FATAL_ERROR
			"${label}: --format json failure must leave stdout empty but got:\n${_o}")
	endif()
endfunction()

# ---------------------------------------------------------------------------
# AZT-E0009: ambiguous method (same qualified name in two TUs, no --source).
# ---------------------------------------------------------------------------
foreach(fmt text json)
	assert_inspect_failure(
		"ambiguous method (${fmt})"
		"${simple_build}" "AmbiguousTarget::run(int)" ${fmt} 3 "AZT-E0009"
	)
endforeach()

# Same method with --source disambiguation must succeed.
execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${simple_build}"
			--source "${simple_source}/ambiguous_a.cpp"
			--method "AmbiguousTarget::run(int)" --format json
	RESULT_VARIABLE _r OUTPUT_VARIABLE _o ERROR_VARIABLE _e
)
if(NOT _r EQUAL 0)
	message(FATAL_ERROR "ambiguous + --source must succeed but exit=${_r}\nstderr:\n${_e}")
endif()

# ---------------------------------------------------------------------------
# AZT-E0010: declaration-only method has no body to extract.
# ---------------------------------------------------------------------------
foreach(fmt text json)
	assert_inspect_failure(
		"declaration-only method (${fmt})"
		"${negative_build}"
		"decl_only_fixture::DeclOnly::forward_declared_only(int) const"
		${fmt} 3 "AZT-E0010"
	)
endforeach()

# ---------------------------------------------------------------------------
# AZT-E0007: empty compile_commands.json (valid JSON, but no entries).
# ---------------------------------------------------------------------------
set(_empty_dir "${PROJECT_BINARY_DIR}/test-work/phase_a/_empty_db")
file(MAKE_DIRECTORY "${_empty_dir}")
file(WRITE "${_empty_dir}/compile_commands.json" "[]\n")
foreach(fmt text json)
	assert_inspect_failure(
		"empty compile_commands (${fmt})"
		"${_empty_dir}" "Gauge::reading() const" ${fmt} 2 "AZT-E0007"
	)
endforeach()

message(STATUS "Phase A negative suite: OK")
