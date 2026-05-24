cmake_minimum_required(VERSION 3.20)

project(azteca_phase_a_negative_fixture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Mirror simple/CMakeLists.txt: inject clang sysroot/resource-dir when needed so
# this fixture builds even when only gxx_linux-64 conda env is active.
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

add_library(phase_a_negative_fixture STATIC declaration_only.cpp)
