#include "farcal/ui/LoopWriteManagerWindow.hpp"
#include "q_lit.hpp"

#include <QAbstractItemView>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace farcal::ui {

LoopWriteManagerWindow::LoopWriteManagerWindow(QWidget* parent) : QMainWindow(parent) {
  applyTheme();
  configureWindow();
}

void LoopWriteManagerWindow::setEntries(const std::vector<LoopWriteEntry>& entries) {
  if (m_table == nullptr || m_statusLabel == nullptr) {
    return;
  }

  m_table->setRowCount(0);
  m_table->setRowCount(static_cast<int>(entries.size()));

  for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
    const auto& entry = entries[static_cast<std::size_t>(row)];

    auto* idItem = new QTableWidgetItem(QString::number(entry.id));
    idItem->setData(Qt::UserRole, static_cast<qulonglong>(entry.id));
    idItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    auto* addressItem = new QTableWidgetItem(
        QString(("0x%1")).arg(static_cast<qulonglong>(entry.address), 0, 16).toUpper());
    addressItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    auto* typeItem = new QTableWidgetItem(entry.type);
    typeItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    auto* valueItem = new QTableWidgetItem(entry.value);
    valueItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    auto* intervalItem = new QTableWidgetItem(QString::number(entry.intervalMs));
    intervalItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    auto* sourceItem = new QTableWidgetItem(entry.source);
    sourceItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    m_table->setItem(row, 0, idItem);
    m_table->setItem(row, 1, addressItem);
    m_table->setItem(row, 2, typeItem);
    m_table->setItem(row, 3, valueItem);
    m_table->setItem(row, 4, intervalItem);
    m_table->setItem(row, 5, sourceItem);
  }

  m_statusLabel->setText(QString(("Active loop writes: %1")).arg(entries.size()));
}

void LoopWriteManagerWindow::applyTheme() {
  setStyleSheet((R"(QMainWindow {
  background-color: #22242a;
  color: #e8eaed;
}
QFrame#panel {
  background-color: #2b2e36;
  border: 1px solid #4a4e58;
  border-radius: 6px;
}
QLabel {
  color: #e8eaed;
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
QTableWidget {
  background-color: #1a1c21;
  color: #e8eaed;
  border: 1px solid #4a4e58;
  gridline-color: #353841;
}
QHeaderView::section {
  background-color: #35373d;
  color: #e8eaed;
  border: 1px solid #4f535e;
  padding: 5px;
})"));
}

void LoopWriteManagerWindow::configureWindow() {
  resize(900, 500);
  setWindowTitle(("Loop Value Manager"));
  setCentralWidget(buildCentralArea());
}

QWidget* LoopWriteManagerWindow::buildCentralArea() {
  auto* root       = new QWidget(this);
  auto* rootLayout = new QVBoxLayout(root);
  rootLayout->setContentsMargins(10, 10, 10, 10);
  rootLayout->setSpacing(8);

  auto* panel = new QFrame(root);
  panel->setObjectName(("panel"));
  auto* panelLayout = new QVBoxLayout(panel);
  panelLayout->setContentsMargins(10, 10, 10, 10);
  panelLayout->setSpacing(8);

  m_statusLabel = new QLabel(("Active loop writes: 0"), panel);
  panelLayout->addWidget(m_statusLabel);

  m_table = new QTableWidget(0, 6, panel);
  m_table->setHorizontalHeaderLabels(
      {("ID"), ("Address"), ("Type"), ("Value"), ("Interval (ms)"), ("Source")});
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->verticalHeader()->setVisible(false);
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
  panelLayout->addWidget(m_table, 1);

  auto* controls = new QHBoxLayout();
  controls->addStretch(1);
  m_stopSelectedButton = new QPushButton(("Stop Selected"), panel);
  controls->addWidget(m_stopSelectedButton);
  panelLayout->addLayout(controls);

  connect(m_stopSelectedButton,
          &QPushButton::clicked,
          this,
          &LoopWriteManagerWindow::onStopSelectedClicked);

  rootLayout->addWidget(panel, 1);
  return root;
}

void LoopWriteManagerWindow::onStopSelectedClicked() {
  if (m_table == nullptr) {
    return;
  }

  const auto selected = m_table->selectionModel()->selectedRows();
  if (selected.empty()) {
    return;
  }

  std::vector<std::uint64_t> ids;
  ids.reserve(static_cast<std::size_t>(selected.size()));

  for (const auto& index : selected) {
    if (!index.isValid()) {
      continue;
    }
    auto* idItem = m_table->item(index.row(), 0);
    if (idItem == nullptr) {
      continue;
    }
    const auto idValue = idItem->data(Qt::UserRole).toULongLong();
    if (idValue != 0) {
      ids.push_back(static_cast<std::uint64_t>(idValue));
    }
  }

  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  if (ids.empty()) {
    return;
  }

  emit stopSelectedRequested(ids);
}

}  // namespace farcal::ui
