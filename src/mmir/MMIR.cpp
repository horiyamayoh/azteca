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

bool validate_mmir(MmirFunction const& function, Diagnostics& diagnostics)
{
	auto valid = true;

	for (auto const& node : function.nodes)
	{
		if (!node.source_range.is_valid() && !node.location.is_valid())
		{
			diagnostics.add(DiagnosticSeverity::kError, "AZTECA_MMIR_LOCATION_MISSING",
			    "MMIR node has no source location: " + to_string(node.kind));
			valid = false;
		}

		if (node.kind == MmirNodeKind::kFieldRef && node.semantic_id.empty())
		{
			diagnostics.add(DiagnosticSeverity::kError, "AZTECA_MMIR_FIELD_DECL_MISSING",
			    "MMIR FieldRef has no field identity", node.location);
			valid = false;
		}

		if (node.kind == MmirNodeKind::kBoundaryCall && node.original_symbol.empty())
		{
			diagnostics.add(DiagnosticSeverity::kError, "AZTECA_MMIR_BOUNDARY_CALLEE_MISSING",
			    "MMIR BoundaryCall has no original callee", node.location);
			valid = false;
		}

		if (node.kind == MmirNodeKind::kCall)
		{
			diagnostics.add(DiagnosticSeverity::kError, "AZTECA_MMIR_UNCLASSIFIED_CALL",
			    "MMIR contains an unclassified call", node.location);
			valid = false;
		}

		if (node.kind == MmirNodeKind::kUnsupported && node.label == "bare this")
		{
			diagnostics.add(DiagnosticSeverity::kError, "AZTECA_MMIR_BARE_THIS",
			    "MMIR contains a bare this expression", node.location);
			valid = false;
		}
	}

	return valid;
}

} // namespace azteca
