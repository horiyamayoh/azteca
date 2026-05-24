if(NOT DEFINED AZTECA_EXECUTABLE)
	message(FATAL_ERROR "AZTECA_EXECUTABLE is required")
endif()

if(NOT DEFINED PROJECT_SOURCE_DIR)
	message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

if(NOT DEFINED PROJECT_BINARY_DIR)
	message(FATAL_ERROR "PROJECT_BINARY_DIR is required")
endif()

set(required_keys
	schema_version
	azteca_phase
	target
	result
	confidence
	receiver_state
	dependency_observations
	observable_effects
	operations
	recursive_helper_candidates
	shape_candidates
	object_ref_requirements
	semantic_features
	unsupported_or_modeled_constructs
	control_flow_summary
	envelope_requirements
	rule_coverage
	paths
	gtest_preview
	diagnostics
)

set(result_enum extracted extracted-with-conservative-notes invalid-plan)
set(confidence_enum high medium low)
set(field_access_enum read write read-write address)
set(dependency_kind_enum query effect operation recursive-candidate)
set(construct_handling_enum
	supported
	modeled
	boundary
	conservative
	not-yet-implemented
	not-meaningful
)
set(envelope_requirement_kind_enum
	self-state
	base-state
	addressable-cell
	object-ref
	dependency-boundary
	dispatch-table
	type-tag
	lifetime-state
	byte-view
	global-environment
	exception-model
	macro-source-map
)
set(path_required_envelope_enum ${envelope_requirement_kind_enum} conservative-control-flow)
set(diagnostic_severity_enum info warning error)
set(certainty_enum certain heuristic conservative)

if(DEFINED CMAKE_CXX_COMPILER AND NOT CMAKE_CXX_COMPILER STREQUAL "")
	set(fixture_cxx_compiler "${CMAKE_CXX_COMPILER}")
else()
	set(fixture_cxx_compiler "clang++-18")
endif()

function(require_json json_text label)
	string(JSON _value ERROR_VARIABLE _error GET "${json_text}" ${ARGN})
	if(_error)
		message(FATAL_ERROR "${label}: missing JSON path '${ARGN}': ${_error}")
	endif()
endfunction()

function(assert_json_enum json_text label allowed_values_variable)
	string(JSON _value ERROR_VARIABLE _error GET "${json_text}" ${ARGN})
	if(_error)
		message(FATAL_ERROR "${label}: missing enum path '${ARGN}': ${_error}")
	endif()

	set(_allowed_values ${${allowed_values_variable}})
	list(FIND _allowed_values "${_value}" _found_index)
	if(_found_index EQUAL -1)
		message(
			FATAL_ERROR
			"${label}: '${ARGN}' value '${_value}' is not in ${allowed_values_variable}: ${_allowed_values}"
		)
	endif()
endfunction()

function(assert_json_const json_text label expected_value)
	string(JSON _value ERROR_VARIABLE _error GET "${json_text}" ${ARGN})
	if(_error)
		message(FATAL_ERROR "${label}: missing const path '${ARGN}': ${_error}")
	endif()
	if(NOT _value STREQUAL "${expected_value}")
		message(
			FATAL_ERROR
			"${label}: '${ARGN}' expected '${expected_value}' but got '${_value}'"
		)
	endif()
endfunction()

function(assert_json_non_negative_integer json_text label)
	string(JSON _value ERROR_VARIABLE _error GET "${json_text}" ${ARGN})
	if(_error)
		message(FATAL_ERROR "${label}: missing integer path '${ARGN}': ${_error}")
	endif()
	if(_value LESS 0)
		message(FATAL_ERROR "${label}: '${ARGN}' must be non-negative, got ${_value}")
	endif()
endfunction()

function(json_array_length out_variable json_text label)
	string(JSON _length ERROR_VARIABLE _error LENGTH "${json_text}" ${ARGN})
	if(_error)
		message(FATAL_ERROR "${label}: '${ARGN}' must be an array: ${_error}")
	endif()
	set(${out_variable} "${_length}" PARENT_SCOPE)
endfunction()

function(validate_source_location json_text label)
	require_json("${json_text}" "${label}" ${ARGN} file)
	assert_json_non_negative_integer("${json_text}" "${label}" ${ARGN} line)
	assert_json_non_negative_integer("${json_text}" "${label}" ${ARGN} column)
