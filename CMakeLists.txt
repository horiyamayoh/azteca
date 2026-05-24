cmake_minimum_required(VERSION 3.24)

project(azteca VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	message(FATAL_ERROR "Azteca must be built with Clang. Configure with CMakePresets.json.")
endif()

if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "18.0" OR NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19.0")
	message(
		FATAL_ERROR
		"Azteca requires Clang 18, but ${CMAKE_CXX_COMPILER} is ${CMAKE_CXX_COMPILER_VERSION}."
	)
endif()

function(require_tool_major_version tool_label tool_executable expected_major)
	execute_process(
		COMMAND ${tool_executable} --version
		OUTPUT_VARIABLE tool_version_output
		ERROR_VARIABLE tool_version_error
		RESULT_VARIABLE tool_version_result
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_STRIP_TRAILING_WHITESPACE
	)

	if(NOT tool_version_result EQUAL 0)
		message(
			FATAL_ERROR
			"Failed to run ${tool_label} at ${tool_executable}: ${tool_version_error}"
		)
	endif()

	string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" tool_version "${tool_version_output}")

	if(NOT tool_version)
		message(
			FATAL_ERROR
			"Could not parse ${tool_label} version from: ${tool_version_output}"
		)
	endif()

	math(EXPR expected_next_major "${expected_major} + 1")

	if(tool_version VERSION_LESS "${expected_major}.0" OR NOT tool_version VERSION_LESS "${expected_next_major}.0")
		message(
			FATAL_ERROR
			"Azteca requires ${tool_label} ${expected_major}.x, but ${tool_executable} is ${tool_version}."
		)
	endif()

	message(STATUS "Using ${tool_label}: ${tool_executable} (${tool_version})")
endfunction()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AZTECA_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(AZTECA_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

include(ProcessorCount)
ProcessorCount(AZTECA_PROCESSOR_COUNT)
if(AZTECA_PROCESSOR_COUNT EQUAL 0)
	set(AZTECA_PROCESSOR_COUNT 4)
endif()

if(AZTECA_PROCESSOR_COUNT LESS 4)
	set(AZTECA_DEFAULT_CLANG_TIDY_JOBS "${AZTECA_PROCESSOR_COUNT}")
else()
	set(AZTECA_DEFAULT_CLANG_TIDY_JOBS 4)
endif()

set(
	AZTECA_CLANG_TIDY_JOBS
	"${AZTECA_DEFAULT_CLANG_TIDY_JOBS}"
	CACHE STRING
	"Number of parallel clang-tidy jobs"
)

add_compile_options(
	-Wall
	-Wextra
	-Wpedantic
	-Wconversion
	-Wsign-conversion
	-Wshadow
	-Wnon-virtual-dtor
)

if(AZTECA_ENABLE_WARNINGS_AS_ERRORS)
	add_compile_options(-Werror)
endif()

if(AZTECA_ENABLE_SANITIZERS)
	add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
	add_link_options(-fsanitize=address,undefined)
endif()

include(CTest)

find_program(GIT_EXECUTABLE git REQUIRED)
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format-18 clang-format REQUIRED)
find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy-18 clang-tidy)
find_program(RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy-18 run-clang-tidy)
find_program(LLVM_CONFIG_EXECUTABLE NAMES llvm-config-18 llvm-config REQUIRED)
find_program(NPM_EXECUTABLE npm REQUIRED)

require_tool_major_version("clang-format" "${CLANG_FORMAT_EXECUTABLE}" 18)

if(CLANG_TIDY_EXECUTABLE)
	require_tool_major_version("clang-tidy" "${CLANG_TIDY_EXECUTABLE}" 18)
else()
	message(STATUS "clang-tidy 18 was not found; lint targets will fail until it is installed.")
endif()

