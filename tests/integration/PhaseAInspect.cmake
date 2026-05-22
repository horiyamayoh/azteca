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

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/service.cpp" --method "Service::handle(Id)" --format text
	RESULT_VARIABLE inspect_text_result
	OUTPUT_VARIABLE inspect_text_output
	ERROR_VARIABLE inspect_text_error
)

if(NOT inspect_text_result EQUAL 0)
	message(FATAL_ERROR "text inspect failed:\n${inspect_text_output}\n${inspect_text_error}")
endif()

file(READ "${PROJECT_SOURCE_DIR}/tests/golden/phase_a/service_handle.inspect.txt" expected_text_output)
if(NOT inspect_text_output STREQUAL expected_text_output)
	message(FATAL_ERROR "text inspect output differed from golden:\n${inspect_text_output}")
endif()

foreach(required_line
		"Receiver state:"
		"bool enabled_ read"
		"query repo_exists(Id) -> bool"
		"query policy_allow(Id) -> bool"
		"effect notifier_send(Id)"
		"Path-wise test burden:"
		"observations: repo_exists, policy_allow"
		"effects: notifier_send"
		"Google Test preview:")
	string(FIND "${inspect_text_output}" "${required_line}" required_index)
	if(required_index EQUAL -1)
		message(FATAL_ERROR "text inspect output did not contain '${required_line}':\n${inspect_text_output}")
	endif()
endforeach()

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/service.cpp" --method "Service::handle(Id)" --format json
	RESULT_VARIABLE inspect_json_result
	OUTPUT_VARIABLE inspect_json_output
	ERROR_VARIABLE inspect_json_error
)

if(NOT inspect_json_result EQUAL 0)
	message(FATAL_ERROR "json inspect failed:\n${inspect_json_output}\n${inspect_json_error}")
endif()

string(REPLACE "${fixture_source}" "<fixture>" normalized_json_output "${inspect_json_output}")
file(READ "${PROJECT_SOURCE_DIR}/tests/golden/phase_a/service_handle.inspect.json" expected_json_output)
string(REGEX REPLACE "[ \t\r\n]" "" normalized_json_compact "${normalized_json_output}")
string(REGEX REPLACE "[ \t\r\n]" "" expected_json_compact "${expected_json_output}")
if(NOT normalized_json_compact STREQUAL expected_json_compact)
	message(FATAL_ERROR "json inspect output differed from golden:\n${normalized_json_output}")
endif()

foreach(required_json
		"\"schema_version\": 1"
		"\"target\""
		"\"receiver_state\""
		"\"dependency_observations\""
		"\"observable_effects\""
		"\"operations\""
		"\"shape_candidates\""
		"\"object_ref_requirements\""
		"\"paths\""
		"\"gtest_preview\""
		"\"diagnostics\"")
	string(FIND "${inspect_json_output}" "${required_json}" required_json_index)
	if(required_json_index EQUAL -1)
		message(FATAL_ERROR "json inspect output did not contain '${required_json}':\n${inspect_json_output}")
	endif()
endforeach()

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/order_service.cpp" --method "OrderService::check(int)" --format text
	RESULT_VARIABLE inspect_shape_result
	OUTPUT_VARIABLE inspect_shape_output
	ERROR_VARIABLE inspect_shape_error
)

if(NOT inspect_shape_result EQUAL 0)
	message(FATAL_ERROR "shape inspect failed:\n${inspect_shape_output}\n${inspect_shape_error}")
endif()

foreach(required_shape_line
		"Generated shapes:"
		"OrderShape"
		"deadline"
		"amount")
	string(FIND "${inspect_shape_output}" "${required_shape_line}" required_shape_index)
	if(required_shape_index EQUAL -1)
		message(
			FATAL_ERROR
			"shape inspect output did not contain '${required_shape_line}':\n${inspect_shape_output}"
		)
	endif()
endforeach()

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/service.cpp" --method "Service::missing()" --format text
	RESULT_VARIABLE missing_method_result
	OUTPUT_VARIABLE missing_method_output
	ERROR_VARIABLE missing_method_error
)

if(NOT missing_method_result EQUAL 3)
	message(
		FATAL_ERROR
		"missing method should exit 3 but exited ${missing_method_result}:\n"
		"${missing_method_output}\n${missing_method_error}"
	)
endif()

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${PROJECT_BINARY_DIR}/test-work/phase_a/missing"
			--method "Service::handle(Id)" --format text
	RESULT_VARIABLE missing_db_result
	OUTPUT_VARIABLE missing_db_output
	ERROR_VARIABLE missing_db_error
)

if(NOT missing_db_result EQUAL 2)
	message(
		FATAL_ERROR
		"missing compile db should exit 2 but exited ${missing_db_result}:\n"
		"${missing_db_output}\n${missing_db_error}"
	)
endif()
