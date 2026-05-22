if(NOT DEFINED CLANG_TIDY_EXECUTABLE)
	message(FATAL_ERROR "CLANG_TIDY_EXECUTABLE is required")
endif()

if(NOT DEFINED GIT_EXECUTABLE)
	message(FATAL_ERROR "GIT_EXECUTABLE is required")
endif()

if(NOT DEFINED PROJECT_SOURCE_DIR)
	message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

if(NOT DEFINED COMPILE_COMMANDS_DIR)
	message(FATAL_ERROR "COMPILE_COMMANDS_DIR is required")
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
set(tidy_files)
set(header_filter_regex "(^|.*/)(include|src|tests)/")

foreach(file IN LISTS git_files)
	if(file MATCHES "^src/.*\\.(cc|cpp|cxx)$")
		list(APPEND tidy_files "${PROJECT_SOURCE_DIR}/${file}")
	endif()
endforeach()

if(NOT tidy_files)
	message(STATUS "No C++ translation units found by git; skipping clang-tidy")
	return()
endif()

if(NOT EXISTS "${COMPILE_COMMANDS_DIR}/compile_commands.json")
	message(FATAL_ERROR "compile_commands.json is missing in ${COMPILE_COMMANDS_DIR}")
endif()

foreach(file IN LISTS tidy_files)
	execute_process(
		COMMAND
			${CLANG_TIDY_EXECUTABLE}
			-p=${COMPILE_COMMANDS_DIR}
			--header-filter=${header_filter_regex}
			--warnings-as-errors=*
			${file}
		WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
		RESULT_VARIABLE tidy_result
	)

	if(NOT tidy_result EQUAL 0)
		message(FATAL_ERROR "clang-tidy failed for ${file}")
	endif()
endforeach()