execute_process(
	COMMAND ${LLVM_CONFIG_EXECUTABLE} --cmakedir
	OUTPUT_VARIABLE LLVM_CMAKE_DIR_FROM_CONFIG
	RESULT_VARIABLE LLVM_CONFIG_RESULT
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT LLVM_CONFIG_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to query LLVM CMake directory with ${LLVM_CONFIG_EXECUTABLE}")
endif()

set(LLVM_DIR "${LLVM_CMAKE_DIR_FROM_CONFIG}" CACHE PATH "LLVM CMake package directory" FORCE)
get_filename_component(LLVM_CMAKE_PACKAGE_ROOT "${LLVM_CMAKE_DIR_FROM_CONFIG}" DIRECTORY)
set(Clang_DIR "${LLVM_CMAKE_PACKAGE_ROOT}/clang" CACHE PATH "Clang CMake package directory" FORCE)

find_package(LLVM CONFIG REQUIRED)
find_package(Clang CONFIG REQUIRED)

if(NOT LLVM_VERSION_MAJOR EQUAL 18)
	message(FATAL_ERROR "Azteca requires LLVM 18, but found ${LLVM_PACKAGE_VERSION}.")
endif()

add_library(
	azteca_core
	src/collect/InspectCollector.cpp
	src/collect/PathAnalyzer.cpp
	src/frontend/InspectFrontend.cpp
	src/frontend/PlanBuilder.cpp
	src/frontend/PlanBuilderAst.cpp
	src/frontend/PlanBuilderDependencies.cpp
	src/frontend/PlanBuilderState.cpp
	src/frontend/InspectSource.cpp
	src/gtest/GTestPreview.cpp
	src/mmir/MMIR.cpp
	src/plan/ExtractionPlan.cpp
	src/plan/RuleCoverageRegistry.cpp
	src/report/DiagnosticCatalog.cpp
	src/report/InspectReport.cpp
	src/report/JsonWriter.cpp
	src/resolve/MethodSelector.cpp
	src/MethodSpec.cpp
)

target_include_directories(
	azteca_core
	PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_include_directories(
	azteca_core
	SYSTEM
	PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS}
)

target_compile_definitions(azteca_core PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(azteca_core PRIVATE clangTooling clangASTMatchers clangFrontend clangAST)

add_executable(azteca src/cli/main.cpp)
target_link_libraries(azteca PRIVATE azteca_core)

set(AZTECA_TEST_DEPENDENCIES azteca)

if(BUILD_TESTING)
	find_package(GTest CONFIG QUIET)

	add_test(
		NAME phase_a_inspect_fixture
		COMMAND
			${CMAKE_COMMAND}
			-DAZTECA_EXECUTABLE=$<TARGET_FILE:azteca>
			-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
			-DPROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}
			-P ${PROJECT_SOURCE_DIR}/tests/integration/PhaseAInspect.cmake
	)

	add_test(
		NAME phase_a_cli_surface
		COMMAND
			${CMAKE_COMMAND}
			-DAZTECA_EXECUTABLE=$<TARGET_FILE:azteca>
			-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
			-P ${PROJECT_SOURCE_DIR}/tests/integration/PhaseACliSurface.cmake
	)

	add_test(
		NAME phase_a_schema_v2_check
		COMMAND
			${CMAKE_COMMAND}
			-DAZTECA_EXECUTABLE=$<TARGET_FILE:azteca>
			-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
			-DPROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}
			-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
			-P ${PROJECT_SOURCE_DIR}/tests/contract/schema_v2/PhaseASchemaV2.cmake
	)

	find_program(AZTECA_NPM_EXECUTABLE NAMES npm)
	if(AZTECA_NPM_EXECUTABLE)
		add_test(
			NAME phase_a_schema_v2_validate
			COMMAND
				${AZTECA_NPM_EXECUTABLE} run --silent validate:phase-a-json
			WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
		)
	endif()

	add_test(
		NAME phase_a_encoding_robustness
		COMMAND
			${CMAKE_COMMAND}
			-DAZTECA_EXECUTABLE=$<TARGET_FILE:azteca>
			-DPROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}
			-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
			-P ${PROJECT_SOURCE_DIR}/tests/integration/PhaseAEncodingRobustness.cmake
	)

	add_test(
		NAME phase_a_perf_smoke
		COMMAND
			${CMAKE_COMMAND}
			-DAZTECA_EXECUTABLE=$<TARGET_FILE:azteca>
			-DPROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}
			-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
			-P ${PROJECT_SOURCE_DIR}/tests/perf/phase_a/PhaseAPerfSmoke.cmake
	)

	add_test(
		NAME phase_a_coverage_matrix
		COMMAND
			${CMAKE_COMMAND}
			-DAZTECA_EXECUTABLE=$<TARGET_FILE:azteca>
			-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
			-DPROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}
			-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
			-P ${PROJECT_SOURCE_DIR}/tests/integration/PhaseACoverageMatrix.cmake
	)

	add_test(
		NAME phase_a_coverage_observations
		COMMAND
			${CMAKE_COMMAND}
			-DAZTECA_EXECUTABLE=$<TARGET_FILE:azteca>
			-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
			-DPROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}
			-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
			-P ${PROJECT_SOURCE_DIR}/tests/integration/PhaseACoverageObservations.cmake
	)

	add_test(
		NAME phase_a_output_quality
		COMMAND
			${CMAKE_COMMAND}
			-DAZTECA_EXECUTABLE=$<TARGET_FILE:azteca>
			-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
			-DPROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}
			-P ${PROJECT_SOURCE_DIR}/tests/integration/PhaseAOutputQuality.cmake
	)

	add_test(
		NAME phase_a_negative_robustness
		COMMAND
			${CMAKE_COMMAND}
			-DAZTECA_EXECUTABLE=$<TARGET_FILE:azteca>
			-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
			-DPROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}
			-P ${PROJECT_SOURCE_DIR}/tests/integration/PhaseANegativeRobustness.cmake
	)

	add_test(
		NAME phase_a_negative_suite
		COMMAND
			${CMAKE_COMMAND}
			-DAZTECA_EXECUTABLE=$<TARGET_FILE:azteca>
			-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
			-DPROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}
			-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
			-P ${PROJECT_SOURCE_DIR}/tests/integration/PhaseANegativeSuite.cmake
	)

	if(GTest_FOUND)
		add_executable(
			azteca_unit_tests
			tests/unit/AssertTests.cpp
			tests/unit/DiagnosticCatalogTests.cpp
			tests/unit/ExtractionPlanTests.cpp
			tests/unit/InspectCollectorTests.cpp
			tests/unit/JsonWriterTests.cpp
			tests/unit/MethodSpecTests.cpp
			tests/unit/RuleCoverageRegistryTests.cpp
		)
		target_include_directories(azteca_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
		target_link_libraries(azteca_unit_tests PRIVATE azteca_core GTest::gtest_main)
		list(APPEND AZTECA_TEST_DEPENDENCIES azteca_unit_tests)
		include(GoogleTest)
		gtest_discover_tests(azteca_unit_tests)
	else()
		message(WARNING "GTest was not found; Azteca unit tests are not built in this environment.")
	endif()
endif()

add_custom_target(
	format-check
	COMMAND
		${CMAKE_COMMAND}
		-DCLANG_FORMAT_EXECUTABLE=${CLANG_FORMAT_EXECUTABLE}
		-DGIT_EXECUTABLE=${GIT_EXECUTABLE}
		-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-DMODE=check
		-P ${PROJECT_SOURCE_DIR}/cmake/ClangFormat.cmake
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	COMMENT "Checking C++ formatting"
	VERBATIM
)

add_custom_target(
	format
	COMMAND
		${CMAKE_COMMAND}
		-DCLANG_FORMAT_EXECUTABLE=${CLANG_FORMAT_EXECUTABLE}
		-DGIT_EXECUTABLE=${GIT_EXECUTABLE}
		-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-DMODE=fix
		-P ${PROJECT_SOURCE_DIR}/cmake/ClangFormat.cmake
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	COMMENT "Formatting C++ sources"
	VERBATIM
)

add_custom_target(
	prettier-check
	COMMAND ${NPM_EXECUTABLE} run format:docs:check
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	COMMENT "Checking Markdown, JSON, and YAML formatting"
	VERBATIM
)

add_custom_target(
	prettier-format
	COMMAND ${NPM_EXECUTABLE} run format:docs
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	COMMENT "Formatting Markdown, JSON, and YAML"
	VERBATIM
)

add_custom_target(
	reference-check
	COMMAND ${NPM_EXECUTABLE} run docs:reference:check
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	COMMENT "Checking generated all-in-one design reference"
	VERBATIM
)

add_custom_target(
	reference-format
	COMMAND ${NPM_EXECUTABLE} run docs:reference
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	COMMENT "Regenerating all-in-one design reference"
	VERBATIM
)

add_custom_target(
	phase-a-accept-goldens
	COMMAND
		${CMAKE_COMMAND}
		-E env AZTECA_ACCEPT_GOLDEN=1
		${CMAKE_CTEST_COMMAND} --output-on-failure --test-dir ${CMAKE_BINARY_DIR}
		-R "^phase_a_(inspect_fixture|cli_surface)$"
	COMMAND ${NPM_EXECUTABLE} run format:phase-a-goldens
	DEPENDS azteca
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	COMMENT "Regenerating Phase A inspect and CLI goldens"
	VERBATIM
)

function(add_azteca_lint_target target_name profile comment)
	if(CLANG_TIDY_EXECUTABLE)
		add_custom_target(
			${target_name}
			COMMAND
				${CMAKE_COMMAND}
				-DCLANG_TIDY_EXECUTABLE=${CLANG_TIDY_EXECUTABLE}
				-DRUN_CLANG_TIDY_EXECUTABLE=${RUN_CLANG_TIDY_EXECUTABLE}
				-DGIT_EXECUTABLE=${GIT_EXECUTABLE}
				-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
				-DCOMPILE_COMMANDS_DIR=${CMAKE_BINARY_DIR}
				-DCLANG_TIDY_JOBS=${AZTECA_CLANG_TIDY_JOBS}
				-DCLANG_TIDY_PROFILE=${profile}
				-P ${PROJECT_SOURCE_DIR}/cmake/ClangTidy.cmake
			WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
			COMMENT "${comment}"
			VERBATIM
		)
	else()
		add_custom_target(
			${target_name}
			COMMAND
				${CMAKE_COMMAND} -E echo
				"clang-tidy 18 was not found at configure time; install clang-tidy-18 and re-run CMake to use ${target_name}."
			COMMAND ${CMAKE_COMMAND} -E false
			WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
			COMMENT "${comment}"
			VERBATIM
		)
	endif()
endfunction()

add_azteca_lint_target(lint full "Running clang-tidy")
add_azteca_lint_target(lint-fast fast "Running clang-tidy fast profile")

add_custom_target(
	quick-check
	COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --test-dir ${CMAKE_BINARY_DIR}
	DEPENDS ${AZTECA_TEST_DEPENDENCIES}
	COMMENT "Running fast local test loop"
	VERBATIM
)

add_custom_target(
	check
	COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --test-dir ${CMAKE_BINARY_DIR}
	DEPENDS ${AZTECA_TEST_DEPENDENCIES} format-check prettier-check reference-check
	COMMENT "Running daily local quality gates"
	VERBATIM
)

add_custom_target(
	strict-check
	COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --test-dir ${CMAKE_BINARY_DIR}
	DEPENDS ${AZTECA_TEST_DEPENDENCIES} format-check prettier-check reference-check lint
	COMMENT "Running strict quality gates"
	VERBATIM
)

# Phase A release gate: runs only the contract / surface tests that must
# pass before tagging a Phase A release. Intended for CI release jobs and
# pre-tag local verification. Excludes lint/perf to keep it fast.
add_custom_target(
	release-check
	COMMAND
		${CMAKE_CTEST_COMMAND} --output-on-failure --test-dir ${CMAKE_BINARY_DIR}
		-R "phase_a_(cli_surface|schema_v2_check|schema_v2_validate|inspect_fixture|encoding_robustness|coverage_matrix|coverage_observations|negative_suite|negative_robustness)|DiagnosticCatalog|InspectReport"
	DEPENDS ${AZTECA_TEST_DEPENDENCIES} format-check prettier-check
	COMMENT "Running Phase A release contract gates"
	VERBATIM
)
