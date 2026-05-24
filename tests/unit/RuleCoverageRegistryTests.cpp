#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

#include "plan/RuleCoverageRegistry.hpp"

namespace
{

TEST(RuleCoverageRegistry, PhaseARegistryHasContiguousUniqueLoweringRules)
{
	auto coverage = azteca::plan::phase_a_rule_coverage();

	ASSERT_EQ(coverage.size(), 49U);

	std::set<std::string> ids;
	for (auto const& entry : coverage)
	{
		EXPECT_TRUE(ids.insert(entry.rule_id).second) << "duplicate rule id: " << entry.rule_id;
		EXPECT_FALSE(entry.note.empty()) << entry.rule_id;
		EXPECT_FALSE(entry.observed) << entry.rule_id;
	}

	for (auto index = 1; index <= 49; ++index)
	{
		char buffer[sizeof("LR-049")] = {};
		std::snprintf(buffer, sizeof(buffer), "LR-%03d", index);
		EXPECT_TRUE(ids.contains(buffer)) << "missing rule id: " << buffer;
	}
}

TEST(RuleCoverageRegistry, PhaseARegistryKeepsRepresentativeHandlingStable)
{
	auto coverage = azteca::plan::phase_a_rule_coverage();
	auto handling_for = [&coverage](std::string const& rule_id)
	{
		auto iterator = std::ranges::find_if(coverage,
		    [&rule_id](azteca::RuleCoverage const& entry)
		    {
			    return entry.rule_id == rule_id;
		    });
		return iterator == coverage.end() ? azteca::ConstructHandling::kNotMeaningful
		                                  : iterator->handling;
	};

	EXPECT_EQ(handling_for("LR-001"), azteca::ConstructHandling::kSupported);
	EXPECT_EQ(handling_for("LR-012"), azteca::ConstructHandling::kModeled);
	EXPECT_EQ(handling_for("LR-013"), azteca::ConstructHandling::kBoundary);
	EXPECT_EQ(handling_for("LR-015"), azteca::ConstructHandling::kConservative);
	EXPECT_EQ(handling_for("LR-038"), azteca::ConstructHandling::kNotYetImplemented);
}

} // namespace