endfunction()

function(validate_source_range json_text label)
	validate_source_location("${json_text}" "${label}" ${ARGN} begin)
	validate_source_location("${json_text}" "${label}" ${ARGN} end)
endfunction()

function(validate_rule_id json_text label)
	string(JSON _rule_id ERROR_VARIABLE _error GET "${json_text}" ${ARGN})
	if(_error)
		message(FATAL_ERROR "${label}: missing rule_id at '${ARGN}': ${_error}")
	endif()
	if(NOT _rule_id MATCHES "^(LR|DEP|PATH|CALL|MMIR|SHAPE)-[A-Z0-9][A-Z0-9-]*$")
		message(FATAL_ERROR "${label}: invalid rule_id '${_rule_id}' at '${ARGN}'")
	endif()
endfunction()

function(validate_evidence json_text label)
	validate_rule_id("${json_text}" "${label}" ${ARGN} rule_id)
	require_json("${json_text}" "${label}" ${ARGN} reason)
	assert_json_enum("${json_text}" "${label}" certainty_enum ${ARGN} certainty)
	require_json("${json_text}" "${label}" ${ARGN} conservative)
	validate_source_range("${json_text}" "${label}" ${ARGN} source_range)
endfunction()

function(validate_string_array json_text label)
	json_array_length(_length "${json_text}" "${label}" ${ARGN})
	if(_length GREATER 0)
		math(EXPR _last_index "${_length} - 1")
		foreach(_index RANGE 0 ${_last_index})
			require_json("${json_text}" "${label}" ${ARGN} ${_index})
		endforeach()
	endif()
endfunction()

function(validate_port_array json_text label array_key expected_kind)
	json_array_length(_length "${json_text}" "${label}" ${array_key})
	if(_length GREATER 0)
		math(EXPR _last_index "${_length} - 1")
		foreach(_index RANGE 0 ${_last_index})
			assert_json_const("${json_text}" "${label}" "${expected_kind}" ${array_key} ${_index} kind)
			require_json("${json_text}" "${label}" ${array_key} ${_index} name)
			require_json("${json_text}" "${label}" ${array_key} ${_index} original_callee)
			require_json("${json_text}" "${label}" ${array_key} ${_index} return_type)
			validate_string_array("${json_text}" "${label}" ${array_key} ${_index} argument_types)
			validate_source_location("${json_text}" "${label}" ${array_key} ${_index} location)
			validate_evidence("${json_text}" "${label}" ${array_key} ${_index})
		endforeach()
	endif()
endfunction()

