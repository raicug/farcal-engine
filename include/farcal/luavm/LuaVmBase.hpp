#pragma once

#include <sol/sol.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace farcal::luavm {

struct ExecutionResult {
  bool success = false;
  std::string message;
};

class LuaVmBase {
 public:
  using OutputCallback = std::function<void(std::string_view)>;

  virtual ~LuaVmBase() = default;

  virtual ExecutionResult execute(std::string_view script, OutputCallback output = {}) const;

 protected:
  virtual void configureState(sol::state& lua, const OutputCallback& output) const;
  virtual ExecutionResult onExecutionSuccess(const OutputCallback& output) const;
  virtual ExecutionResult onExecutionFailure(const sol::error& error, const OutputCallback& output) const;
  static std::string stringifyForOutput(const sol::object& value);
};

class BasicLuaVm final : public LuaVmBase {
 public:
  ~BasicLuaVm() override = default;
};

} // namespace farcal::luavm
