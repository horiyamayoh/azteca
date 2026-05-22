if(NOT DEFINED CLANG_FORMAT_EXECUTABLE)
	message(FATAL_ERROR "CLANG_FORMAT_EXECUTABLE is required")
endif()

if(NOT DEFINED GIT_EXECUTABLE)
	message(FATAL_ERROR "GIT_EXECUTABLE is required")
endif()

if(NOT DEFINED PROJECT_SOURCE_DIR)
	message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

if(NOT DEFINED MODE)
	message(FATAL_ERROR "MODE must be 'check' or 'fix'")
endif()

execute_process(
	COMMAND ${GIT_EXECUTABLE} ls-files --cached --others --exclude-standard
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	OUTPUT_VARIABLE git_files
	RESULT_VARIABLE git_result
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT git_result EQUAL 0)
	message(FATAL_ERROR "git ls-files failed")
endif()

string(REPLACE "\n" ";" git_files "${git_files}")
set(format_files)

foreach(file IN LISTS git_files)
	if(file MATCHES "\\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$")
		list(APPEND format_files "${PROJECT_SOURCE_DIR}/${file}")
	endif()
endforeach()

if(NOT format_files)
	message(STATUS "No C++ files found by git; skipping clang-format")
	return()
endif()

if(MODE STREQUAL "check")
	set(format_args --dry-run --Werror)
elseif(MODE STREQUAL "fix")
	set(format_args -i)
else()
	message(FATAL_ERROR "Unknown clang-format mode: ${MODE}")
endif()

execute_process(
	COMMAND ${CLANG_FORMAT_EXECUTABLE} ${format_args} ${format_files}
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	RESULT_VARIABLE format_result
)

if(NOT format_result EQUAL 0)
	message(FATAL_ERROR "clang-format ${MODE} failed")
endif()
