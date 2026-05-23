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

enum class ConstructHandling : std::uint8_t
{
	kSupported,
	kModeled,
	kBoundary,
	kConservative,
	kNotYetImplemented,
	kNotMeaningful,
};

enum class EnvelopeRequirementKind : std::uint8_t
{
	kSelfState,
	kBaseState,
	kAddressableCell,
	kObjectRef,
	kDependencyBoundary,
	kDispatchTable,
	kTypeTag,
	kLifetimeState,
	kByteView,
	kGlobalEnvironment,
	kExceptionModel,
	kMacroSourceMap,
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

struct SemanticFeature
{
	std::string name;
	ConstructHandling handling{ConstructHandling::kSupported};
	std::string detail;
	PlanEvidence evidence;
};

struct UnsupportedOrModeledConstruct
{
	std::string construct;
	ConstructHandling handling{ConstructHandling::kModeled};
	std::string reason;
	std::vector<std::string> fallbacks;
	SourceLocation location;
	PlanEvidence evidence;
};

struct ControlFlowSummary
{
	bool has_if{false};
	bool has_switch{false};
	bool has_loop{false};
	bool has_range_for{false};
	bool has_try{false};
	bool has_throw{false};
	bool has_return{false};
	bool conservative{false};
	std::vector<std::string> conservative_reasons;
};

struct EnvelopeRequirement
{
	EnvelopeRequirementKind kind{EnvelopeRequirementKind::kSelfState};
	std::string reason;
	std::string source;
	PlanEvidence evidence;
};

struct RuleCoverage
{
	std::string rule_id;
	ConstructHandling handling{ConstructHandling::kNotYetImplemented};
	std::string note;
	bool observed{false};
};

struct PathBurden
{
	std::string name;
	std::vector<std::string> observations;
	std::vector<std::string> effects;
	std::vector<std::string> operations;
	std::vector<std::string> loop_body_observations;
	std::vector<std::string> required_envelopes;
	std::string conservative_reason;
	PlanEvidence evidence;
};

struct GTestPreview
{
	std::string sample_test_path;
	std::vector<std::string> lines;
};

struct ExtractionPlan
{
	int schema_version{2};
	TargetInfo target;
	std::string result{"extracted"};
	std::string confidence{"high"};
	std::vector<ReceiverField> receiver_state;
	std::vector<DependencyPort> dependency_ports;
	std::vector<ShapeCandidate> shape_candidates;
	std::vector<ObjectRefRequirement> object_ref_requirements;
	std::vector<SemanticFeature> semantic_features;
	std::vector<UnsupportedOrModeledConstruct> unsupported_or_modeled_constructs;
	ControlFlowSummary control_flow_summary;
	std::vector<EnvelopeRequirement> envelope_requirements;
	std::vector<RuleCoverage> rule_coverage;
	std::vector<PathBurden> paths;
	GTestPreview gtest_preview;
	Diagnostics diagnostics;
	MmirFunction mmir;
};

[[nodiscard]] std::string to_string(FieldAccess access);
[[nodiscard]] std::string to_string(DependencyKind kind);
[[nodiscard]] std::string to_string(ConstructHandling handling);
[[nodiscard]] std::string to_string(EnvelopeRequirementKind kind);

} // namespace azteca
