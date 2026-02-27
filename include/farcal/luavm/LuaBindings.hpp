#pragma once

#include <sol/sol.hpp>

namespace farcal::luavm::bindings {

void registerGlmTypes(sol::state& lua);
void registerMemoryReadFunctions(sol::state& lua);

} // namespace farcal::luavm::bindings
