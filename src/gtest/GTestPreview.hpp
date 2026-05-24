#pragma once

#include <clang/AST/DeclCXX.h>

#include <string_view>

#include "azteca/ExtractionPlan.hpp"

namespace azteca::gtest_preview
{

void build(ExtractionPlan& plan, std::string_view qualified_name, bool returns_void);
void build(ExtractionPlan& plan, clang::CXXMethodDecl const& method);

} // namespace azteca::gtest_preview
