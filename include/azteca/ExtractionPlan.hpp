#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "azteca/Diagnostics.hpp"
#include "azteca/MMIR.hpp"
#include "azteca/SourceLocation.hpp"

namespace azteca
{

enum class FieldAccess : std::uint8_t
{
	kRead,
	kWrite,
	kReadWrite,
	kAddress,
};

struct PlanEvidence
{
	std::string rule_id;
	std::string reason;
	std::string certainty{"certain"};
	bool conservative{false};
	SourceRange source_range;
};

struct TargetInfo
{
	std::string qualified_name;
	std::string signature;
	std::string source_file;
	unsigned line{0};
};

struct ReceiverField
{
	std::string name;
	std::string type;
	FieldAccess access{FieldAccess::kRead};
	bool is_mutable{false};
	std::string access_specifier;
	SourceLocation location;
	PlanEvidence evidence;
};

enum class DependencyKind : std::uint8_t
{
	kRecursiveCandidate,
	kQuery,
	kEffect,
	kOperation,
};

struct DependencyPort
{
	DependencyKind kind{DependencyKind::kQuery};
	std::string name;
	std::string original_callee;
	std::string return_type;
	std::vector<std::string> argument_types;
	SourceLocation location;
	PlanEvidence evidence;
};

struct ShapeCandidate
{
	std::string name;
	std::string source_dependency;
	std::vector<std::string> observed_members;
	PlanEvidence evidence;
};

struct ObjectRefRequirement
{
	std::string reason;
	std::string expression;
	SourceLocation location;
	PlanEvidence evidence;
};

struct PathBurden
{
	std::string name;
	std::vector<std::string> observations;
	std::vector<std::string> effects;
	std::vector<std::string> operations;
	PlanEvidence evidence;
};

struct GTestPreview
{
	std::string sample_test_path;
	std::vector<std::string> lines;
};

struct ExtractionPlan
{
	int schema_version{1};
	TargetInfo target;
	std::string result{"extracted"};
	std::vector<ReceiverField> receiver_state;
	std::vector<DependencyPort> dependency_ports;
	std::vector<ShapeCandidate> shape_candidates;
	std::vector<ObjectRefRequirement> object_ref_requirements;
	std::vector<PathBurden> paths;
	GTestPreview gtest_preview;
	Diagnostics diagnostics;
	MmirFunction mmir;
};

[[nodiscard]] std::string to_string(FieldAccess access);
[[nodiscard]] std::string to_string(DependencyKind kind);

} // namespace azteca
