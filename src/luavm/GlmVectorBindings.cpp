#include "GlmBindingSections.hpp"

#include <glm/ext/vector_bool1.hpp>
#include <glm/ext/vector_bool2.hpp>
#include <glm/ext/vector_bool3.hpp>
#include <glm/ext/vector_bool4.hpp>
#include <glm/ext/vector_double1.hpp>
#include <glm/ext/vector_double2.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_double4.hpp>
#include <glm/ext/vector_float1.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_int1.hpp>
#include <glm/ext/vector_int2.hpp>
#include <glm/ext/vector_int3.hpp>
#include <glm/ext/vector_int4.hpp>
#include <glm/ext/vector_uint1.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/ext/vector_uint3.hpp>
#include <glm/ext/vector_uint4.hpp>

namespace farcal::luavm::bindings::detail {
namespace {

template <typename VecT>
void registerVec1Type(sol::table& glmTable, const char* name) {
  using V = typename VecT::value_type;
  glmTable.new_usertype<VecT>(
      name,
      sol::call_constructor,
      sol::constructors<VecT(), VecT(V)>(),
      "x", &VecT::x);
}

template <typename VecT>
void registerVec2Type(sol::table& glmTable, const char* name) {
  using V = typename VecT::value_type;
  glmTable.new_usertype<VecT>(
      name,
      sol::call_constructor,
      sol::constructors<VecT(), VecT(V), VecT(V, V)>(),
      "x", &VecT::x,
      "y", &VecT::y);
}

template <typename VecT>
void registerVec3Type(sol::table& glmTable, const char* name) {
  using V = typename VecT::value_type;
  glmTable.new_usertype<VecT>(
      name,
      sol::call_constructor,
      sol::constructors<VecT(), VecT(V), VecT(V, V, V)>(),
      "x", &VecT::x,
      "y", &VecT::y,
      "z", &VecT::z);
}

template <typename VecT>
void registerVec4Type(sol::table& glmTable, const char* name) {
  using V = typename VecT::value_type;
  glmTable.new_usertype<VecT>(
      name,
      sol::call_constructor,
      sol::constructors<VecT(), VecT(V), VecT(V, V, V, V)>(),
      "x", &VecT::x,
      "y", &VecT::y,
      "z", &VecT::z,
      "w", &VecT::w);
}

} // namespace

void registerGlmVectorTypes(sol::table& glmTable) {
  registerVec1Type<glm::vec1>(glmTable, "vec1");
  registerVec2Type<glm::vec2>(glmTable, "vec2");
  registerVec3Type<glm::vec3>(glmTable, "vec3");
  registerVec4Type<glm::vec4>(glmTable, "vec4");

  registerVec1Type<glm::dvec1>(glmTable, "dvec1");
  registerVec2Type<glm::dvec2>(glmTable, "dvec2");
  registerVec3Type<glm::dvec3>(glmTable, "dvec3");
  registerVec4Type<glm::dvec4>(glmTable, "dvec4");

  registerVec1Type<glm::ivec1>(glmTable, "ivec1");
  registerVec2Type<glm::ivec2>(glmTable, "ivec2");
  registerVec3Type<glm::ivec3>(glmTable, "ivec3");
  registerVec4Type<glm::ivec4>(glmTable, "ivec4");

  registerVec1Type<glm::uvec1>(glmTable, "uvec1");
  registerVec2Type<glm::uvec2>(glmTable, "uvec2");
  registerVec3Type<glm::uvec3>(glmTable, "uvec3");
  registerVec4Type<glm::uvec4>(glmTable, "uvec4");

  registerVec1Type<glm::bvec1>(glmTable, "bvec1");
  registerVec2Type<glm::bvec2>(glmTable, "bvec2");
  registerVec3Type<glm::bvec3>(glmTable, "bvec3");
  registerVec4Type<glm::bvec4>(glmTable, "bvec4");
}

} // namespace farcal::luavm::bindings::detail
