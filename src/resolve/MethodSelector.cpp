#include "MethodSelector.hpp"

#include <clang/AST/DeclTemplate.h>
#include <clang/AST/TemplateBase.h>
#include <clang/AST/Type.h>
#include <llvm/Support/raw_ostream.h>

#include <set>
#include <sstream>

namespace azteca
{
namespace
{

[[nodiscard]] std::string type_string(clang::ASTContext const& context, clang::QualType type)
{
	clang::PrintingPolicy policy(context.getLangOpts());
	policy.SuppressTagKeyword = true;
	policy.SuppressUnwrittenScope = true;
	return type.getAsString(policy);
}

[[nodiscard]] std::string canonical_type_string(
    clang::ASTContext const& context, clang::QualType type)
{
	return type_string(context, type.getCanonicalType());
}

[[nodiscard]] std::set<std::string> template_parameter_names(clang::CXXMethodDecl const& method)
{
	std::set<std::string> names;
	auto const* primary_template = method.getPrimaryTemplate();
	if (primary_template == nullptr)
	{
		primary_template = method.getDescribedFunctionTemplate();
	}
	if (primary_template == nullptr)
	{
		return names;
	}

	for (auto const* parameter : *primary_template->getTemplateParameters())
	{
		if (auto const* named = llvm::dyn_cast<clang::NamedDecl>(parameter);
		    named != nullptr && !named->getNameAsString().empty())
		{
			names.insert(normalize_type_for_match(named->getNameAsString()));
		}
	}
	return names;
}

} // namespace

std::string method_signature(clang::ASTContext const& context, clang::CXXMethodDecl const& method)
{
	std::ostringstream output;
	output << type_string(context, method.getReturnType()) << '(';
	for (auto index = unsigned{0}; index < method.getNumParams(); ++index)
	{
		if (index != 0U)
		{
			output << ", ";
		}
		output << type_string(context, method.getParamDecl(index)->getType());
	}
	output << ')';
	if (method.isConst())
	{
		output << " const";
	}
	if (method.isVolatile())
	{
		output << " volatile";
	}
	switch (method.getRefQualifier())
	{
		case clang::RQ_None:
			break;
		case clang::RQ_LValue:
			output << " &";
			break;
		case clang::RQ_RValue:
			output << " &&";
			break;
	}
	return output.str();
}

std::vector<std::string> method_template_arguments(
    clang::ASTContext const& context, clang::CXXMethodDecl const& method)
{
	std::vector<std::string> arguments;
	auto const* template_arguments = method.getTemplateSpecializationArgs();
	if (template_arguments == nullptr)
	{
		return arguments;
	}

	clang::PrintingPolicy policy(context.getLangOpts());
	policy.SuppressTagKeyword = true;
	policy.SuppressUnwrittenScope = true;
	for (auto const& argument : template_arguments->asArray())
	{
		std::string text;
		llvm::raw_string_ostream stream(text);
		argument.print(policy, stream, true);
		arguments.push_back(stream.str());
	}
	return arguments;
}

bool method_matches_spec(
    MethodSpec const& spec, clang::ASTContext const& context, clang::CXXMethodDecl const& method)
{
	auto const* parent = method.getParent();
	if (parent == nullptr)
	{
		return false;
	}

	if (parent->getQualifiedNameAsString() != spec.qualified_class_name)
	{
		return false;
	}

	if (method.getNameAsString() != spec.method_name)
	{
		return false;
	}

	if (method.getNumParams() != spec.parameter_types.size())
	{
		return false;
	}

	if (method.isConst() != spec.is_const)
	{
		return false;
	}

	if (method.isVolatile() != spec.is_volatile)
	{
		return false;
	}

	if ((spec.ref_qualifier == RefQualifier::kNone && method.getRefQualifier() != clang::RQ_None) ||
	    (spec.ref_qualifier == RefQualifier::kLValue &&
	        method.getRefQualifier() != clang::RQ_LValue) ||
	    (spec.ref_qualifier == RefQualifier::kRValue &&
	        method.getRefQualifier() != clang::RQ_RValue))
	{
		return false;
	}

	if (!spec.template_arguments.empty())
	{
		auto actual_template_arguments = method_template_arguments(context, method);
		if (actual_template_arguments.empty() && method.getDescribedFunctionTemplate() == nullptr)
		{
			return false;
		}
		if (!actual_template_arguments.empty())
		{
			if (actual_template_arguments.size() != spec.template_arguments.size())
			{
				return false;
			}
			for (auto index = std::size_t{0}; index < spec.template_arguments.size(); ++index)
			{
				if (normalize_type_for_match(spec.template_arguments[index]) !=
				    normalize_type_for_match(actual_template_arguments[index]))
				{
					return false;
				}
			}
		}
	}

	auto template_parameters = template_parameter_names(method);
	for (auto index = std::size_t{0}; index < spec.parameter_types.size(); ++index)
	{
		auto expected = normalize_type_for_match(spec.parameter_types[index]);
		auto actual = normalize_type_for_match(canonical_type_string(
		    context, method.getParamDecl(static_cast<unsigned>(index))->getType()));
		auto actual_spelled = normalize_type_for_match(
		    type_string(context, method.getParamDecl(static_cast<unsigned>(index))->getType()));

		auto const kTemplateParameterPlaceholder = template_parameters.contains(expected) ||
		    template_parameters.contains(actual) || template_parameters.contains(actual_spelled);
		if ((!spec.template_arguments.empty() || method.isFunctionTemplateSpecialization()) &&
		    kTemplateParameterPlaceholder)
		{
			continue;
		}

		if (expected != actual && expected != actual_spelled)
		{
			return false;
		}
	}

	return true;
}

} // namespace azteca
