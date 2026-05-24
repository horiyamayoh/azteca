#include <gtest/gtest.h>

#include "azteca/Assert.hpp"

namespace
{

TEST(AztecaAssert, PassesWhenConditionTrue)
{
	int value = 0;
	AZTECA_ASSERT(value == 0, "value must be zero");
	SUCCEED();
}

TEST(AztecaAssertDeathTest, AbortsWithAZTE0011)
{
	GTEST_FLAG_SET(death_test_style, "threadsafe");
	EXPECT_DEATH({ AZTECA_ASSERT(false, "invariant body"); }, "AZT-E0011");
}

} // namespace
