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
		"Semantic features:"
		"Envelope requirements:"
		"Control flow summary:"
		"Path-wise test burden:"
		"required envelope: dependency_boundary"
		"Rule coverage:"
		"LR-001 [supported] observed"
		"observations: repo_exists, policy_allow"
		"effects: notifier_send"
		"Google Test preview:")
	assert_contains("${inspect_text_output}" "${required_line}" "text inspect output")
endforeach()

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/service.cpp" --method "Service::handle(Id)" --format text --verbose
	RESULT_VARIABLE inspect_verbose_result
	OUTPUT_VARIABLE inspect_verbose_output
	ERROR_VARIABLE inspect_verbose_error
)

if(NOT inspect_verbose_result EQUAL 0)
	message(FATAL_ERROR "verbose inspect failed:\n${inspect_verbose_output}\n${inspect_verbose_error}")
endif()

foreach(required_verbose_line
		"rule: LR-001 - receiver field read"
		"rule: DEP-MEMBER-QUERY - const non-void member object call is a query"
		"rule: PATH-DFS - return path accumulated by DFS")
	assert_contains("${inspect_verbose_output}" "${required_verbose_line}" "verbose inspect output")
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
		"\"schema_version\": 2"
		"\"target\""
		"\"receiver_state\""
		"\"dependency_observations\""
		"\"observable_effects\""
		"\"operations\""
		"\"shape_candidates\""
		"\"object_ref_requirements\""
		"\"semantic_features\""
		"\"unsupported_or_modeled_constructs\""
		"\"control_flow_summary\""
		"\"envelope_requirements\""
		"\"rule_coverage\""
		"\"confidence\""
		"\"paths\""
		"\"gtest_preview\""
		"\"diagnostics\""
		"\"rule_id\""
		"\"source_range\""
		"\"certainty\""
		"\"conservative\"")
	assert_contains("${inspect_json_output}" "${required_json}" "json inspect output")
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
	assert_contains("${inspect_shape_output}" "${required_shape_line}" "shape inspect output")
endforeach()

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/hardening.cpp" --method "FieldWriteExample::update(int)" --format text
	RESULT_VARIABLE inspect_field_write_result
	OUTPUT_VARIABLE inspect_field_write_output
	ERROR_VARIABLE inspect_field_write_error
)

if(NOT inspect_field_write_result EQUAL 0)
	message(FATAL_ERROR "field write inspect failed:\n${inspect_field_write_output}\n${inspect_field_write_error}")
endif()

foreach(required_field_write_line "int count_ read/write" "Path-wise test burden:")
	assert_contains("${inspect_field_write_output}" "${required_field_write_line}" "field write inspect output")
endforeach()

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/hardening.cpp" --method "HelperExample::run(int)" --format text
	RESULT_VARIABLE inspect_helper_result
	OUTPUT_VARIABLE inspect_helper_output
	ERROR_VARIABLE inspect_helper_error
)

if(NOT inspect_helper_result EQUAL 0)
	message(FATAL_ERROR "helper inspect failed:\n${inspect_helper_output}\n${inspect_helper_error}")
endif()

foreach(required_helper_line "Recursive helper candidates:" "recursive normalize(int) -> int")
	assert_contains("${inspect_helper_output}" "${required_helper_line}" "helper inspect output")
endforeach()

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/hardening.cpp" --method "DependencyKinds::run(int)" --format text
	RESULT_VARIABLE inspect_dependency_result
	OUTPUT_VARIABLE inspect_dependency_output
	ERROR_VARIABLE inspect_dependency_error
)

if(NOT inspect_dependency_result EQUAL 0)
	message(FATAL_ERROR "dependency inspect failed:\n${inspect_dependency_output}\n${inspect_dependency_error}")
endif()

foreach(required_dependency_line
		"query repo_exists(int) -> bool"
		"effect notifier_send(int)"
		"operation repo_refresh(int) -> int")
	assert_contains("${inspect_dependency_output}" "${required_dependency_line}" "dependency inspect output")
endforeach()

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/hardening.cpp" --method "EscapeExample::link()" --format text
	RESULT_VARIABLE inspect_escape_result
	OUTPUT_VARIABLE inspect_escape_output
	ERROR_VARIABLE inspect_escape_error
)

if(NOT inspect_escape_result EQUAL 0)
	message(FATAL_ERROR "escape inspect failed:\n${inspect_escape_output}\n${inspect_escape_error}")
endif()

foreach(required_escape_line
		"Object identity requirements:"
		"this passed to dependency"
		"effect publish(EscapeExample *)")
	assert_contains("${inspect_escape_output}" "${required_escape_line}" "escape inspect output")
endforeach()

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/hardening.cpp" --method "LoopExample::run(int)" --format text
	RESULT_VARIABLE inspect_loop_result
	OUTPUT_VARIABLE inspect_loop_output
	ERROR_VARIABLE inspect_loop_error
)

if(NOT inspect_loop_result EQUAL 0)
	message(FATAL_ERROR "loop inspect failed:\n${inspect_loop_output}\n${inspect_loop_error}")
