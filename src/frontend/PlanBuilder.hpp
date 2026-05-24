#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtCXX.h>

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "../collect/InspectCollector.hpp"
#include "azteca/ExtractionPlan.hpp"

namespace azteca
{

class PlanBuilder : public clang::RecursiveASTVisitor<PlanBuilder>
{
   public:
	PlanBuilder(clang::ASTContext& context, clang::CXXMethodDecl const& method);

	[[nodiscard]] ExtractionPlan build();

	bool VisitMemberExpr(clang::MemberExpr* member);

	bool VisitDeclRefExpr(clang::DeclRefExpr* reference);

	bool VisitVarDecl(clang::VarDecl* variable);

	bool VisitBinaryOperator(clang::BinaryOperator* binary_operator);

	bool VisitConditionalOperator(clang::ConditionalOperator* conditional_operator);

	bool VisitBinaryConditionalOperator(clang::BinaryConditionalOperator* conditional_operator);

	bool VisitUnaryOperator(clang::UnaryOperator* unary_operator);

	bool VisitIfStmt(clang::IfStmt* if_statement);

	bool VisitSwitchStmt(clang::SwitchStmt* switch_statement);

	bool VisitForStmt(clang::ForStmt* statement);

	bool VisitWhileStmt(clang::WhileStmt* statement);

	bool VisitDoStmt(clang::DoStmt* statement);

	bool VisitCXXForRangeStmt(clang::CXXForRangeStmt* statement);

	bool VisitBreakStmt(clang::BreakStmt* statement);

	bool VisitContinueStmt(clang::ContinueStmt* statement);

	bool VisitReturnStmt(clang::ReturnStmt* return_statement);

	bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr* call);

	bool VisitCallExpr(clang::CallExpr* call);

	bool VisitCXXOperatorCallExpr(clang::CXXOperatorCallExpr* call);

	bool VisitLambdaExpr(clang::LambdaExpr* lambda);

	bool VisitDependentScopeDeclRefExpr(clang::DependentScopeDeclRefExpr* expression);

	bool VisitCXXDependentScopeMemberExpr(clang::CXXDependentScopeMemberExpr* expression);

	bool VisitCXXThrowExpr(clang::CXXThrowExpr* throw_expression);

	bool VisitCXXTryStmt(clang::CXXTryStmt* try_statement);

	bool VisitCXXDynamicCastExpr(clang::CXXDynamicCastExpr* cast);

	bool VisitCXXReinterpretCastExpr(clang::CXXReinterpretCastExpr* cast);

	bool VisitCXXTypeidExpr(clang::CXXTypeidExpr* typeid_expression);

	bool VisitCXXDeleteExpr(clang::CXXDeleteExpr* delete_expression);

	bool VisitCXXNewExpr(clang::CXXNewExpr* new_expression);

	bool VisitCXXConstructExpr(clang::CXXConstructExpr* construct_expression);

	bool VisitCXXDefaultArgExpr(clang::CXXDefaultArgExpr* expression);

	bool VisitCXXStaticCastExpr(clang::CXXStaticCastExpr* cast);

	bool VisitCXXFunctionalCastExpr(clang::CXXFunctionalCastExpr* cast);

	bool VisitCStyleCastExpr(clang::CStyleCastExpr* cast);

	bool VisitCXXConstCastExpr(clang::CXXConstCastExpr* cast);

