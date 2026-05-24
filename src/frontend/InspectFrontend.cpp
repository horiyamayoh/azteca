#include "azteca/InspectFrontend.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "../resolve/MethodSelector.hpp"
#include "InspectSource.hpp"
#include "PlanBuilder.hpp"
#include "azteca/MethodSpec.hpp"

namespace azteca
{
namespace
{

struct ToolState
{
	MethodSpec spec;
	std::vector<ExtractionPlan> plans;
	std::optional<ExtractionPlan> template_fallback_plan;
	Diagnostics diagnostics;
	bool selected_template_without_explicit_args{false};
};

using inspect_frontend::source_location;

class MethodLookupVisitor : public clang::RecursiveASTVisitor<MethodLookupVisitor>
{
   public:
	MethodLookupVisitor(clang::ASTContext& context, ToolState& state)
	    : context_(context), state_(state)
	{
	}

	bool VisitCXXMethodDecl(clang::CXXMethodDecl* method)
	{
		if (!method_matches_spec(state_.spec, context_, *method))
		{
			return true;
		}

		if (llvm::isa<clang::CXXConstructorDecl>(method) ||
		    llvm::isa<clang::CXXDestructorDecl>(method))
		{
			state_.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_UNSUPPORTED_TARGET",
			    "constructors and destructors are outside Phase A inspect scope",
			    source_location(context_, method->getLocation()));
			return true;
		}

		if (method->isStatic())
		{
			state_.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_STATIC_METHOD",
			    "static member functions are outside Phase A inspect scope",
			    source_location(context_, method->getLocation()));
			return true;
		}

		if (!method->isThisDeclarationADefinition() && method->getDefinition() != nullptr)
		{
			return true;
		}

		if (auto const* function_template = method->getDescribedFunctionTemplate();
		    function_template != nullptr)
		{
			if (inspect_template_specializations(*function_template))
			{
				return true;
			}
			record_template_fallback(*method);
			return true;
		}

		if (method->isFunctionTemplateSpecialization())
		{
			if (state_.spec.template_arguments.empty() &&
			    state_.selected_template_without_explicit_args)
			{
				return true;
			}

			auto const* definition =
			    llvm::dyn_cast_or_null<clang::CXXMethodDecl>(method->getDefinition());
			if (definition == nullptr)
			{
				record_template_fallback(*method);
				return true;
			}

			PlanBuilder builder(context_, *definition);
			state_.plans.push_back(builder.build());
			if (state_.spec.template_arguments.empty())
			{
				state_.selected_template_without_explicit_args = true;
			}
			return true;
		}

		auto const* definition =
		    llvm::dyn_cast_or_null<clang::CXXMethodDecl>(method->getDefinition());
		if (definition == nullptr)
		{
			state_.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_METHOD_DECL_ONLY",
			    "matching method has no parsed definition",
			    source_location(context_, method->getLocation()));
			return true;
		}

		PlanBuilder builder(context_, *definition);
		state_.plans.push_back(builder.build());
		return true;
	}

   private:
	[[nodiscard]] bool inspect_template_specializations(
	    clang::FunctionTemplateDecl const& function_template)
	{
		for (auto const* specialization : function_template.specializations())
		{
			auto const* method = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(specialization);
			if (method == nullptr || !method_matches_spec(state_.spec, context_, *method))
			{
				continue;
			}

			if (state_.spec.template_arguments.empty() &&
			    state_.selected_template_without_explicit_args)
			{
				return true;
			}

			auto const* definition =
			    llvm::dyn_cast_or_null<clang::CXXMethodDecl>(method->getDefinition());
			if (definition == nullptr)
			{
				if (!state_.spec.template_arguments.empty())
				{
					record_template_fallback(*method);
					return true;
				}
				continue;
			}

			PlanBuilder builder(context_, *definition);
			state_.plans.push_back(builder.build());
			if (state_.spec.template_arguments.empty())
			{
				state_.selected_template_without_explicit_args = true;
			}
			return true;
		}

		return false;
	}

	void record_template_fallback(clang::CXXMethodDecl const& method)
	{
		if (state_.template_fallback_plan.has_value())
		{
			return;
		}

		PlanBuilder builder(context_, method);
		auto plan = builder.build();
		plan.result = "invalid-plan";
		plan.confidence = "low";
		plan.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_TEMPLATE_INSTANTIATION_NOT_FOUND",
		    "matching template method has no instantiated body for the requested template "
		    "arguments",
		    source_location(context_, method.getLocation()));
		state_.template_fallback_plan = std::move(plan);
	}

	clang::ASTContext& context_;
	ToolState& state_;
};

class InspectAstConsumer final : public clang::ASTConsumer
{
   public:
	explicit InspectAstConsumer(ToolState& state) : state_(state) {}

	void HandleTranslationUnit(clang::ASTContext& context) override
	{
		MethodLookupVisitor visitor(context, state_);
		visitor.TraverseDecl(context.getTranslationUnitDecl());
	}

   private:
	ToolState& state_;
};

class InspectFrontendAction final : public clang::ASTFrontendAction
{
   public:
	explicit InspectFrontendAction(ToolState& state) : state_(state) {}

	std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
	    clang::CompilerInstance& compiler_instance, llvm::StringRef input_file) override
	{
		(void)compiler_instance;
		(void)input_file;
		return std::make_unique<InspectAstConsumer>(state_);
	}

   private:
	ToolState& state_;
};