function(validate_phase_a_json json_text label)
	foreach(key IN LISTS required_keys)
		require_json("${json_text}" "${label}" ${key})
	endforeach()

	assert_json_const("${json_text}" "${label}" "2" schema_version)
	assert_json_const("${json_text}" "${label}" "A" azteca_phase)
	assert_json_enum("${json_text}" "${label}" result_enum result)
	assert_json_enum("${json_text}" "${label}" confidence_enum confidence)

	require_json("${json_text}" "${label}" target qualified_name)
	require_json("${json_text}" "${label}" target signature)
	require_json("${json_text}" "${label}" target source_file)
	assert_json_non_negative_integer("${json_text}" "${label}" target line)

	json_array_length(_receiver_count "${json_text}" "${label}" receiver_state)
	if(_receiver_count GREATER 0)
		math(EXPR _last_receiver "${_receiver_count} - 1")
		foreach(_index RANGE 0 ${_last_receiver})
			require_json("${json_text}" "${label}" receiver_state ${_index} name)
			require_json("${json_text}" "${label}" receiver_state ${_index} type)
			assert_json_enum(
				"${json_text}" "${label}" field_access_enum receiver_state ${_index} access
			)
			require_json("${json_text}" "${label}" receiver_state ${_index} mutable)
			require_json("${json_text}" "${label}" receiver_state ${_index} access_specifier)
			validate_source_location("${json_text}" "${label}" receiver_state ${_index} location)
			validate_evidence("${json_text}" "${label}" receiver_state ${_index})
		endforeach()
	endif()

	validate_port_array("${json_text}" "${label}" dependency_observations query)
	validate_port_array("${json_text}" "${label}" observable_effects effect)
	validate_port_array("${json_text}" "${label}" operations operation)
	validate_port_array("${json_text}" "${label}" recursive_helper_candidates recursive-candidate)

	json_array_length(_shape_count "${json_text}" "${label}" shape_candidates)
	if(_shape_count GREATER 0)
		math(EXPR _last_shape "${_shape_count} - 1")
		foreach(_index RANGE 0 ${_last_shape})
			require_json("${json_text}" "${label}" shape_candidates ${_index} name)
			require_json("${json_text}" "${label}" shape_candidates ${_index} source_dependency)
			validate_string_array("${json_text}" "${label}" shape_candidates ${_index} observed_members)
			validate_evidence("${json_text}" "${label}" shape_candidates ${_index})
		endforeach()
	endif()

	json_array_length(_object_ref_count "${json_text}" "${label}" object_ref_requirements)
	if(_object_ref_count GREATER 0)
		math(EXPR _last_object_ref "${_object_ref_count} - 1")
		foreach(_index RANGE 0 ${_last_object_ref})
			require_json("${json_text}" "${label}" object_ref_requirements ${_index} requirement_reason)
			require_json("${json_text}" "${label}" object_ref_requirements ${_index} expression)
			validate_source_location(
				"${json_text}" "${label}" object_ref_requirements ${_index} location
			)
			validate_evidence("${json_text}" "${label}" object_ref_requirements ${_index})
		endforeach()
	endif()

	json_array_length(_feature_count "${json_text}" "${label}" semantic_features)
	if(_feature_count GREATER 0)
		math(EXPR _last_feature "${_feature_count} - 1")
		foreach(_index RANGE 0 ${_last_feature})
			require_json("${json_text}" "${label}" semantic_features ${_index} name)
			assert_json_enum(
				"${json_text}" "${label}" construct_handling_enum semantic_features ${_index} handling
			)
			require_json("${json_text}" "${label}" semantic_features ${_index} detail)
			validate_evidence("${json_text}" "${label}" semantic_features ${_index})
		endforeach()
	endif()

	json_array_length(_construct_count "${json_text}" "${label}" unsupported_or_modeled_constructs)
	if(_construct_count GREATER 0)
		math(EXPR _last_construct "${_construct_count} - 1")
		foreach(_index RANGE 0 ${_last_construct})
			require_json("${json_text}" "${label}" unsupported_or_modeled_constructs ${_index} construct)
			assert_json_enum(
				"${json_text}" "${label}" construct_handling_enum
				unsupported_or_modeled_constructs ${_index} handling
			)
			require_json(
				"${json_text}" "${label}" unsupported_or_modeled_constructs ${_index}
				construct_reason
			)
			validate_string_array(
				"${json_text}" "${label}" unsupported_or_modeled_constructs ${_index} fallbacks
			)
			validate_source_location(
				"${json_text}" "${label}" unsupported_or_modeled_constructs ${_index} location
			)
			validate_evidence("${json_text}" "${label}" unsupported_or_modeled_constructs ${_index})
		endforeach()
	endif()

	foreach(_key has_if has_switch has_loop has_range_for has_try has_throw has_return conservative)
		require_json("${json_text}" "${label}" control_flow_summary ${_key})
	endforeach()
	validate_string_array("${json_text}" "${label}" control_flow_summary conservative_reasons)

	json_array_length(_envelope_count "${json_text}" "${label}" envelope_requirements)
	if(_envelope_count GREATER 0)
		math(EXPR _last_envelope "${_envelope_count} - 1")
		foreach(_index RANGE 0 ${_last_envelope})
			assert_json_enum(
				"${json_text}" "${label}" envelope_requirement_kind_enum
				envelope_requirements ${_index} kind
			)
			require_json("${json_text}" "${label}" envelope_requirements ${_index} requirement_reason)
			require_json("${json_text}" "${label}" envelope_requirements ${_index} source)
			validate_evidence("${json_text}" "${label}" envelope_requirements ${_index})
		endforeach()
	endif()

	json_array_length(_coverage_count "${json_text}" "${label}" rule_coverage)
	if(_coverage_count GREATER 0)
		math(EXPR _last_coverage "${_coverage_count} - 1")
		foreach(_index RANGE 0 ${_last_coverage})
			validate_rule_id("${json_text}" "${label}" rule_coverage ${_index} rule_id)
			assert_json_enum(
				"${json_text}" "${label}" construct_handling_enum rule_coverage ${_index} handling
			)
			require_json("${json_text}" "${label}" rule_coverage ${_index} note)
			require_json("${json_text}" "${label}" rule_coverage ${_index} observed)
		endforeach()
	endif()

	json_array_length(_path_count "${json_text}" "${label}" paths)
	if(_path_count GREATER 0)
		math(EXPR _last_path "${_path_count} - 1")
		foreach(_index RANGE 0 ${_last_path})
			require_json("${json_text}" "${label}" paths ${_index} name)
			validate_string_array("${json_text}" "${label}" paths ${_index} observations)
			validate_string_array("${json_text}" "${label}" paths ${_index} effects)
			validate_string_array("${json_text}" "${label}" paths ${_index} operations)
			validate_string_array("${json_text}" "${label}" paths ${_index} loop_body_observations)
			json_array_length(_required_count "${json_text}" "${label}" paths ${_index} required_envelopes)
			if(_required_count GREATER 0)
				math(EXPR _last_required "${_required_count} - 1")
				foreach(_required_index RANGE 0 ${_last_required})
					assert_json_enum(
						"${json_text}" "${label}" path_required_envelope_enum
						paths ${_index} required_envelopes ${_required_index}
					)
				endforeach()
			endif()
			require_json("${json_text}" "${label}" paths ${_index} conservative_reason)
			validate_evidence("${json_text}" "${label}" paths ${_index})
		endforeach()
	endif()

	require_json("${json_text}" "${label}" gtest_preview sample_test_path)
	validate_string_array("${json_text}" "${label}" gtest_preview lines)

	json_array_length(_diagnostic_count "${json_text}" "${label}" diagnostics)
	if(_diagnostic_count GREATER 0)
		math(EXPR _last_diagnostic "${_diagnostic_count} - 1")
		foreach(_index RANGE 0 ${_last_diagnostic})
			assert_json_enum(
				"${json_text}" "${label}" diagnostic_severity_enum diagnostics ${_index} severity
			)
			require_json("${json_text}" "${label}" diagnostics ${_index} code)
			require_json("${json_text}" "${label}" diagnostics ${_index} message)
			validate_source_location("${json_text}" "${label}" diagnostics ${_index} location)
		endforeach()
	endif()
