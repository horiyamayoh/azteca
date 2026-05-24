if(NOT DEFINED AZTECA_EXECUTABLE)
	message(FATAL_ERROR "AZTECA_EXECUTABLE is required")
endif()

if(NOT DEFINED PROJECT_BINARY_DIR)
	message(FATAL_ERROR "PROJECT_BINARY_DIR is required")
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER)
	message(FATAL_ERROR "CMAKE_CXX_COMPILER is required")
endif()

set(_work "${PROJECT_BINARY_DIR}/test-work/phase_a_encoding")
file(REMOVE_RECURSE "${_work}")
file(MAKE_DIRECTORY "${_work}")

set(_body
"class C { public: int m(int x) { return x + 1; } };
int sink(C& c) { return c.m(2); }
")

# Produce a 3-byte UTF-8 BOM via POSIX printf, then prepend it to the ASCII body.
execute_process(
	COMMAND printf "\\xef\\xbb\\xbf"
	OUTPUT_FILE "${_work}/bom_prefix.bin"
)
file(READ "${_work}/bom_prefix.bin" _bom_hex HEX)
if(NOT _bom_hex STREQUAL "efbbbf")
	message(FATAL_ERROR "failed to produce UTF-8 BOM bytes (got ${_bom_hex})")
endif()
file(READ "${_work}/bom_prefix.bin" _bom_bytes)
file(WRITE "${_work}/bom_source.cpp" "${_bom_bytes}${_body}")

# CRLF line endings.
string(REPLACE "\n" "\r\n" _crlf_body "${_body}")
file(WRITE "${_work}/crlf_source.cpp" "${_crlf_body}")

# UTF-8 non-ASCII identifier (Japanese class name).
file(
	WRITE
	"${_work}/utf8_source.cpp"
	"class Café { public: int タ(int x) { return x + 1; } };\nint sink(Café& c) { return c.タ(2); }\n"
)

# Minimal compile_commands.json covering all three.
file(WRITE "${_work}/compile_commands.json" "[\n")
set(_first TRUE)
foreach(_name bom_source crlf_source utf8_source)
	if(_first)
		set(_first FALSE)
	else()
		file(APPEND "${_work}/compile_commands.json" ",\n")
	endif()
	file(
		APPEND
		"${_work}/compile_commands.json"
		"  {\n    \"directory\": \"${_work}\",\n    \"command\": \"${CMAKE_CXX_COMPILER} -std=c++20 -c ${_name}.cpp\",\n    \"file\": \"${_work}/${_name}.cpp\"\n  }"
	)
endforeach()
file(APPEND "${_work}/compile_commands.json" "\n]\n")

function(run_encoding_case source method)
	execute_process(
		COMMAND
			"${AZTECA_EXECUTABLE}" inspect -p "${_work}" --source "${_work}/${source}"
			--method "${method}" --format json
		RESULT_VARIABLE _result
		OUTPUT_VARIABLE _stdout
		ERROR_VARIABLE _stderr
	)
	if(NOT _result EQUAL 0)
		message(
			FATAL_ERROR
			"encoding case ${source} ${method} failed (exit=${_result}):\n${_stderr}\n${_stdout}"
		)
	endif()
	string(JSON _schema GET "${_stdout}" schema_version)
	if(NOT _schema STREQUAL "2")
		message(FATAL_ERROR "encoding case ${source} schema_version=${_schema}")
	endif()
	string(JSON _phase GET "${_stdout}" azteca_phase)
	if(NOT _phase STREQUAL "A")
		message(FATAL_ERROR "encoding case ${source} azteca_phase=${_phase}")
	endif()
endfunction()

run_encoding_case("bom_source.cpp" "C::m(int)")
run_encoding_case("crlf_source.cpp" "C::m(int)")
run_encoding_case("utf8_source.cpp" "Café::タ(int)")

message(STATUS "Phase A encoding robustness: OK")
