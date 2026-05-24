if(NOT DEFINED AZTECA_EXECUTABLE)
	message(FATAL_ERROR "AZTECA_EXECUTABLE is required")
endif()

if(NOT DEFINED PROJECT_BINARY_DIR)
	message(FATAL_ERROR "PROJECT_BINARY_DIR is required")
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER)
	message(FATAL_ERROR "CMAKE_CXX_COMPILER is required")
endif()

set(_work "${PROJECT_BINARY_DIR}/test-work/phase_a_perf")
file(REMOVE_RECURSE "${_work}")
file(MAKE_DIRECTORY "${_work}")

# Synthesize N translation units. One TU contains the target class; the rest
# are filler. This stresses compile-database scan + ASTContext setup without
# stressing parsing depth.
set(_tu_count 50)

file(
	WRITE
	"${_work}/target.cpp"
	[[
class Target
{
public:
	int sum(int a, int b)
	{
		int total = a + b;
		if (total < 0)
		{
			return -total;
		}
		return total;
	}

private:
	int seed_ = 0;
};

int target_sink(Target& t)
{
	return t.sum(1, 2);
}
]]
)

foreach(_i RANGE 1 ${_tu_count})
	file(
		WRITE
		"${_work}/filler_${_i}.cpp"
		"struct Filler${_i} { int v_ = ${_i}; int get() const { return v_; } };\n"
		"int filler_sink_${_i}(Filler${_i}& f) { return f.get(); }\n"
	)
endforeach()

set(_entries "target")
foreach(_i RANGE 1 ${_tu_count})
	list(APPEND _entries "filler_${_i}")
endforeach()

set(_first TRUE)
file(WRITE "${_work}/compile_commands.json" "[\n")
foreach(_name IN LISTS _entries)
	if(_first)
		set(_first FALSE)
	else()
		file(APPEND "${_work}/compile_commands.json" ",\n")
	endif()
	file(
		APPEND
		"${_work}/compile_commands.json"
		"  {\n    \"directory\": \"${_work}\",\n    \"command\": \"${CMAKE_CXX_COMPILER} -std=c++20 -c ${_name}.cpp\",\n    \"file\": \"${_work}/${_name}.cpp\"\n  }"
	)
endforeach()
file(APPEND "${_work}/compile_commands.json" "\n]\n")

# Measure wall time around a single inspect run.
string(TIMESTAMP _t0 "%s")

execute_process(
	COMMAND
		"${AZTECA_EXECUTABLE}" inspect -p "${_work}" --source "${_work}/target.cpp"
		--method "Target::sum(int,int)" --format json
	RESULT_VARIABLE _result
	OUTPUT_VARIABLE _stdout
	ERROR_VARIABLE _stderr
)

string(TIMESTAMP _t1 "%s")
math(EXPR _elapsed "${_t1} - ${_t0}")

if(NOT _result EQUAL 0)
	message(
		FATAL_ERROR
		"perf smoke inspect failed (exit=${_result}):\n${_stderr}\n${_stdout}"
	)
endif()

# Sanity-check the JSON contract still holds at scale.
string(JSON _schema GET "${_stdout}" schema_version)
if(NOT _schema STREQUAL "2")
	message(FATAL_ERROR "perf smoke schema_version=${_schema}")
endif()

# Non-blocking baseline: warn (do not fail) above the documented baseline by
# the configured ratio so regressions are visible in CI logs without blocking
# PRs. Promote to a hard threshold in a later phase once the baseline is
# tracked across CI runs.
set(_baseline_path "${CMAKE_CURRENT_LIST_DIR}/baseline.json")
if(EXISTS "${_baseline_path}")
	file(READ "${_baseline_path}" _baseline_json)
	string(JSON _baseline_seconds GET "${_baseline_json}" baseline_seconds)
	string(JSON _warn_ratio_percent GET "${_baseline_json}" warn_ratio_percent)
	math(EXPR _warn_threshold_seconds "(${_baseline_seconds} * ${_warn_ratio_percent}) / 100")
else()
	set(_baseline_seconds 0)
	set(_warn_threshold_seconds 30)
endif()

message(
	STATUS
	"phase_a_perf_smoke: elapsed=${_elapsed}s baseline=${_baseline_seconds}s warn>${_warn_threshold_seconds}s tu=${_tu_count}"
)

if(_elapsed GREATER ${_warn_threshold_seconds})
	message(
		WARNING
		"phase_a_perf_smoke took ${_elapsed}s (baseline=${_baseline_seconds}s, warn>${_warn_threshold_seconds}s)"
	)
endif()

message(STATUS "Phase A perf smoke: OK (${_elapsed}s, ${_tu_count} filler TUs)")