	bool VisitUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr* expression);

	bool VisitCXXNoexceptExpr(clang::CXXNoexceptExpr* expression);

	bool VisitDecompositionDecl(clang::DecompositionDecl* declaration);

	bool VisitCoroutineBodyStmt(clang::CoroutineBodyStmt* statement);

	[[nodiscard]] std::string overload_suffix(clang::FunctionDecl const& callee) const;

	[[nodiscard]] std::string stable_port_name(
	    std::string const& base_name, clang::FunctionDecl const& callee) const;

	[[nodiscard]] std::optional<DependencyPort> dependency_port_for_call(
	    clang::CXXMemberCallExpr const& call);

	[[nodiscard]] std::optional<DependencyPort> dependency_port_for_call(
	    clang::CallExpr const& call);

	[[nodiscard]] std::vector<inspect_collect::PathEvent> collect_events(
	    clang::Stmt const& statement);

   private:
	struct StructuredBindingSource
	{
		std::string source_dependency;
		std::optional<std::string> local_type;
	};

	void initialize_rule_coverage();

	void mark_rule_observed(std::string const& rule_id);

	void finalize_deterministic_order();

	void record_method_qualifiers();

	[[nodiscard]] PlanEvidence make_evidence(std::string rule_id, std::string reason,
	    clang::SourceRange range, bool conservative = false,
	    std::string certainty = "certain") const;

	void mark_conservative(std::string code, std::string message, clang::SourceLocation location);

	bool record_loop(clang::Stmt const& statement, clang::SourceLocation location,
	    std::string_view label, std::string rule_id);

	bool record_supported_cast(
	    clang::ExplicitCastExpr const& cast, clang::SourceLocation location, std::string label);

	void add_semantic_feature(
	    std::string name, ConstructHandling handling, std::string detail, PlanEvidence evidence);

	void add_construct(std::string construct, ConstructHandling handling, std::string reason,
	    std::vector<std::string> fallbacks, clang::SourceLocation location, PlanEvidence evidence);

	void add_envelope_requirement(EnvelopeRequirementKind kind, std::string reason,
	    std::string source, PlanEvidence evidence);

	void record_macro_if_needed(clang::SourceRange range);

	[[nodiscard]] std::vector<std::string> call_argument_types(clang::CallExpr const& call) const;

	[[nodiscard]] static bool contains_this_argument(clang::CallExpr const& call);

	[[nodiscard]] bool is_shape_call(clang::CXXMemberCallExpr const& call) const;

	[[nodiscard]] static clang::CXXMethodDecl const* member_function_pointer_target(
	    clang::Expr const* expression);

	[[nodiscard]] FieldAccess classify_field_access(clang::MemberExpr const& member);

	[[nodiscard]] bool is_dependency_object_base(clang::MemberExpr const& member);

	void add_receiver_field(
	    clang::FieldDecl const& field, FieldAccess access, PlanEvidence evidence);

	void remove_dependency_object_fields();

	void record_dependency_local(clang::VarDecl const& variable);

	void add_shape_observation(clang::MemberExpr const& member);

	[[nodiscard]] bool record_structured_binding_shape(clang::DecompositionDecl const& declaration);

	[[nodiscard]] std::optional<StructuredBindingSource> structured_binding_source(
	    clang::DecompositionDecl const& declaration);

	[[nodiscard]] static clang::Expr const* unwrap_decomposition_initializer(
	    clang::Expr const* expression);

	void record_constructor_local_shape(clang::CXXConstructExpr const& expression,
	    clang::CXXConstructorDecl const& constructor, PlanEvidence const& evidence);

	[[nodiscard]] clang::VarDecl const* local_constructor_target(
	    clang::CXXConstructExpr const& expression) const;

	void add_dependency_port(DependencyPort port);

	void add_object_ref_requirement(std::string reason, std::string expression,
	    clang::SourceLocation location, clang::SourceRange range, std::string rule_id);

	void add_unclassified_call(clang::CallExpr const& call);

	void add_mmir(MmirNodeKind kind, std::string label, clang::SourceLocation location,
	    clang::SourceRange range, PlanEvidence const& evidence, std::string semantic_id = {},
	    std::string original_symbol = {});

	clang::ASTContext& context_;

	clang::CXXMethodDecl const& method_;

	ExtractionPlan plan_;

	std::map<clang::FieldDecl const*, std::size_t> receiver_field_indices_;

	std::set<clang::FieldDecl const*> dependency_fields_;

	std::set<std::string> macro_ranges_;

	std::map<clang::VarDecl const*, std::string> local_types_;

	std::map<clang::VarDecl const*, std::string> local_dependency_sources_;
};

} // namespace azteca
