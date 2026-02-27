#pragma once

#include <cstdint>

namespace farcal::luavm {

class AttachedProcessContext final {
 public:
  static void          setAttachedProcessId(std::uint32_t processId) noexcept;
  static std::uint32_t attachedProcessId() noexcept;
  static void          clear() noexcept;
};

} // namespace farcal::luavm
