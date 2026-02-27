#pragma once

#include <sol/sol.hpp>

namespace farcal::luavm::bindings::detail {

void registerGlmVectorTypes(sol::table& glmTable);
void registerGlmMatrixTypes(sol::table& glmTable);
void registerGlmQuaternionTypes(sol::table& glmTable);

} // namespace farcal::luavm::bindings::detail
