#include "farcal/ui/LogWindow.hpp"
#include "q_lit.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace farcal::ui {

LogWindow::LogWindow(QWidget* parent) : QMainWindow(parent) {
  applyTheme();
  configureWindow();
}

void LogWindow::appendLog(const QString& message) {
  if (m_logText == nullptr) {
    return;
  }

  const QString timestamp = QDateTime::currentDateTime().toString(("hh:mm:ss.zzz"));
  const QString formatted = QString(("[%1] %2")).arg(timestamp, message);

  m_logText->append(formatted);

  // Auto-scroll to bottom
  auto cursor = m_logText->textCursor();
  cursor.movePosition(QTextCursor::End);
  m_logText->setTextCursor(cursor);
}

void LogWindow::applyTheme() {
  setStyleSheet((R"(QMainWindow {
  background-color: #22242a;
  color: #e8eaed;
}
QTextEdit {
  background-color: #1a1c21;
  color: #e8eaed;
  border: 1px solid #4a4e58;
  font-family: 'Consolas', 'Courier New', monospace;
  font-size: 9pt;
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
})"));
}

void LogWindow::configureWindow() {
  setWindowTitle(("Debug Log"));
  resize(800, 600);

  auto* central = new QWidget(this);
  auto* layout  = new QVBoxLayout(central);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(8);

  m_logText = new QTextEdit(central);
  m_logText->setReadOnly(true);
  layout->addWidget(m_logText, 1);

  auto* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_clearButton = new QPushButton(("Clear"), central);
  connect(m_clearButton, &QPushButton::clicked, this, [this]() {
    if (m_logText != nullptr) {
      m_logText->clear();
    }
  });
  buttonLayout->addWidget(m_clearButton);

  layout->addLayout(buttonLayout);

  setCentralWidget(central);
}

}  // namespace farcal::ui
