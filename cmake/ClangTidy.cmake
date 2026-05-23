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

if(NOT DEFINED CLANG_TIDY_PROFILE)
	set(CLANG_TIDY_PROFILE full)
endif()

if(NOT DEFINED CLANG_TIDY_JOBS)
	set(CLANG_TIDY_JOBS 1)
endif()

if(NOT CLANG_TIDY_JOBS MATCHES "^[1-9][0-9]*$")
	message(FATAL_ERROR "CLANG_TIDY_JOBS must be a positive integer")
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
set(tidy_file_regex "src/.*\\.(cc|cpp|cxx)$")
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

set(tidy_profile_args)
if(CLANG_TIDY_PROFILE STREQUAL "fast")
	set(
		fast_checks
		"-*,clang-diagnostic-*,bugprone-*,performance-*,modernize-*,readability-*,-modernize-use-trailing-return-type,-readability-function-cognitive-complexity,-readability-magic-numbers"
	)
	set(tidy_profile_args "-checks=${fast_checks}")
elseif(NOT CLANG_TIDY_PROFILE STREQUAL "full")
	message(FATAL_ERROR "Unknown CLANG_TIDY_PROFILE: ${CLANG_TIDY_PROFILE}")
endif()

if(
	DEFINED RUN_CLANG_TIDY_EXECUTABLE
	AND NOT RUN_CLANG_TIDY_EXECUTABLE STREQUAL ""
	AND NOT RUN_CLANG_TIDY_EXECUTABLE MATCHES "-NOTFOUND$"
)
	message(
		STATUS
		"Running clang-tidy ${CLANG_TIDY_PROFILE} profile with ${CLANG_TIDY_JOBS} jobs"
	)
	execute_process(
		COMMAND
			${RUN_CLANG_TIDY_EXECUTABLE}
			-clang-tidy-binary ${CLANG_TIDY_EXECUTABLE}
			-p ${COMPILE_COMMANDS_DIR}
			-j ${CLANG_TIDY_JOBS}
			-quiet
			-header-filter=${header_filter_regex}
			${tidy_profile_args}
			-warnings-as-errors=*
			${tidy_file_regex}
		WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
		RESULT_VARIABLE tidy_result
	)

	if(NOT tidy_result EQUAL 0)
		message(FATAL_ERROR "clang-tidy ${CLANG_TIDY_PROFILE} profile failed")
	endif()

	return()
endif()

message(STATUS "run-clang-tidy was not found; running clang-tidy serially")
foreach(file IN LISTS tidy_files)
	execute_process(
		COMMAND
			${CLANG_TIDY_EXECUTABLE}
			-p=${COMPILE_COMMANDS_DIR}
			--header-filter=${header_filter_regex}
			${tidy_profile_args}
			--warnings-as-errors=*
			${file}
		WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
		RESULT_VARIABLE tidy_result
	)

	if(NOT tidy_result EQUAL 0)
		message(FATAL_ERROR "clang-tidy failed for ${file}")
	endif()
endforeach()
