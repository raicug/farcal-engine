#include "farcal/luavm/LuaVmBase.hpp"

#include "farcal/luavm/LuaBindings.hpp"

#include <sstream>
#include <string>

namespace farcal::luavm {

ExecutionResult LuaVmBase::execute(std::string_view script, OutputCallback output) const {
  if (script.empty()) {
    return {false, "Script is empty."};
  }

  sol::state lua;
  configureState(lua, output);

  const sol::protected_function_result result =
      lua.safe_script(std::string(script), &sol::script_pass_on_error);
  if (!result.valid()) {
    const sol::error error = result;
    return onExecutionFailure(error, output);
  }

  return onExecutionSuccess(output);
}

void LuaVmBase::configureState(sol::state& lua, const OutputCallback& output) const {
  lua.open_libraries(
      sol::lib::base,
      sol::lib::package,
      sol::lib::coroutine,
      sol::lib::string,
      sol::lib::os,
      sol::lib::math,
      sol::lib::table,
      sol::lib::utf8);

  lua.set_function("print", [output](sol::variadic_args values) {
    if (!output) {
      return;
    }

    std::ostringstream stream;
    bool first = true;
    for (auto arg : values) {
      if (!first) {
        stream << ' ';
      }
      first = false;
      stream << stringifyForOutput(arg.get<sol::object>());
    }

    output(stream.str());
  });

  bindings::registerGlmTypes(lua);
  bindings::registerMemoryReadFunctions(lua);
}

ExecutionResult LuaVmBase::onExecutionSuccess(const OutputCallback& output) const {
  if (output) {
    output("[LUAVM] Execution succeeded.");
  }
  return {true, "Execution succeeded."};
}

ExecutionResult LuaVmBase::onExecutionFailure(const sol::error& error, const OutputCallback& output) const {
  if (output) {
    output(std::string("[LUAVM] Execution failed: ") + error.what());
  }
  return {false, error.what()};
}

std::string LuaVmBase::stringifyForOutput(const sol::object& value) {
  if (value.is<sol::lua_nil_t>()) {
    return "nil";
  }
  if (value.is<std::string>()) {
    return value.as<std::string>();
  }
  if (value.is<const char*>()) {
    return std::string(value.as<const char*>());
  }
  if (value.is<bool>()) {
    return value.as<bool>() ? "true" : "false";
  }
  if (value.is<lua_Integer>()) {
    return std::to_string(static_cast<long long>(value.as<lua_Integer>()));
  }
  if (value.is<lua_Number>()) {
    std::ostringstream stream;
    stream.precision(15);
    stream << static_cast<double>(value.as<lua_Number>());
    return stream.str();
  }

  const sol::state_view lua(value.lua_state());
  sol::protected_function tostringFunc = lua["tostring"];
  if (tostringFunc.valid()) {
    const sol::protected_function_result tostringResult = tostringFunc(value);
    if (tostringResult.valid()) {
      return tostringResult.get<std::string>();
    }
  }

  return "<value>";
}

} // namespace farcal::luavm
