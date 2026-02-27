#include "GlmBindingSections.hpp"

#include <glm/ext/matrix_double2x2.hpp>
#include <glm/ext/matrix_double2x3.hpp>
#include <glm/ext/matrix_double2x4.hpp>
#include <glm/ext/matrix_double3x2.hpp>
#include <glm/ext/matrix_double3x3.hpp>
#include <glm/ext/matrix_double3x4.hpp>
#include <glm/ext/matrix_double4x2.hpp>
#include <glm/ext/matrix_double4x3.hpp>
#include <glm/ext/matrix_double4x4.hpp>
#include <glm/ext/matrix_float2x2.hpp>
#include <glm/ext/matrix_float2x3.hpp>
#include <glm/ext/matrix_float2x4.hpp>
#include <glm/ext/matrix_float3x2.hpp>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float3x4.hpp>
#include <glm/ext/matrix_float4x2.hpp>
#include <glm/ext/matrix_float4x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>

namespace farcal::luavm::bindings::detail {
namespace {

template <typename MatT>
void registerMatType(sol::table& glmTable, const char* name) {
  using V = typename MatT::value_type;
  glmTable.new_usertype<MatT>(
      name,
      sol::call_constructor,
      sol::constructors<MatT(), MatT(V)>(),
      "get",
      [](const MatT& m, int column, int row) -> V {
        if (column < 0 || row < 0 || column >= static_cast<int>(MatT::length())) {
          return V{};
        }
        using Col = typename MatT::col_type;
        if (row >= static_cast<int>(Col::length())) {
          return V{};
        }
        return m[static_cast<typename MatT::length_type>(column)][static_cast<typename MatT::length_type>(row)];
      },
      "set",
      [](MatT& m, int column, int row, V value) {
        if (column < 0 || row < 0 || column >= static_cast<int>(MatT::length())) {
          return;
        }
        using Col = typename MatT::col_type;
        if (row >= static_cast<int>(Col::length())) {
          return;
        }
        m[static_cast<typename MatT::length_type>(column)][static_cast<typename MatT::length_type>(row)] = value;
      });
}

} // namespace

void registerGlmMatrixTypes(sol::table& glmTable) {
  registerMatType<glm::mat2>(glmTable, "mat2");
  registerMatType<glm::mat3>(glmTable, "mat3");
  registerMatType<glm::mat4>(glmTable, "mat4");
  registerMatType<glm::mat2x3>(glmTable, "mat2x3");
  registerMatType<glm::mat2x4>(glmTable, "mat2x4");
  registerMatType<glm::mat3x2>(glmTable, "mat3x2");
  registerMatType<glm::mat3x4>(glmTable, "mat3x4");
  registerMatType<glm::mat4x2>(glmTable, "mat4x2");
  registerMatType<glm::mat4x3>(glmTable, "mat4x3");

  registerMatType<glm::dmat2>(glmTable, "dmat2");
  registerMatType<glm::dmat3>(glmTable, "dmat3");
  registerMatType<glm::dmat4>(glmTable, "dmat4");
  registerMatType<glm::dmat2x3>(glmTable, "dmat2x3");
  registerMatType<glm::dmat2x4>(glmTable, "dmat2x4");
  registerMatType<glm::dmat3x2>(glmTable, "dmat3x2");
  registerMatType<glm::dmat3x4>(glmTable, "dmat3x4");
  registerMatType<glm::dmat4x2>(glmTable, "dmat4x2");
  registerMatType<glm::dmat4x3>(glmTable, "dmat4x3");
}

} // namespace farcal::luavm::bindings::detail
