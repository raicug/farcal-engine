#include "GlmBindingSections.hpp"

#include <glm/ext/quaternion_double.hpp>
#include <glm/ext/quaternion_float.hpp>

namespace farcal::luavm::bindings::detail {
namespace {

template <typename QuatT>
void registerQuatType(sol::table& glmTable, const char* name) {
  using V = typename QuatT::value_type;
  glmTable.new_usertype<QuatT>(
      name,
      sol::call_constructor,
      sol::constructors<QuatT(), QuatT(V, V, V, V)>(),
      "x", &QuatT::x,
      "y", &QuatT::y,
      "z", &QuatT::z,
      "w", &QuatT::w);
}

} // namespace

void registerGlmQuaternionTypes(sol::table& glmTable) {
  registerQuatType<glm::quat>(glmTable, "quat");
  registerQuatType<glm::dquat>(glmTable, "dquat");
}

} // namespace farcal::luavm::bindings::detail
