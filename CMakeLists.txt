cmake_minimum_required(VERSION 3.24)

project(azteca VERSION 0.1.0 LANGUAGES CXX)

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
find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy-18 clang-tidy REQUIRED)
find_program(NPM_EXECUTABLE npm REQUIRED)

require_tool_major_version("clang-format" "${CLANG_FORMAT_EXECUTABLE}" 18)
require_tool_major_version("clang-tidy" "${CLANG_TIDY_EXECUTABLE}" 18)

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
	lint
	COMMAND
		${CMAKE_COMMAND}
		-DCLANG_TIDY_EXECUTABLE=${CLANG_TIDY_EXECUTABLE}
		-DGIT_EXECUTABLE=${GIT_EXECUTABLE}
		-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-DCOMPILE_COMMANDS_DIR=${CMAKE_BINARY_DIR}
		-P ${PROJECT_SOURCE_DIR}/cmake/ClangTidy.cmake
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	COMMENT "Running clang-tidy"
	VERBATIM
)

add_custom_target(
	check
	COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --test-dir ${CMAKE_BINARY_DIR}
	DEPENDS format-check prettier-check reference-check lint
	COMMENT "Running all local quality gates"
	VERBATIM
)
