#pragma once

#include "farcal/ui/SettingsTypes.hpp"

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QKeySequenceEdit;

namespace farcal::ui {

class SettingsWindow final : public QDialog {
  Q_OBJECT

 public:
  explicit SettingsWindow(QWidget* parent = nullptr);

  void setKeybindSettings(const KeybindSettings& settings);
  [[nodiscard]] KeybindSettings keybindSettings() const;

 signals:
  void keybindsSaved(const farcal::ui::KeybindSettings& settings);

 private:
  QWidget* buildKeybindPage();

  QListWidget* m_categoryList = nullptr;
  QStackedWidget* m_pages = nullptr;

  QKeySequenceEdit* m_structureDissectorKeybind = nullptr;
  QKeySequenceEdit* m_luaVmKeybind = nullptr;
  QKeySequenceEdit* m_rttiKeybind = nullptr;
  QKeySequenceEdit* m_stringScannerKeybind = nullptr;
  QKeySequenceEdit* m_attachProcessKeybind = nullptr;
  QKeySequenceEdit* m_attachSavedProcessKeybind = nullptr;
};

}  // namespace farcal::ui
