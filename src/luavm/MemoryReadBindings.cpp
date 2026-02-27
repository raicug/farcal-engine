#include "farcal/luavm/LuaBindings.hpp"

#include "farcal/luavm/AttachedProcessContext.hpp"
#include "farcal/memory/MemoryReader.hpp"

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
#include <glm/ext/quaternion_double.hpp>
#include <glm/ext/quaternion_float.hpp>
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

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <tlhelp32.h>
#  include <windows.h>
#endif

namespace farcal::luavm::bindings {
namespace {

std::uint32_t resolveProcessId(std::optional<std::uint32_t> explicitProcessId = std::nullopt) {
  if (explicitProcessId.has_value() && explicitProcessId.value() != 0) {
    return explicitProcessId.value();
  }
  return AttachedProcessContext::attachedProcessId();
}

template <typename T>
sol::object readAsObject(sol::state_view lua, std::uint32_t processId, std::uintptr_t address) {
  if (processId == 0) {
    return sol::make_object(lua, sol::lua_nil);
  }

  memory::MemoryReader reader;
  if (!reader.attach(processId)) {
    return sol::make_object(lua, sol::lua_nil);
  }

  const auto value = reader.read<T>(address);
  if (!value.has_value()) {
    return sol::make_object(lua, sol::lua_nil);
  }

  return sol::make_object(lua, value.value());
}

template <typename T>
sol::object readAsObjectAttached(sol::state_view lua, std::uintptr_t address) {
  return readAsObject<T>(lua, resolveProcessId(), address);
}

template <typename T>
void bindReadFunction(sol::table& memoryTable, sol::state_view lua, const char* name) {
  memoryTable.set_function(
      name,
      sol::overload([lua](std::uintptr_t address)
                        -> sol::object { return readAsObjectAttached<T>(lua, address); },
                    [lua](std::uint32_t processId, std::uintptr_t address) -> sol::object {
                      return readAsObject<T>(lua, processId, address);
                    }));
}

std::string normalizeTypeName(std::string_view typeName) {
  std::string normalized;
  normalized.reserve(typeName.size());
  for (const unsigned char ch : typeName) {
    if (std::isalnum(ch) != 0) {
      normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  return normalized;
}

std::size_t sanitizeMaxLength(std::size_t maxLength) {
  constexpr std::size_t kDefaultMaxLength = 256;
  constexpr std::size_t kHardMaxLength = 1U << 20U;
  if (maxLength == 0) {
    return kDefaultMaxLength;
  }
  return (maxLength > kHardMaxLength) ? kHardMaxLength : maxLength;
}

std::optional<std::string> readCStringValue(std::uint32_t processId,
                                            std::uintptr_t address,
                                            std::size_t    maxLength) {
  if (processId == 0 || address == 0) {
    return std::nullopt;
  }

  memory::MemoryReader reader;
  if (!reader.attach(processId)) {
    return std::nullopt;
  }

  const std::size_t boundedMaxLength = sanitizeMaxLength(maxLength);
  constexpr std::size_t kChunkSize   = 256;
  std::vector<char>     chunk(kChunkSize);
  std::string           result;
  result.reserve((boundedMaxLength < kChunkSize) ? boundedMaxLength : kChunkSize);

  std::size_t offset = 0;
  while (offset < boundedMaxLength) {
    const std::size_t remaining = boundedMaxLength - offset;
    const std::size_t toRead    = (remaining < kChunkSize) ? remaining : kChunkSize;

    if (!reader.readBytes(address + offset, chunk.data(), toRead)) {
      return (offset == 0) ? std::nullopt : std::optional<std::string>(result);
    }

    for (std::size_t i = 0; i < toRead; ++i) {
      if (chunk[i] == '\0') {
        return result;
      }
      result.push_back(chunk[i]);
    }

    offset += toRead;
  }

  return result;
}

sol::object readStringAsObject(sol::state_view lua,
                               std::uint32_t   processId,
                               std::uintptr_t  address,
                               std::size_t     maxLength) {
  const auto value = readCStringValue(processId, address, maxLength);
  if (!value.has_value()) {
    return sol::make_object(lua, sol::lua_nil);
  }
  return sol::make_object(lua, value.value());
}

sol::object readByTypeName(sol::state_view  lua,
                           std::uint32_t    processId,
                           std::uintptr_t   address,
                           std::string_view typeName) {
  const std::string normalized = normalizeTypeName(typeName);

#define FARCAL_LUA_READ_IF(type_name, cpp_type)             \
  if (normalized == type_name) {                            \
    return readAsObject<cpp_type>(lua, processId, address); \
  }

  FARCAL_LUA_READ_IF("bool", bool)

  FARCAL_LUA_READ_IF("i8", std::int8_t)
  FARCAL_LUA_READ_IF("int8", std::int8_t)
  FARCAL_LUA_READ_IF("s8", std::int8_t)
  FARCAL_LUA_READ_IF("char", std::int8_t)
  FARCAL_LUA_READ_IF("u8", std::uint8_t)
  FARCAL_LUA_READ_IF("uint8", std::uint8_t)
  FARCAL_LUA_READ_IF("byte", std::uint8_t)

  FARCAL_LUA_READ_IF("i16", std::int16_t)
  FARCAL_LUA_READ_IF("int16", std::int16_t)
  FARCAL_LUA_READ_IF("s16", std::int16_t)
  FARCAL_LUA_READ_IF("short", std::int16_t)
  FARCAL_LUA_READ_IF("u16", std::uint16_t)
  FARCAL_LUA_READ_IF("uint16", std::uint16_t)
  FARCAL_LUA_READ_IF("unsignedshort", std::uint16_t)
  FARCAL_LUA_READ_IF("word", std::uint16_t)

  FARCAL_LUA_READ_IF("i32", std::int32_t)
  FARCAL_LUA_READ_IF("int32", std::int32_t)
  FARCAL_LUA_READ_IF("s32", std::int32_t)
  FARCAL_LUA_READ_IF("int", std::int32_t)
  FARCAL_LUA_READ_IF("integer", std::int32_t)
  FARCAL_LUA_READ_IF("long", std::int32_t)
  FARCAL_LUA_READ_IF("u32", std::uint32_t)
  FARCAL_LUA_READ_IF("uint32", std::uint32_t)
  FARCAL_LUA_READ_IF("uint", std::uint32_t)
  FARCAL_LUA_READ_IF("unsignedint", std::uint32_t)
  FARCAL_LUA_READ_IF("ulong", std::uint32_t)
  FARCAL_LUA_READ_IF("dword", std::uint32_t)

  FARCAL_LUA_READ_IF("i64", std::int64_t)
  FARCAL_LUA_READ_IF("int64", std::int64_t)
  FARCAL_LUA_READ_IF("s64", std::int64_t)
  FARCAL_LUA_READ_IF("longlong", std::int64_t)
  FARCAL_LUA_READ_IF("u64", std::uint64_t)
  FARCAL_LUA_READ_IF("uint64", std::uint64_t)
  FARCAL_LUA_READ_IF("qword", std::uint64_t)
  FARCAL_LUA_READ_IF("ulonglong", std::uint64_t)

  FARCAL_LUA_READ_IF("f32", float)
  FARCAL_LUA_READ_IF("float", float)
  FARCAL_LUA_READ_IF("f64", double)
  FARCAL_LUA_READ_IF("double", double)

  FARCAL_LUA_READ_IF("ptr", std::uintptr_t)
  FARCAL_LUA_READ_IF("pointer", std::uintptr_t)
  FARCAL_LUA_READ_IF("uintptr", std::uintptr_t)
  FARCAL_LUA_READ_IF("usize", std::uintptr_t)

  FARCAL_LUA_READ_IF("vec1", glm::vec1)
  FARCAL_LUA_READ_IF("vec2", glm::vec2)
  FARCAL_LUA_READ_IF("vec3", glm::vec3)
  FARCAL_LUA_READ_IF("vec4", glm::vec4)
  FARCAL_LUA_READ_IF("dvec1", glm::dvec1)
  FARCAL_LUA_READ_IF("dvec2", glm::dvec2)
  FARCAL_LUA_READ_IF("dvec3", glm::dvec3)
  FARCAL_LUA_READ_IF("dvec4", glm::dvec4)
  FARCAL_LUA_READ_IF("ivec1", glm::ivec1)
  FARCAL_LUA_READ_IF("ivec2", glm::ivec2)
  FARCAL_LUA_READ_IF("ivec3", glm::ivec3)
  FARCAL_LUA_READ_IF("ivec4", glm::ivec4)
  FARCAL_LUA_READ_IF("uvec1", glm::uvec1)
  FARCAL_LUA_READ_IF("uvec2", glm::uvec2)
  FARCAL_LUA_READ_IF("uvec3", glm::uvec3)
  FARCAL_LUA_READ_IF("uvec4", glm::uvec4)
  FARCAL_LUA_READ_IF("bvec1", glm::bvec1)
  FARCAL_LUA_READ_IF("bvec2", glm::bvec2)
  FARCAL_LUA_READ_IF("bvec3", glm::bvec3)
  FARCAL_LUA_READ_IF("bvec4", glm::bvec4)

  FARCAL_LUA_READ_IF("mat2", glm::mat2)
  FARCAL_LUA_READ_IF("mat3", glm::mat3)
  FARCAL_LUA_READ_IF("mat4", glm::mat4)
  FARCAL_LUA_READ_IF("mat2x3", glm::mat2x3)
  FARCAL_LUA_READ_IF("mat2x4", glm::mat2x4)
  FARCAL_LUA_READ_IF("mat3x2", glm::mat3x2)
  FARCAL_LUA_READ_IF("mat3x4", glm::mat3x4)
  FARCAL_LUA_READ_IF("mat4x2", glm::mat4x2)
  FARCAL_LUA_READ_IF("mat4x3", glm::mat4x3)
  FARCAL_LUA_READ_IF("dmat2", glm::dmat2)
  FARCAL_LUA_READ_IF("dmat3", glm::dmat3)
  FARCAL_LUA_READ_IF("dmat4", glm::dmat4)
  FARCAL_LUA_READ_IF("dmat2x3", glm::dmat2x3)
  FARCAL_LUA_READ_IF("dmat2x4", glm::dmat2x4)
  FARCAL_LUA_READ_IF("dmat3x2", glm::dmat3x2)
  FARCAL_LUA_READ_IF("dmat3x4", glm::dmat3x4)
  FARCAL_LUA_READ_IF("dmat4x2", glm::dmat4x2)
  FARCAL_LUA_READ_IF("dmat4x3", glm::dmat4x3)
  FARCAL_LUA_READ_IF("quat", glm::quat)
  FARCAL_LUA_READ_IF("dquat", glm::dquat)

#undef FARCAL_LUA_READ_IF

  if (normalized == "string" || normalized == "cstring" || normalized == "str") {
    return readStringAsObject(lua, processId, address, 256);
  }

  return sol::make_object(lua, sol::lua_nil);
}

void registerScalarFunctions(sol::table& memoryTable, sol::state_view lua) {
  bindReadFunction<bool>(memoryTable, lua, "read_bool");

  bindReadFunction<std::int8_t>(memoryTable, lua, "read_i8");
  bindReadFunction<std::int8_t>(memoryTable, lua, "read_int8");
  bindReadFunction<std::uint8_t>(memoryTable, lua, "read_u8");
  bindReadFunction<std::uint8_t>(memoryTable, lua, "read_uint8");
  bindReadFunction<std::uint8_t>(memoryTable, lua, "read_byte");
  bindReadFunction<std::int16_t>(memoryTable, lua, "read_i16");
  bindReadFunction<std::int16_t>(memoryTable, lua, "read_int16");
  bindReadFunction<std::int16_t>(memoryTable, lua, "read_short");
  bindReadFunction<std::uint16_t>(memoryTable, lua, "read_u16");
  bindReadFunction<std::uint16_t>(memoryTable, lua, "read_uint16");
  bindReadFunction<std::uint16_t>(memoryTable, lua, "read_ushort");
  bindReadFunction<std::uint16_t>(memoryTable, lua, "read_word");
  bindReadFunction<std::int32_t>(memoryTable, lua, "read_i32");
  bindReadFunction<std::int32_t>(memoryTable, lua, "read_int32");
  bindReadFunction<std::int32_t>(memoryTable, lua, "read_int");
  bindReadFunction<std::int32_t>(memoryTable, lua, "read_long");
  bindReadFunction<std::uint32_t>(memoryTable, lua, "read_u32");
  bindReadFunction<std::uint32_t>(memoryTable, lua, "read_uint32");
  bindReadFunction<std::uint32_t>(memoryTable, lua, "read_uint");
  bindReadFunction<std::uint32_t>(memoryTable, lua, "read_ulong");
  bindReadFunction<std::uint32_t>(memoryTable, lua, "read_dword");
  bindReadFunction<std::int64_t>(memoryTable, lua, "read_i64");
  bindReadFunction<std::int64_t>(memoryTable, lua, "read_int64");
  bindReadFunction<std::int64_t>(memoryTable, lua, "read_longlong");
  bindReadFunction<std::uint64_t>(memoryTable, lua, "read_u64");
  bindReadFunction<std::uint64_t>(memoryTable, lua, "read_uint64");
  bindReadFunction<std::uint64_t>(memoryTable, lua, "read_ulonglong");
  bindReadFunction<std::uint64_t>(memoryTable, lua, "read_qword");
  bindReadFunction<float>(memoryTable, lua, "read_f32");
  bindReadFunction<double>(memoryTable, lua, "read_f64");
  bindReadFunction<std::uintptr_t>(memoryTable, lua, "read_ptr");
}

void registerStringFunctions(sol::table& memoryTable, sol::state_view lua) {
  memoryTable.set_function(
      "read_string",
      sol::overload(
          [lua](std::uintptr_t address) -> sol::object {
            return readStringAsObject(lua, resolveProcessId(), address, 256);
          },
          [lua](std::uintptr_t address, std::size_t maxLength) -> sol::object {
            return readStringAsObject(lua, resolveProcessId(), address, maxLength);
          },
          [lua](std::uint32_t processId, std::uintptr_t address) -> sol::object {
            return readStringAsObject(lua, processId, address, 256);
          },
          [lua](std::uint32_t processId, std::uintptr_t address, std::size_t maxLength)
              -> sol::object { return readStringAsObject(lua, processId, address, maxLength); }));

  memoryTable["read_cstring"] = memoryTable["read_string"];
}

void registerGlmFunctions(sol::table& memoryTable, sol::state_view lua) {
  bindReadFunction<glm::vec1>(memoryTable, lua, "read_vec1");
  bindReadFunction<glm::vec2>(memoryTable, lua, "read_vec2");
  bindReadFunction<glm::vec3>(memoryTable, lua, "read_vec3");
  bindReadFunction<glm::vec4>(memoryTable, lua, "read_vec4");

  bindReadFunction<glm::dvec1>(memoryTable, lua, "read_dvec1");
  bindReadFunction<glm::dvec2>(memoryTable, lua, "read_dvec2");
  bindReadFunction<glm::dvec3>(memoryTable, lua, "read_dvec3");
  bindReadFunction<glm::dvec4>(memoryTable, lua, "read_dvec4");

  bindReadFunction<glm::ivec1>(memoryTable, lua, "read_ivec1");
  bindReadFunction<glm::ivec2>(memoryTable, lua, "read_ivec2");
  bindReadFunction<glm::ivec3>(memoryTable, lua, "read_ivec3");
  bindReadFunction<glm::ivec4>(memoryTable, lua, "read_ivec4");

  bindReadFunction<glm::uvec1>(memoryTable, lua, "read_uvec1");
  bindReadFunction<glm::uvec2>(memoryTable, lua, "read_uvec2");
  bindReadFunction<glm::uvec3>(memoryTable, lua, "read_uvec3");
  bindReadFunction<glm::uvec4>(memoryTable, lua, "read_uvec4");

  bindReadFunction<glm::bvec1>(memoryTable, lua, "read_bvec1");
  bindReadFunction<glm::bvec2>(memoryTable, lua, "read_bvec2");
  bindReadFunction<glm::bvec3>(memoryTable, lua, "read_bvec3");
  bindReadFunction<glm::bvec4>(memoryTable, lua, "read_bvec4");

  bindReadFunction<glm::mat2>(memoryTable, lua, "read_mat2");
  bindReadFunction<glm::mat3>(memoryTable, lua, "read_mat3");
  bindReadFunction<glm::mat4>(memoryTable, lua, "read_mat4");
  bindReadFunction<glm::mat2x3>(memoryTable, lua, "read_mat2x3");
  bindReadFunction<glm::mat2x4>(memoryTable, lua, "read_mat2x4");
  bindReadFunction<glm::mat3x2>(memoryTable, lua, "read_mat3x2");
  bindReadFunction<glm::mat3x4>(memoryTable, lua, "read_mat3x4");
  bindReadFunction<glm::mat4x2>(memoryTable, lua, "read_mat4x2");
  bindReadFunction<glm::mat4x3>(memoryTable, lua, "read_mat4x3");

  bindReadFunction<glm::dmat2>(memoryTable, lua, "read_dmat2");
  bindReadFunction<glm::dmat3>(memoryTable, lua, "read_dmat3");
  bindReadFunction<glm::dmat4>(memoryTable, lua, "read_dmat4");
  bindReadFunction<glm::dmat2x3>(memoryTable, lua, "read_dmat2x3");
  bindReadFunction<glm::dmat2x4>(memoryTable, lua, "read_dmat2x4");
  bindReadFunction<glm::dmat3x2>(memoryTable, lua, "read_dmat3x2");
  bindReadFunction<glm::dmat3x4>(memoryTable, lua, "read_dmat3x4");
  bindReadFunction<glm::dmat4x2>(memoryTable, lua, "read_dmat4x2");
  bindReadFunction<glm::dmat4x3>(memoryTable, lua, "read_dmat4x3");

  bindReadFunction<glm::quat>(memoryTable, lua, "read_quat");
  bindReadFunction<glm::dquat>(memoryTable, lua, "read_dquat");
}

std::optional<std::uintptr_t> getMainModuleBaseAddress(std::uint32_t processId) {
  if (processId == 0) {
    return std::nullopt;
  }

#ifdef _WIN32
  const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                                     static_cast<DWORD>(processId));
  if (snapshot == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }

  MODULEENTRY32W moduleEntry{};
  moduleEntry.dwSize = sizeof(moduleEntry);
  const BOOL ok      = ::Module32FirstW(snapshot, &moduleEntry);
  ::CloseHandle(snapshot);

  if (ok == FALSE) {
    return std::nullopt;
  }

  return reinterpret_cast<std::uintptr_t>(moduleEntry.modBaseAddr);
#else
  (void)processId;
  return std::nullopt;
#endif
}

sol::object moduleBaseAsObject(sol::state_view              lua,
                               std::optional<std::uint32_t> explicitProcessId) {
  const std::uint32_t processId  = resolveProcessId(explicitProcessId);
  const auto          moduleBase = getMainModuleBaseAddress(processId);
  if (!moduleBase.has_value()) {
    return sol::make_object(lua, sol::lua_nil);
  }
  return sol::make_object(lua, moduleBase.value());
}

}  // namespace

void registerMemoryReadFunctions(sol::state& lua) {
  auto                  memoryTable = lua.create_named_table("memory");
  const sol::state_view state(lua.lua_state());

  registerScalarFunctions(memoryTable, state);
  registerStringFunctions(memoryTable, state);
  registerGlmFunctions(memoryTable, state);

  memoryTable.set_function(
      "current_pid", []() -> std::uint32_t { return AttachedProcessContext::attachedProcessId(); });

  memoryTable.set_function(
      "module_base",
      sol::overload([state]() -> sol::object { return moduleBaseAsObject(state, std::nullopt); },
                    [state](std::uint32_t processId) -> sol::object {
                      return moduleBaseAsObject(state, processId);
                    }));

  memoryTable.set_function(
      "read",
      sol::overload(
          [state](std::uintptr_t address, std::string_view typeName) -> sol::object {
            return readByTypeName(state, resolveProcessId(), address, typeName);
          },
          [state](std::uint32_t processId, std::uintptr_t address, std::string_view typeName)
              -> sol::object { return readByTypeName(state, processId, address, typeName); }));

  memoryTable["read_type"]  = memoryTable["read"];
  memoryTable["read_typed"] = memoryTable["read"];
}

}  // namespace farcal::luavm::bindings
