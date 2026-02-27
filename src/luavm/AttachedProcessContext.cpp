#include "farcal/luavm/AttachedProcessContext.hpp"

#include <atomic>

namespace farcal::luavm {
namespace {

std::atomic<std::uint32_t> g_attachedProcessId{0};

} // namespace

void AttachedProcessContext::setAttachedProcessId(std::uint32_t processId) noexcept {
  g_attachedProcessId.store(processId, std::memory_order_relaxed);
}

std::uint32_t AttachedProcessContext::attachedProcessId() noexcept {
  return g_attachedProcessId.load(std::memory_order_relaxed);
}

void AttachedProcessContext::clear() noexcept {
  setAttachedProcessId(0);
}

} // namespace farcal::luavm
