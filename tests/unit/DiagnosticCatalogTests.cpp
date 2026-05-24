#include <gtest/gtest.h>

#include <string>
#include <unordered_set>

#include "azteca/DiagnosticCatalog.hpp"

namespace
{

TEST(DiagnosticCatalog, AllIdsAreUniqueAndWellFormed)
{
	std::unordered_set<std::string> seen;
	for (auto const& entry : azteca::diagnostic_catalog())
	{
		std::string id{entry.id};
		ASSERT_TRUE(id.starts_with("AZT-E") || id.starts_with("AZT-W"))
		    << "unexpected id prefix: " << id;
		ASSERT_TRUE(seen.insert(id).second) << "duplicate id: " << id;
		EXPECT_FALSE(entry.summary.empty()) << id;
		EXPECT_FALSE(entry.detail.empty()) << id;
	}
}

TEST(DiagnosticCatalog, FindByIdHits)
{
	auto info = azteca::find_diagnostic("AZT-E0008");
	ASSERT_TRUE(info.has_value());
	EXPECT_EQ(info->id, "AZT-E0008");
}

TEST(DiagnosticCatalog, FindByIdMissesUnknown)
{
	EXPECT_FALSE(azteca::find_diagnostic("AZT-X9999").has_value());
	EXPECT_FALSE(azteca::find_diagnostic("").has_value());
}

TEST(DiagnosticCatalog, RenderExplainContainsIdAndDetail)
{
	auto info = azteca::find_diagnostic("AZT-E0008");
	ASSERT_TRUE(info.has_value());
	auto text = azteca::render_explain(*info);
	EXPECT_NE(text.find("AZT-E0008"), std::string::npos);
	EXPECT_NE(text.find(std::string{info->detail}), std::string::npos);
}

TEST(DiagnosticCatalog, PublicMappingTargetsRegisteredIds)
{
	// Every public id returned by public_diagnostic_id() must exist in the catalog.
	for (auto const& internal : {"AZTECA_COMPILE_DB_MISSING", "AZTECA_COMPILE_DB_LOAD_FAILED",
	         "AZTECA_COMPILE_DB_EMPTY", "AZTECA_CLANG_PARSE_FAILED", "AZTECA_METHOD_NOT_FOUND",
	         "AZTECA_TEMPLATE_INSTANTIATION_NOT_FOUND", "AZTECA_METHOD_AMBIGUOUS",
	         "AZTECA_UNSUPPORTED_TARGET", "AZTECA_STATIC_METHOD", "AZTECA_METHOD_DECL_ONLY",
	         "AZTECA_MMIR_LOCATION_MISSING", "AZTECA_MMIR_FIELD_DECL_MISSING",
	         "AZTECA_MMIR_BOUNDARY_CALLEE_MISSING", "AZTECA_MMIR_UNCLASSIFIED_CALL",
	         "AZTECA_MMIR_BARE_THIS"})
	{
		auto public_id = azteca::public_diagnostic_id(internal);
		ASSERT_TRUE(public_id.has_value()) << "unmapped internal code: " << internal;
		EXPECT_TRUE(azteca::find_diagnostic(*public_id).has_value())
		    << "public id not in catalog: " << *public_id << " (from " << internal << ')';
	}
}

TEST(DiagnosticCatalog, PublicMappingReturnsNulloptForUnknown)
{
	EXPECT_FALSE(azteca::public_diagnostic_id("NOT_A_REAL_CODE").has_value());
	EXPECT_FALSE(azteca::public_diagnostic_id("").has_value());
}

} // namespace