class InspectActionFactory final : public clang::tooling::FrontendActionFactory
{
   public:
	explicit InspectActionFactory(ToolState& state) : state_(state) {}

	std::unique_ptr<clang::FrontendAction> create() override
	{
		return std::make_unique<InspectFrontendAction>(state_);
	}

   private:
	ToolState& state_;
};

[[nodiscard]] std::filesystem::path comparable_source_path(std::string const& path)
{
	std::error_code error;
	auto absolute = std::filesystem::absolute(std::filesystem::path(path), error);
	if (error)
	{
		return std::filesystem::path(path).lexically_normal();
	}

	auto canonical = std::filesystem::weakly_canonical(absolute, error);
	if (error)
	{
		return absolute.lexically_normal();
	}

	return canonical.lexically_normal();
}

[[nodiscard]] bool source_file_is_in_database(
    clang::tooling::CompilationDatabase const& database, std::string const& requested_source)
{
	auto requested_path = comparable_source_path(requested_source);
	auto source_files = database.getAllFiles();
	return std::ranges::any_of(source_files,
	    [&requested_path](std::string const& source_file)
	    {
		    return comparable_source_path(source_file) == requested_path;
	    });
}

[[nodiscard]] std::vector<std::string> source_files(
    clang::tooling::CompilationDatabase const& database,
    std::optional<std::string> const& requested_source)
{
	if (requested_source.has_value())
	{
		return {*requested_source};
	}

	auto files = database.getAllFiles();
	std::ranges::sort(files);
	files.erase(std::unique(files.begin(), files.end()), files.end());
	return files;
}

[[nodiscard]] bool looks_like_constructor_or_destructor(MethodSpec const& spec)
{
	auto separator = spec.qualified_class_name.rfind("::");
	auto class_name = separator == std::string::npos
	    ? spec.qualified_class_name
	    : spec.qualified_class_name.substr(separator + 2);
	return spec.method_name == class_name || spec.method_name == "~" + class_name;
}

} // namespace

InspectResult inspect_method(InspectOptions const& options)
{
	InspectResult result;

	if (looks_like_constructor_or_destructor(options.method))
	{
		result.status = InspectStatus::kMethodResolutionError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_UNSUPPORTED_TARGET",
		    "constructors and destructors are outside Phase A inspect scope");
		return result;
	}

	auto compile_commands_path = std::filesystem::path(options.build_dir) / "compile_commands.json";
	if (!std::filesystem::exists(compile_commands_path))
	{
		result.status = InspectStatus::kCompileDatabaseError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_COMPILE_DB_MISSING",
		    "compile_commands.json was not found in build directory");
		return result;
	}

	std::string error;
	auto database =
	    clang::tooling::CompilationDatabase::loadFromDirectory(options.build_dir, error);
	if (!database)
	{
		result.status = InspectStatus::kCompileDatabaseError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_COMPILE_DB_LOAD_FAILED",
		    error.empty() ? "failed to load compile_commands.json" : error);
		return result;
	}

	if (options.source_file.has_value() &&
	    !source_file_is_in_database(*database, *options.source_file))
	{
		result.status = InspectStatus::kCompileDatabaseError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_COMPILE_DB_SOURCE_NOT_FOUND",
		    "--source is not present in compile_commands.json");
		return result;
	}

	auto files = source_files(*database, options.source_file);
	if (files.empty())
	{
		result.status = InspectStatus::kCompileDatabaseError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_COMPILE_DB_EMPTY",
		    "compile database does not contain source files");
		return result;
	}

	ToolState state;
	state.spec = options.method;
	clang::tooling::ClangTool tool(*database, files);
	tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
	    "-Wno-unknown-warning-option", clang::tooling::ArgumentInsertPosition::END));

	InspectActionFactory factory(state);
	auto tool_result = tool.run(&factory);
	if (tool_result != 0)
	{
		result.status = InspectStatus::kCompileDatabaseError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_CLANG_PARSE_FAILED",
		    "Clang failed to parse at least one translation unit");
		return result;
	}

	if (state.plans.empty() && state.template_fallback_plan.has_value())
	{
		result.plan = std::move(*state.template_fallback_plan);
		result.status = result.plan->diagnostics.has_errors()
		    ? InspectStatus::kExtractionPlanningError
		    : InspectStatus::kSuccess;
		return result;
	}

	if (state.plans.empty())
	{
		result.status = InspectStatus::kMethodResolutionError;
		if (state.diagnostics.has_errors())
		{
			result.diagnostics = std::move(state.diagnostics);
		}
		else
		{
			result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_METHOD_NOT_FOUND",
			    "no CXXMethodDecl matched the requested method spec");
		}
		return result;
	}

	if (state.plans.size() > 1U)
	{
		result.status = InspectStatus::kMethodResolutionError;
		result.diagnostics.add(DiagnosticSeverity::kError, "AZTECA_METHOD_AMBIGUOUS",
		    "method matched multiple translation units; pass --source to disambiguate");
		return result;
	}

	result.plan = std::move(state.plans.front());
	if (result.plan->diagnostics.has_errors() || result.plan->result == "invalid-plan")
	{
		result.status = InspectStatus::kExtractionPlanningError;
	}
	else
	{
		result.status = InspectStatus::kSuccess;
	}
	return result;
}

} // namespace azteca
