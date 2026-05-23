#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "azteca/Diagnostics.hpp"
#include "azteca/SourceLocation.hpp"

namespace azteca
{

enum class MmirNodeKind : std::uint8_t
{
	kArgRef,
	kLocalRef,
	kFieldRef,
	kBaseFieldRef,
	kGlobalRef,
	kLiteral,
	kUnary,
	kBinary,
	kAssign,
	kIf,
	kSwitch,
	kLoop,
	kReturn,
	kThrow,
	kTry,
	kCall,
	kBoundaryCall,
	kDispatchCall,
	kOperatorCall,
	kCast,
	kTypeInfo,
	kLambda,
	kLifetimeOp,
	kByteView,
	kMacroExpansion,
	kUnevaluatedContext,
	kStructuredBinding,
	kObjectRefRequirement,
	kUnsupported,
};

struct MmirNode
{
	MmirNodeKind kind{MmirNodeKind::kUnsupported};
	std::string label;
	SourceLocation location;
	SourceRange source_range;
	std::string semantic_id;
	std::string original_symbol;
	std::string rule_id;
	std::string reason;
	bool conservative{false};
};

struct MmirFunction
{
	std::string target_name;
	std::vector<MmirNode> nodes;
};

[[nodiscard]] std::string to_string(MmirNodeKind kind);
[[nodiscard]] bool validate_mmir(MmirFunction const& function, Diagnostics& diagnostics);

} // namespace azteca
