#include "PathAnalyzer.hpp"

#include <clang/AST/ExprCXX.h>
#include <clang/AST/StmtCXX.h>
#include <llvm/Support/Casting.h>

#include <utility>

#include "../frontend/InspectSource.hpp"

namespace azteca::inspect_collect
{

PathAnalyzer::PathAnalyzer(clang::ASTContext& context, EvidenceFactory make_evidence,
    EventCollector collect_events, ConservativeMarker mark_conservative)
    : context_(context),
      make_evidence_(std::move(make_evidence)),
      collect_events_(std::move(collect_events)),
      mark_conservative_(std::move(mark_conservative))
{
}

std::vector<PathBurden> PathAnalyzer::analyze(clang::Stmt const& body)
{
	std::vector<PathBurden> paths;
	auto states = std::vector<PathState>{{}};
	auto path_index = std::size_t{1};
	auto fallthrough_states = analyze_statement(body, std::move(states), paths, path_index);

	for (auto const& state : fallthrough_states)
	{
		paths.push_back(
		    build_path_burden(path_name("path_" + std::to_string(path_index), state), state.events,
		        make_evidence(state.conservative ? "PATH-CONSERVATIVE" : "PATH-DFS",
		            state.conservative ? "fallthrough path includes conservative control flow"
		                               : "fallthrough path accumulated by DFS",
		            body.getSourceRange(), state.conservative,
		            state.conservative ? "conservative" : "certain"),
		        state.loop_body_observations));
		++path_index;
	}

	return paths;
}

PlanEvidence PathAnalyzer::make_evidence(std::string rule_id, std::string reason,
    clang::SourceRange range, bool conservative, std::string certainty) const
{
	return make_evidence_(
	    std::move(rule_id), std::move(reason), range, conservative, std::move(certainty));
}

std::vector<PathState> PathAnalyzer::analyze_statement(clang::Stmt const& statement,
    std::vector<PathState> states, std::vector<PathBurden>& paths, std::size_t& path_index)
{
	if (auto const* compound = llvm::dyn_cast<clang::CompoundStmt>(&statement))
	{
		for (auto const* child : compound->body())
		{
			if (states.empty())
			{
				break;
			}
			states = analyze_statement(*child, std::move(states), paths, path_index);
		}
		return states;
	}

	if (auto const* return_statement = llvm::dyn_cast<clang::ReturnStmt>(&statement))
	{
		append_events(states, collect_events_(statement));
		for (auto const& state : states)
		{
			paths.push_back(
			    build_path_burden(path_name(*return_statement, path_index, state), state.events,
			        make_evidence(state.conservative ? "PATH-CONSERVATIVE" : "PATH-DFS",
			            state.conservative ? "return path includes conservative control flow"
			                               : "return path accumulated by DFS",
			            return_statement->getSourceRange(), state.conservative,
			            state.conservative ? "conservative" : "certain"),
			        state.loop_body_observations));
			++path_index;
		}
		return {};
	}

	if (llvm::isa<clang::CXXThrowExpr>(&statement))
	{
		append_events(states, collect_events_(statement));
		return {};
	}

	if (auto const* if_statement = llvm::dyn_cast<clang::IfStmt>(&statement))
	{
		if (if_statement->getInit() != nullptr)
		{
			append_events(states, collect_events_(*if_statement->getInit()));
		}
		if (if_statement->getCond() != nullptr)
		{
			append_events(states, collect_events_(*if_statement->getCond()));
		}

		auto then_states = if_statement->getThen() == nullptr
		    ? states
		    : analyze_statement(*if_statement->getThen(), states, paths, path_index);
		auto else_states = if_statement->getElse() == nullptr
		    ? std::move(states)
		    : analyze_statement(*if_statement->getElse(), std::move(states), paths, path_index);
		then_states.insert(then_states.end(), else_states.begin(), else_states.end());
		return then_states;
	}

	if (auto const* for_statement = llvm::dyn_cast<clang::ForStmt>(&statement))
	{
		return analyze_loop(*for_statement, for_statement->getBody(), true, std::move(states));
	}

	if (auto const* while_statement = llvm::dyn_cast<clang::WhileStmt>(&statement))
	{
		return analyze_loop(*while_statement, while_statement->getBody(), true, std::move(states));
	}

	if (auto const* do_statement = llvm::dyn_cast<clang::DoStmt>(&statement))
	{
		return analyze_loop(*do_statement, do_statement->getBody(), false, std::move(states));
	}

	if (auto const* range_statement = llvm::dyn_cast<clang::CXXForRangeStmt>(&statement))
	{
		return analyze_loop(*range_statement, range_statement->getBody(), true, std::move(states));
	}

	if (auto const* switch_statement = llvm::dyn_cast<clang::SwitchStmt>(&statement))
	{
		return analyze_switch(*switch_statement, std::move(states), paths, path_index);
	}

	if (auto const* try_statement = llvm::dyn_cast<clang::CXXTryStmt>(&statement))
	{
		return analyze_try(*try_statement, states, paths, path_index);
	}

	if (is_conservative_control(statement))
	{
		append_events(states, collect_events_(statement));
		for (auto& state : states)
		{
			state.conservative = true;
		}
		mark_conservative_("AZTECA_PATH_CONSERVATIVE",
		    "loop, switch, or try statement is represented as a single conservative path",
		    statement.getBeginLoc());
		return states;
	}

	append_events(states, collect_events_(statement));
	return states;
}

std::vector<PathState> PathAnalyzer::analyze_loop(clang::ForStmt const& statement,
    clang::Stmt const* body, bool has_zero_iteration_path, std::vector<PathState> states)
{
	std::vector<PathEvent> before_events;
	std::vector<PathEvent> after_one_iteration_events;
	if (statement.getInit() != nullptr)
	{
		append_events(before_events, collect_events_(*statement.getInit()));
	}
	if (statement.getCond() != nullptr)
	{
		append_events(before_events, collect_events_(*statement.getCond()));
	}
	if (statement.getInc() != nullptr)
	{
		append_events(after_one_iteration_events, collect_events_(*statement.getInc()));
	}
	return analyze_loop_body(statement, body, before_events, after_one_iteration_events,
	    has_zero_iteration_path, std::move(states));
}

std::vector<PathState> PathAnalyzer::analyze_loop(clang::WhileStmt const& statement,
    clang::Stmt const* body, bool has_zero_iteration_path, std::vector<PathState> states)
{
	std::vector<PathEvent> before_events;
	if (statement.getCond() != nullptr)
	{
		append_events(before_events, collect_events_(*statement.getCond()));
	}
	return analyze_loop_body(
	    statement, body, before_events, {}, has_zero_iteration_path, std::move(states));
}

std::vector<PathState> PathAnalyzer::analyze_loop(clang::DoStmt const& statement,
    clang::Stmt const* body, bool has_zero_iteration_path, std::vector<PathState> states)
{
	std::vector<PathEvent> after_one_iteration_events;
	if (statement.getCond() != nullptr)
	{
		append_events(after_one_iteration_events, collect_events_(*statement.getCond()));
	}
	return analyze_loop_body(statement, body, {}, after_one_iteration_events,
	    has_zero_iteration_path, std::move(states));
}

std::vector<PathState> PathAnalyzer::analyze_loop(clang::CXXForRangeStmt const& statement,
    clang::Stmt const* body, bool has_zero_iteration_path, std::vector<PathState> states)
{
	std::vector<PathEvent> before_events;
	if (statement.getRangeInit() != nullptr)
	{
		append_events(before_events, collect_events_(*statement.getRangeInit()));
	}
	return analyze_loop_body(
	    statement, body, before_events, {}, has_zero_iteration_path, std::move(states));
}

std::vector<PathState> PathAnalyzer::analyze_loop_body(clang::Stmt const& statement,
    clang::Stmt const* body, std::vector<PathEvent> const& before_events,
    std::vector<PathEvent> const& after_one_iteration_events, bool has_zero_iteration_path,
    std::vector<PathState> states)
{
	append_events(states, before_events);
	auto body_events = body == nullptr ? std::vector<PathEvent>{} : collect_events_(*body);
	auto loop_body_observations = event_names(body_events);
	std::vector<PathState> result;

	if (has_zero_iteration_path)
	{
		auto zero_states = states;
		append_path_label(zero_states, "loop_zero_iterations");
		mark_path_conservative(zero_states);
		result.insert(result.end(), zero_states.begin(), zero_states.end());
	}

	auto one_or_more_states = std::move(states);
	append_path_label(one_or_more_states, "loop_one_or_more_iterations");
	append_events(one_or_more_states, body_events);
	append_events(one_or_more_states, after_one_iteration_events);
	append_loop_body_observations(one_or_more_states, loop_body_observations);
	mark_path_conservative(one_or_more_states);
	result.insert(result.end(), one_or_more_states.begin(), one_or_more_states.end());

	mark_conservative_("AZTECA_PATH_CONSERVATIVE",
	    "loop statement is approximated as zero/one-or-more execution paths",
	    statement.getBeginLoc());
	return result;
}

std::vector<PathState> PathAnalyzer::analyze_switch(clang::SwitchStmt const& statement,
    std::vector<PathState> states, std::vector<PathBurden>& paths, std::size_t& path_index)
{
	if (statement.getCond() != nullptr)
	{
		append_events(states, collect_events_(*statement.getCond()));
	}

	auto segments = switch_segments(statement);
	if (segments.empty())
	{
		append_events(states, collect_events_(statement));
		mark_path_conservative(states);
		mark_conservative_("AZTECA_PATH_CONSERVATIVE",
		    "switch statement could not be segmented and remains conservative",
		    statement.getBeginLoc());
		return states;
	}

	std::vector<PathState> result;
	auto has_default = false;
	for (auto segment_index = std::size_t{0}; segment_index < segments.size(); ++segment_index)
	{
		has_default = has_default || segments[segment_index].label == "switch_default";
		auto active_states = states;
		append_path_label(active_states, segments[segment_index].label);

		for (auto fallthrough_index = segment_index; fallthrough_index < segments.size();
		     ++fallthrough_index)
		{
			auto stopped_by_break = false;
			for (auto const* child : segments[fallthrough_index].statements)
			{
				if (llvm::isa<clang::BreakStmt>(child))
				{
					stopped_by_break = true;
					break;
				}
				active_states =
				    analyze_statement(*child, std::move(active_states), paths, path_index);
				if (active_states.empty())
				{
					break;
				}
			}

			if (stopped_by_break)
			{
				result.insert(result.end(), active_states.begin(), active_states.end());
				break;
			}

			if (active_states.empty())
			{
				break;
			}

			if (fallthrough_index + 1U == segments.size())
			{
				result.insert(result.end(), active_states.begin(), active_states.end());
			}
		}
	}

	if (!has_default)
	{
		auto no_match_states = states;
		append_path_label(no_match_states, "switch_no_match");
		result.insert(result.end(), no_match_states.begin(), no_match_states.end());
	}

	return result;
}

std::vector<PathState> PathAnalyzer::analyze_try(clang::CXXTryStmt const& statement,
    std::vector<PathState> const& states, std::vector<PathBurden>& paths, std::size_t& path_index)
{
	std::vector<PathState> result;
	auto try_states = states;
	append_path_label(try_states, "try_no_exception");
	auto try_result =
	    analyze_statement(*statement.getTryBlock(), std::move(try_states), paths, path_index);
	result.insert(result.end(), try_result.begin(), try_result.end());

	for (auto handler_index = unsigned{0}; handler_index < statement.getNumHandlers();
	     ++handler_index)
	{
		auto const* handler = statement.getHandler(handler_index);
		auto catch_states = states;
		append_path_label(catch_states, catch_label(*handler, handler_index));
		mark_path_conservative(catch_states);
		auto catch_result = analyze_statement(
		    *handler->getHandlerBlock(), std::move(catch_states), paths, path_index);
		result.insert(result.end(), catch_result.begin(), catch_result.end());
	}

	mark_conservative_("AZTECA_PATH_CONSERVATIVE",
	    "try/catch paths are split into no-exception and conservative catch paths",
	    statement.getBeginLoc());
	return result;
}

std::string PathAnalyzer::path_name(
    clang::ReturnStmt const& return_statement, std::size_t index, PathState const& state) const
{
	auto text = inspect_frontend::source_text(context_, return_statement.getSourceRange());
	if (text.empty())
	{
		return path_name("path_" + std::to_string(index), state);
	}

	return path_name(sanitize_identifier(text), state);
}

std::string PathAnalyzer::path_name(std::string base_name, PathState const& state)
{
	for (auto const& label : state.labels)
	{
		base_name += "__" + label;
	}
	return base_name;
}

std::vector<std::string> PathAnalyzer::event_names(std::vector<PathEvent> const& events)
{
	std::vector<std::string> names;
	names.reserve(events.size());
	for (auto const& event : events)
	{
		names.push_back(event.name);
	}
	deduplicate_strings(names);
	return names;
}

void PathAnalyzer::append_path_label(std::vector<PathState>& states, std::string const& label)
{
	for (auto& state : states)
	{
		state.labels.push_back(label);
	}
}

void PathAnalyzer::mark_path_conservative(std::vector<PathState>& states)
{
	for (auto& state : states)
	{
		state.conservative = true;
	}
}

void PathAnalyzer::append_loop_body_observations(
    std::vector<PathState>& states, std::vector<std::string> const& observations)
{
	for (auto& state : states)
	{
		state.loop_body_observations.insert(
		    state.loop_body_observations.end(), observations.begin(), observations.end());
		deduplicate_strings(state.loop_body_observations);
	}
}

std::vector<PathAnalyzer::SwitchSegment> PathAnalyzer::switch_segments(
    clang::SwitchStmt const& statement) const
{
	std::vector<SwitchSegment> segments;
	auto const* body = statement.getBody();
	if (auto const* compound = llvm::dyn_cast_or_null<clang::CompoundStmt>(body))
	{
		for (auto const* child : compound->body())
		{
			append_switch_segment(*child, segments);
		}
		return segments;
	}

	if (body != nullptr)
	{
		append_switch_segment(*body, segments);
	}
	return segments;
}

void PathAnalyzer::append_switch_segment(
    clang::Stmt const& statement, std::vector<SwitchSegment>& segments) const
{
	if (auto const* case_statement = llvm::dyn_cast<clang::CaseStmt>(&statement))
	{
		segments.push_back({.label = case_label(*case_statement), .statements = {}});
		if (case_statement->getSubStmt() != nullptr)
		{
			append_switch_segment(*case_statement->getSubStmt(), segments);
		}
		return;
	}

	if (auto const* default_statement = llvm::dyn_cast<clang::DefaultStmt>(&statement))
	{
		segments.push_back({.label = "switch_default", .statements = {}});
		if (default_statement->getSubStmt() != nullptr)
		{
			append_switch_segment(*default_statement->getSubStmt(), segments);
		}
		return;
	}

	if (!segments.empty())
	{
		segments.back().statements.push_back(&statement);
	}
}

std::string PathAnalyzer::case_label(clang::CaseStmt const& statement) const
{
	auto text = statement.getLHS() == nullptr
	    ? std::string{}
	    : inspect_frontend::source_text(context_, statement.getLHS()->getSourceRange());
	if (text.empty())
	{
		text = "case";
	}
	return "switch_case_" + sanitize_identifier(text);
}

std::string PathAnalyzer::catch_label(
    clang::CXXCatchStmt const& statement, unsigned handler_index) const
{
	if (statement.getExceptionDecl() == nullptr)
	{
		return "catch_all";
	}
	auto type =
	    sanitize_identifier(inspect_frontend::type_string(context_, statement.getCaughtType()));
	if (type.empty())
	{
		return "catch_" + std::to_string(handler_index + 1U);
	}
	return "catch_" + type;
}

bool PathAnalyzer::is_conservative_control(clang::Stmt const& statement)
{
	return llvm::isa<clang::ForStmt>(statement) || llvm::isa<clang::WhileStmt>(statement) ||
	    llvm::isa<clang::DoStmt>(statement) || llvm::isa<clang::CXXForRangeStmt>(statement);
}

} // namespace azteca::inspect_collect
