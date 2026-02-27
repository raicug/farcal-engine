#include "farcal/ui/LuaVmOutputWindow.hpp"

#include "q_lit.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace farcal::ui {

LuaVmOutputWindow::LuaVmOutputWindow(QWidget* parent) : QMainWindow(parent) {
  applyTheme();
  configureWindow();
}

void LuaVmOutputWindow::appendLine(const QString& line) {
  if (m_outputView == nullptr || line.isEmpty()) {
    return;
  }
  m_outputView->appendPlainText(line);
}

void LuaVmOutputWindow::applyTheme() {
  setStyleSheet((R"(QMainWindow {
  background-color: #22242a;
  color: #e8eaed;
}
QFrame#panel {
  background-color: #2b2e36;
  border: 1px solid #4a4e58;
  border-radius: 6px;
}
QPushButton {
  background-color: #444851;
  border: 1px solid #656a76;
  border-radius: 4px;
  color: #f2f4f7;
  padding: 4px 10px;
}
QPushButton:hover {
  background-color: #525762;
}
QPushButton:pressed {
  background-color: #3a3e47;
}
QPlainTextEdit {
  background-color: #121419;
  color: #e8eaed;
  border: 1px solid #4a4e58;
  selection-background-color: #4e5f82;
})"));
}

void LuaVmOutputWindow::configureWindow() {
  resize(760, 460);
  setWindowTitle(("LuaVM Output"));
  setCentralWidget(buildCentralArea());
}

QWidget* LuaVmOutputWindow::buildCentralArea() {
  auto* root       = new QWidget(this);
  auto* rootLayout = new QVBoxLayout(root);
  rootLayout->setContentsMargins(10, 10, 10, 10);
  rootLayout->setSpacing(8);

  auto* panel = new QFrame(root);
  panel->setObjectName(("panel"));
  auto* panelLayout = new QVBoxLayout(panel);
  panelLayout->setContentsMargins(10, 10, 10, 10);
  panelLayout->setSpacing(8);

  m_outputView = new QPlainTextEdit(panel);
  m_outputView->setReadOnly(true);
  m_outputView->setPlaceholderText(("LuaVM output..."));
  panelLayout->addWidget(m_outputView, 1);

  auto* row = new QHBoxLayout();
  row->addStretch(1);
  auto* clearButton = new QPushButton(("Clear"), panel);
  connect(clearButton, &QPushButton::clicked, this, [this]() {
    if (m_outputView != nullptr) {
      m_outputView->clear();
    }
  });
  row->addWidget(clearButton);
  panelLayout->addLayout(row);

  rootLayout->addWidget(panel, 1);
  return root;
}

}  // namespace farcal::ui
