#pragma once

#include <QKeySequence>

namespace farcal::ui {

struct KeybindSettings {
  QKeySequence openStructureDissector;
  QKeySequence openLuaVm;
  QKeySequence openRttiScanner;
  QKeySequence openStringScanner;
  QKeySequence attachToProcess;
  QKeySequence attachSavedProcess;

  static KeybindSettings defaults() {
    return {
        QKeySequence(QStringLiteral("Ctrl+D")),
        QKeySequence(QStringLiteral("Ctrl+L")),
        QKeySequence(QStringLiteral("Ctrl+X")),
        QKeySequence(QStringLiteral("Ctrl+S")),
        QKeySequence(QStringLiteral("Ctrl+M")),
        QKeySequence(QStringLiteral("Ctrl+Shift+M")),
    };
  }
};

} // namespace farcal::ui