endif()

foreach(required_loop_line
		"Extraction result: extracted-with-conservative-notes"
		"AZTECA_PATH_CONSERVATIVE"
		"observations: repo_exists"
		"effects: notifier_send")
	assert_contains("${inspect_loop_output}" "${required_loop_line}" "loop inspect output")
endforeach()

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/hardening.cpp" --method "BitFieldExample::read() const" --format text
	RESULT_VARIABLE inspect_bitfield_result
	OUTPUT_VARIABLE inspect_bitfield_output
	ERROR_VARIABLE inspect_bitfield_error
)

if(NOT inspect_bitfield_result EQUAL 0)
	message(FATAL_ERROR "bit-field inspect failed:\n${inspect_bitfield_output}\n${inspect_bitfield_error}")
endif()

foreach(required_bitfield_line
		"Extraction result: extracted-with-conservative-notes"
		"AZTECA_BIT_FIELD_PARTIAL")
	assert_contains("${inspect_bitfield_output}" "${required_bitfield_line}" "bit-field inspect output")
endforeach()

function(assert_syntax_inspect method)
	execute_process(
		COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
				"${fixture_source}/syntax_matrix.cpp" --method "${method}" --format text
		RESULT_VARIABLE syntax_result
		OUTPUT_VARIABLE syntax_output
		ERROR_VARIABLE syntax_error
	)

	if(NOT syntax_result EQUAL 0)
		message(FATAL_ERROR "syntax inspect failed for ${method}:\n${syntax_output}\n${syntax_error}")
	endif()

	foreach(required_line IN LISTS ARGN)
		assert_contains("${syntax_output}" "${required_line}" "syntax inspect output for ${method}")
	endforeach()
endfunction()

assert_syntax_inspect(
	"SyntaxMatrix::base_global_static(int)"
	"base_state [modeled]"
	"global_state [modeled]"
	"LR-008 [boundary] observed"
	"LR-010 [modeled] observed"
	"LR-011 [modeled] observed"
)

assert_syntax_inspect(
	"SyntaxMatrix::dispatch(int) const"
	"virtual_dispatch [modeled]"
	"dispatch_table: virtual call requires dispatch table"
	"LR-012 [modeled] observed"
)

assert_syntax_inspect(
	"SyntaxMatrix::operator_path()"
	"overloaded_operator [boundary]"
	"s.when.op_operator"
	"LR-013 [boundary] observed"
)

assert_syntax_inspect(
	"SyntaxMatrix::lambda_run(int)"
	"lambda [modeled]"
	"lambda_call [supported]"
	"LR-018 [modeled] observed"
)

assert_syntax_inspect(
	"SyntaxMatrix::exception_run(int)"
	"try_catch [conservative]"
	"exception_throw [supported]"
	"LR-035 [supported] observed"
	"LR-036 [conservative] observed"
)

assert_syntax_inspect(
	"SyntaxMatrix::switch_loop(int)"
	"switch_control_flow [conservative]"
	"loop_control_flow [conservative]"
	"LR-015 [conservative] observed"
	"LR-016 [conservative] observed"
)

assert_syntax_inspect(
	"SyntaxMatrix::identity_and_type(MatrixBase *)"
	"object_identity [modeled]"
	"dynamic_type [modeled]"
	"type_info [modeled]"
	"LR-020 [modeled] observed"
	"LR-023 [modeled] observed"
	"LR-024 [modeled] observed"
)

assert_syntax_inspect(
	"SyntaxMatrix::first_byte()"
	"byte_representation [boundary]"
	"byte_view: byte representation must not fake product object layout"
	"LR-025 [boundary] observed"
)

assert_syntax_inspect(
	"SyntaxMatrix::release()"
	"lifetime_operation [modeled]"
	"delete expression [modeled]"
	"LR-026 [modeled] observed"
)

assert_syntax_inspect(
	"SyntaxMatrix::reset_in_place()"
	"constructor_call [conservative]"
	"explicit destructor call [modeled]"
	"placement new on this [modeled]"
	"LR-027 [modeled] observed"
	"LR-028 [modeled] observed"
	"LR-049 [conservative] observed"
)

assert_syntax_inspect(
	"SyntaxMatrix::structured()"
	"structured_binding [conservative]"
	"LR-037 [conservative] observed"
)

assert_syntax_inspect(
	"SyntaxMatrix::macro_use()"
	"macro_expansion [conservative]"
	"LR-034 [conservative] observed"
)

function(assert_edge_inspect method)
	execute_process(
		COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
				"${fixture_source}/edge_cases.cpp" --method "${method}" --format text
		RESULT_VARIABLE edge_result
		OUTPUT_VARIABLE edge_output
		ERROR_VARIABLE edge_error
	)

	if(NOT edge_result EQUAL 0)
		message(FATAL_ERROR "edge inspect failed for ${method}:\n${edge_output}\n${edge_error}")
	endif()

	foreach(required_line IN LISTS ARGN)
		assert_contains("${edge_output}" "${required_line}" "edge inspect output for ${method}")
	endforeach()
endfunction()

