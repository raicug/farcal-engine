#pragma once

#include <QString>

#include <cstdint>

namespace farcal::ui {

struct LoopWriteEntry {
  std::uint64_t id = 0;
  std::uintptr_t address = 0;
  QString type;
  QString value;
  int intervalMs = 100;
  qint64 nextRunAtMs = 0;
  QString source;
};

}  // namespace farcal::ui
