#pragma once

#include <clang/AST/DeclCXX.h>

#include "azteca/ExtractionPlan.hpp"

namespace azteca::gtest_preview
{

void build(ExtractionPlan& plan, clang::CXXMethodDecl const& method);

} // namespace azteca::gtest_preview
