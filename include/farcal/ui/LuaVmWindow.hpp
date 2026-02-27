#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

class QLabel;
class QPlainTextEdit;
class QWidget;

namespace farcal::luavm {
class LuaVmBase;
}

namespace farcal::ui {

class LuaVmOutputWindow;

class LuaVmWindow final : public QMainWindow {
 public:
  explicit LuaVmWindow(QWidget* parent = nullptr);
  ~LuaVmWindow() override;

 private:
  void     applyTheme();
  void     configureWindow();
  void     createMenuBar();
  QWidget* buildCentralArea();
  void     executeLuaScript();
  void     clearLuaScript();
  void     loadLuaScript();
  void     saveLuaScript();
  void     showOutputWindow();
  void     appendLuaOutput(const QString& line);

  QPlainTextEdit* m_editor = nullptr;
  QLabel*         m_statusLabel = nullptr;
  QString         m_currentFilePath;
  std::unique_ptr<farcal::luavm::LuaVmBase> m_vm;
  std::unique_ptr<LuaVmOutputWindow>        m_outputWindow;
};

}  // namespace farcal::ui
