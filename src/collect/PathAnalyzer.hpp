#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtCXX.h>
#include <clang/Basic/SourceLocation.h>

#include <functional>
#include <string>
#include <vector>

#include "InspectCollector.hpp"

namespace azteca::inspect_collect
{

class PathAnalyzer
{
   public:
	using EvidenceFactory = std::function<PlanEvidence(
	    std::string, std::string, clang::SourceRange, bool, std::string)>;
	using EventCollector = std::function<std::vector<PathEvent>(clang::Stmt const&)>;
	using ConservativeMarker = std::function<void(std::string, std::string, clang::SourceLocation)>;

	PathAnalyzer(clang::ASTContext& context, EvidenceFactory make_evidence,
	    EventCollector collect_events, ConservativeMarker mark_conservative);

	[[nodiscard]] std::vector<PathBurden> analyze(clang::Stmt const& body);

   private:
	struct SwitchSegment
	{
		std::string label;
		std::vector<clang::Stmt const*> statements;
	};

	[[nodiscard]] PlanEvidence make_evidence(std::string rule_id, std::string reason,
	    clang::SourceRange range, bool conservative = false,
	    std::string certainty = "certain") const;

	[[nodiscard]] std::vector<PathState> analyze_statement(clang::Stmt const& statement,
	    std::vector<PathState> states, std::vector<PathBurden>& paths, std::size_t& path_index);
	[[nodiscard]] std::vector<PathState> analyze_loop(clang::ForStmt const& statement,
	    clang::Stmt const* body, bool has_zero_iteration_path, std::vector<PathState> states);
	[[nodiscard]] std::vector<PathState> analyze_loop(clang::WhileStmt const& statement,
	    clang::Stmt const* body, bool has_zero_iteration_path, std::vector<PathState> states);
	[[nodiscard]] std::vector<PathState> analyze_loop(clang::DoStmt const& statement,
	    clang::Stmt const* body, bool has_zero_iteration_path, std::vector<PathState> states);
	[[nodiscard]] std::vector<PathState> analyze_loop(clang::CXXForRangeStmt const& statement,
	    clang::Stmt const* body, bool has_zero_iteration_path, std::vector<PathState> states);
	[[nodiscard]] std::vector<PathState> analyze_loop_body(clang::Stmt const& statement,
	    clang::Stmt const* body, std::vector<PathEvent> const& before_events,
	    std::vector<PathEvent> const& after_one_iteration_events, bool has_zero_iteration_path,
	    std::vector<PathState> states);
	[[nodiscard]] std::vector<PathState> analyze_switch(clang::SwitchStmt const& statement,
	    std::vector<PathState> states, std::vector<PathBurden>& paths, std::size_t& path_index);
	[[nodiscard]] std::vector<PathState> analyze_try(clang::CXXTryStmt const& statement,
	    std::vector<PathState> const& states, std::vector<PathBurden>& paths,
	    std::size_t& path_index);

	[[nodiscard]] std::string path_name(
	    clang::ReturnStmt const& return_statement, std::size_t index, PathState const& state) const;
	[[nodiscard]] static std::string path_name(std::string base_name, PathState const& state);
	[[nodiscard]] static std::vector<std::string> event_names(std::vector<PathEvent> const& events);
	static void append_path_label(std::vector<PathState>& states, std::string const& label);
	static void mark_path_conservative(std::vector<PathState>& states);
	static void append_loop_body_observations(
	    std::vector<PathState>& states, std::vector<std::string> const& observations);
	[[nodiscard]] std::vector<SwitchSegment> switch_segments(
	    clang::SwitchStmt const& statement) const;
	void append_switch_segment(
	    clang::Stmt const& statement, std::vector<SwitchSegment>& segments) const;
	[[nodiscard]] std::string case_label(clang::CaseStmt const& statement) const;
	[[nodiscard]] std::string catch_label(
	    clang::CXXCatchStmt const& statement, unsigned handler_index) const;
	[[nodiscard]] static bool is_conservative_control(clang::Stmt const& statement);

	clang::ASTContext& context_;
	EvidenceFactory make_evidence_;
	EventCollector collect_events_;
	ConservativeMarker mark_conservative_;
};

} // namespace azteca::inspect_collect