endfunction()

file(GLOB golden_files "${PROJECT_SOURCE_DIR}/tests/golden/phase_a/*.inspect.json")
if(NOT golden_files)
	message(FATAL_ERROR "no Phase A JSON goldens found")
endif()

foreach(golden_path IN LISTS golden_files)
	file(READ "${golden_path}" golden_text)
	validate_phase_a_json("${golden_text}" "golden ${golden_path}")
endforeach()

set(fixture_source "${PROJECT_SOURCE_DIR}/tests/fixtures/phase_a/simple")
set(fixture_build "${PROJECT_BINARY_DIR}/test-work/phase_a/simple")

if(NOT EXISTS "${fixture_build}/compile_commands.json")
	execute_process(
		COMMAND
			${CMAKE_COMMAND}
			-S "${fixture_source}"
			-B "${fixture_build}"
			-G Ninja
			-DCMAKE_CXX_COMPILER=${fixture_cxx_compiler}
			-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
		RESULT_VARIABLE cfg_result
		OUTPUT_VARIABLE cfg_output
		ERROR_VARIABLE cfg_error
	)
	if(NOT cfg_result EQUAL 0)
		message(FATAL_ERROR "fixture configure failed:\n${cfg_output}\n${cfg_error}")
	endif()
endif()

execute_process(
	COMMAND
		"${AZTECA_EXECUTABLE}" inspect
		-p "${fixture_build}"
		--source "${fixture_source}/service.cpp"
		--method "Service::handle(Id)"
		--format json
	RESULT_VARIABLE live_result
	OUTPUT_VARIABLE live_output
	ERROR_VARIABLE live_error
)

if(NOT live_result EQUAL 0)
	message(FATAL_ERROR "live inspect failed:\n${live_output}\n${live_error}")
endif()

validate_phase_a_json("${live_output}" "live azteca inspect (Service::handle)")

message(STATUS "Phase A schema_v2 contract: OK")
