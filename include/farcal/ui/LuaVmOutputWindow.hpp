#pragma once

#include <QMainWindow>
#include <QString>

class QPlainTextEdit;
class QWidget;

namespace farcal::ui {

class LuaVmOutputWindow final : public QMainWindow {
 public:
  explicit LuaVmOutputWindow(QWidget* parent = nullptr);

  void appendLine(const QString& line);

 private:
  void     applyTheme();
  void     configureWindow();
  QWidget* buildCentralArea();

  QPlainTextEdit* m_outputView = nullptr;
};

}  // namespace farcal::ui
