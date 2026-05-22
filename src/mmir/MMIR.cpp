#include "azteca/MMIR.hpp"

namespace azteca
{

std::string to_string(MmirNodeKind kind)
{
	switch (kind)
	{
		case MmirNodeKind::kArgRef:
			return "ArgRef";
		case MmirNodeKind::kLocalRef:
			return "LocalRef";
		case MmirNodeKind::kFieldRef:
			return "FieldRef";
		case MmirNodeKind::kLiteral:
			return "Literal";
		case MmirNodeKind::kUnary:
			return "Unary";
		case MmirNodeKind::kBinary:
			return "Binary";
		case MmirNodeKind::kAssign:
			return "Assign";
		case MmirNodeKind::kIf:
			return "If";
		case MmirNodeKind::kReturn:
			return "Return";
		case MmirNodeKind::kCall:
			return "Call";
		case MmirNodeKind::kBoundaryCall:
			return "BoundaryCall";
		case MmirNodeKind::kObjectRefRequirement:
			return "ObjectRefRequirement";
		case MmirNodeKind::kUnsupported:
			return "Unsupported";
	}

	return "Unsupported";
}

} // namespace azteca
