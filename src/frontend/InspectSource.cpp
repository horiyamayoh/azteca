#include "InspectSource.hpp"

#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>

#include <utility>

namespace azteca::inspect_frontend
{

std::string type_string(clang::ASTContext const& context, clang::QualType type)
{
	clang::PrintingPolicy policy(context.getLangOpts());
	policy.SuppressTagKeyword = true;
	policy.SuppressUnwrittenScope = true;
	return type.getAsString(policy);
}

azteca::SourceLocation source_location(
    clang::ASTContext const& context, clang::SourceLocation location)
{
	if (location.isInvalid())
	{
		return {};
	}

	auto const& source_manager = context.getSourceManager();
	auto spelling_location = source_manager.getSpellingLoc(location);
	if (spelling_location.isInvalid())
	{
		return {};
	}

	auto presumed = source_manager.getPresumedLoc(spelling_location);
	if (presumed.isInvalid())
	{
		return {};
	}

	return {
	    .file = presumed.getFilename(),
	    .line = presumed.getLine(),
	    .column = presumed.getColumn(),
	};
}

azteca::SourceRange source_range(clang::ASTContext const& context, clang::SourceRange range)
{
	if (range.isInvalid())
	{
		return {};
	}

	auto const& source_manager = context.getSourceManager();
	auto const& language_options = context.getLangOpts();
	auto end_location =
	    clang::Lexer::getLocForEndOfToken(range.getEnd(), 0, source_manager, language_options);
	if (end_location.isInvalid())
	{
		end_location = range.getEnd();
	}

	return {
	    .begin = source_location(context, range.getBegin()),
	    .end = source_location(context, end_location),
	};
}

std::string source_text(clang::ASTContext const& context, clang::SourceRange range)
{
	if (range.isInvalid())
	{
		return "";
	}

	auto const& source_manager = context.getSourceManager();
	auto const& language_options = context.getLangOpts();
	bool invalid = false;
	auto text = clang::Lexer::getSourceText(
	    clang::CharSourceRange::getTokenRange(range), source_manager, language_options, &invalid);

	if (invalid)
	{
		return "";
	}

	return text.str();
}

PlanEvidence make_evidence(clang::ASTContext const& context, std::string rule_id,
    std::string reason, clang::SourceRange range, bool conservative, std::string certainty)
{
	return {
	    .rule_id = std::move(rule_id),
	    .reason = std::move(reason),
	    .certainty = std::move(certainty),
	    .conservative = conservative,
	    .source_range = source_range(context, range),
	};
}

} // namespace azteca::inspect_frontend
