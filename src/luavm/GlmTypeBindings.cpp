#include "farcal/luavm/LuaBindings.hpp"
#include "GlmBindingSections.hpp"

namespace farcal::luavm::bindings {

void registerGlmTypes(sol::state& lua) {
  auto glmTable = lua.create_named_table("glm");
  detail::registerGlmVectorTypes(glmTable);
  detail::registerGlmMatrixTypes(glmTable);
  detail::registerGlmQuaternionTypes(glmTable);
}

} // namespace farcal::luavm::bindings