assert_edge_inspect(
	"EdgeCases::explicit_this_read() const"
	"explicit this receiver field read"
	"access_control [modeled]"
	"LR-002 [supported] observed"
	"LR-048 [modeled] observed"
)

assert_edge_inspect(
	"EdgeCases::default_argument(int)"
	"default_argument [supported]"
	"query dependency_value(int, int) -> int"
	"LR-042 [supported] observed"
)

assert_edge_inspect(
	"EdgeCases::overloads(int)"
	"recursive helper__int(int) -> int"
	"recursive helper__double(double) -> int"
	"LR-007 [supported] observed"
)

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/edge_cases.cpp" --method
			"EdgeCases::value_category_and_casts(const int *)" --format text
	RESULT_VARIABLE edge_cast_result
	OUTPUT_VARIABLE edge_cast_output
	ERROR_VARIABLE edge_cast_error
)

if(NOT edge_cast_result EQUAL 0)
	message(FATAL_ERROR "edge cast inspect failed:\n${edge_cast_output}\n${edge_cast_error}")
endif()

foreach(required_cast_line
		"value_category_cast [supported]"
		"cast_expression [supported]"
		"const_cast [boundary]"
		"LR-044 [supported] observed"
		"LR-045 [boundary] observed")
	assert_contains("${edge_cast_output}" "${required_cast_line}" "edge cast inspect output")
endforeach()
assert_not_contains("${edge_cast_output}" "std_move" "edge cast inspect output")

assert_edge_inspect(
	"EdgeCases::branch_controls(int)"
	"break_continue [supported]"
	"loop_control_flow [conservative]"
	"LR-046 [supported] observed"
)

assert_edge_inspect(
	"EdgeCases::ternary(bool) const"
	"conditional_operator [supported]"
	"LR-041 [supported] observed"
)

assert_edge_inspect(
	"EdgeCases::cv_ref() const volatile &"
	"method_qualifier [supported]: target method qualifiers: const volatile &"
	"LR-047 [supported] observed"
)

assert_edge_inspect(
	"outer::Container::Inner::run() const"
	"Azteca can inspect outer::Container::Inner::run."
	"access_control [modeled]"
)

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/syntax_matrix.cpp" --method "SyntaxMatrix::operator+(int)" --format text
	RESULT_VARIABLE operator_spec_result
	OUTPUT_VARIABLE operator_spec_output
	ERROR_VARIABLE operator_spec_error
)

if(NOT operator_spec_result EQUAL 1)
	message(
		FATAL_ERROR
		"operator spec should exit 1 but exited ${operator_spec_result}:\n"
		"${operator_spec_output}\n${operator_spec_error}"
	)
endif()
assert_contains("${operator_spec_error}" "operator methods are not supported" "operator spec stderr")

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
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --method
			"AmbiguousTarget::run(int)" --format text
	RESULT_VARIABLE ambiguous_result
	OUTPUT_VARIABLE ambiguous_output
	ERROR_VARIABLE ambiguous_error
)

if(NOT ambiguous_result EQUAL 3)
	message(
		FATAL_ERROR
		"ambiguous method should exit 3 but exited ${ambiguous_result}:\n"
		"${ambiguous_output}\n${ambiguous_error}"
	)
endif()
assert_contains("${ambiguous_error}" "AZTECA_METHOD_AMBIGUOUS" "ambiguous method stderr")

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/hardening.cpp" --method "StaticExample::target(int)" --format text
	RESULT_VARIABLE static_result
	OUTPUT_VARIABLE static_output
	ERROR_VARIABLE static_error
)

if(NOT static_result EQUAL 3)
	message(
		FATAL_ERROR
		"static method should exit 3 but exited ${static_result}:\n"
		"${static_output}\n${static_error}"
	)
endif()
assert_contains("${static_error}" "AZTECA_STATIC_METHOD" "static method stderr")

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/hardening.cpp" --method "TemplateExample::target(T)" --format text
	RESULT_VARIABLE template_result
	OUTPUT_VARIABLE template_output
	ERROR_VARIABLE template_error
)

if(NOT template_result EQUAL 3)
	message(
		FATAL_ERROR
		"template method should exit 3 but exited ${template_result}:\n"
		"${template_output}\n${template_error}"
	)
endif()
assert_contains("${template_error}" "AZTECA_TEMPLATE_METHOD" "template method stderr")

execute_process(
	COMMAND "${AZTECA_EXECUTABLE}" inspect -p "${fixture_build}" --source
			"${fixture_source}/hardening.cpp" --method "DeclarationOnly::missing_body(int)" --format text
	RESULT_VARIABLE missing_body_result
	OUTPUT_VARIABLE missing_body_output
	ERROR_VARIABLE missing_body_error
)

if(NOT missing_body_result EQUAL 3)
	message(
		FATAL_ERROR
		"missing body should exit 3 but exited ${missing_body_result}:\n"
		"${missing_body_output}\n${missing_body_error}"
	)
endif()
assert_contains("${missing_body_error}" "AZTECA_METHOD_DECL_ONLY" "missing body stderr")

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
