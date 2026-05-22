#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "azteca/SourceLocation.hpp"

namespace azteca
{

enum class MmirNodeKind : std::uint8_t
{
	kArgRef,
	kLocalRef,
	kFieldRef,
	kLiteral,
	kUnary,
	kBinary,
	kAssign,
	kIf,
	kReturn,
	kCall,
	kBoundaryCall,
	kObjectRefRequirement,
	kUnsupported,
};

struct MmirNode
{
	MmirNodeKind kind{MmirNodeKind::kUnsupported};
	std::string label;
	SourceLocation location;
};

struct MmirFunction
{
	std::string target_name;
	std::vector<MmirNode> nodes;
};

[[nodiscard]] std::string to_string(MmirNodeKind kind);

} // namespace azteca
