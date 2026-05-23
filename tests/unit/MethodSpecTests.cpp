#include <gtest/gtest.h>

#include "azteca/MethodSpec.hpp"

namespace
{

TEST(MethodSpecParser, ParsesConstLvalueQualifiedMethod)
{
	auto result = azteca::parse_method_spec("ns::C::m(int, Foo const&) const volatile &");

	ASSERT_TRUE(result.spec.has_value()) << result.error;
	EXPECT_EQ(result.spec->qualified_class_name, "ns::C");
	EXPECT_EQ(result.spec->method_name, "m");
	ASSERT_EQ(result.spec->parameter_types.size(), 2U);
	EXPECT_EQ(result.spec->parameter_types[0], "int");
	EXPECT_EQ(result.spec->parameter_types[1], "Foo const&");
	EXPECT_TRUE(result.spec->is_const);
	EXPECT_TRUE(result.spec->is_volatile);
	EXPECT_EQ(result.spec->ref_qualifier, azteca::RefQualifier::kLValue);
}

TEST(MethodSpecParser, ParsesVolatileRvalueQualifiedMethod)
{
	auto result = azteca::parse_method_spec("ns::C::m() volatile &&");

	ASSERT_TRUE(result.spec.has_value()) << result.error;
	EXPECT_FALSE(result.spec->is_const);
	EXPECT_TRUE(result.spec->is_volatile);
	EXPECT_EQ(result.spec->ref_qualifier, azteca::RefQualifier::kRValue);
}

TEST(MethodSpecParser, RejectsTemplateMethodInPhaseA)
{
	auto result = azteca::parse_method_spec("C::f<int>(int)");

	EXPECT_FALSE(result.spec.has_value());
	EXPECT_NE(result.error.find("template"), std::string::npos);
}

TEST(MethodSpecParser, RejectsOperatorOverloadButAllowsOperatorPrefixIdentifier)
{
	auto rejected = azteca::parse_method_spec("C::operator+(C const&) const");
	EXPECT_FALSE(rejected.spec.has_value());
	EXPECT_NE(rejected.error.find("operator"), std::string::npos);

	auto accepted = azteca::parse_method_spec("C::operator_path()");
	ASSERT_TRUE(accepted.spec.has_value()) << accepted.error;
	EXPECT_EQ(accepted.spec->method_name, "operator_path");
}

TEST(MethodSpecParser, NormalizesTypeForMatching)
{
	EXPECT_EQ(azteca::normalize_type_for_match("class ns::Id const &"), "ns::Idconst&");
	EXPECT_EQ(azteca::normalize_type_for_match("struct Value *"), "Value*");
}

} // namespace
