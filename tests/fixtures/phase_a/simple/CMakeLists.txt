cmake_minimum_required(VERSION 3.20)

project(azteca_phase_a_simple_fixture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# When clang lives under a conda/micromamba prefix, the activation scripts inject
# --sysroot/-resource-dir via CXXFLAGS. If this CMakeLists is configured from a
# shell that did not activate the clang toolchain (only gcc), those flags are
# missing and clang fails to locate <stddef.h>. Detect and inject them so the
# fixture compiles regardless of which conda toolchain is currently active.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	get_filename_component(_azt_clang_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
	get_filename_component(_azt_clang_prefix "${_azt_clang_bin_dir}" DIRECTORY)
	execute_process(
		COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
		OUTPUT_VARIABLE _azt_clang_resource_dir
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	file(GLOB _azt_clang_sysroot_candidates LIST_DIRECTORIES true
		"${_azt_clang_prefix}/*-conda-linux-gnu/sysroot")
	list(GET _azt_clang_sysroot_candidates 0 _azt_clang_sysroot)
	if(_azt_clang_resource_dir AND _azt_clang_sysroot AND IS_DIRECTORY "${_azt_clang_sysroot}")
		add_compile_options(
			"--sysroot=${_azt_clang_sysroot}"
			"-resource-dir=${_azt_clang_resource_dir}"
		)
	endif()
endif()

add_library(
	phase_a_simple_fixture
	STATIC
	account.cpp
	ambiguous_a.cpp
	ambiguous_b.cpp
	edge_cases.cpp
	hardening.cpp
	order_service.cpp
	real_project.cpp
	real_project_duplicate_a.cpp
	real_project_duplicate_b.cpp
	service.cpp
	simple_scenarios.cpp
	syntax_matrix.cpp
)

target_include_directories(phase_a_simple_fixture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
