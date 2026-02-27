#include "farcal/ui/MainWindow.hpp"
#include "q_lit.hpp"

#include "farcal/luavm/AttachedProcessContext.hpp"
#include "farcal/memory/MemoryReader.hpp"
#include "farcal/ui/AttachProcessDialog.hpp"
#include "farcal/ui/InfoWindow.hpp"
#include "farcal/ui/LogWindow.hpp"
#include "farcal/ui/Logger.hpp"
#include "farcal/ui/LoopWriteManagerWindow.hpp"
#include "farcal/ui/LuaVmWindow.hpp"
#include "farcal/ui/MemoryViewerWindow.hpp"
#include "farcal/ui/RttiWindow.hpp"
#include "farcal/ui/SettingsWindow.hpp"
#include "farcal/ui/StringsWindow.hpp"
#include "farcal/ui/StructureDissectorWindow.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTableWidgetSelectionRange>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <tlhelp32.h>
#  include <windows.h>
#endif
namespace farcal::ui {

MainWindow::MainWindow(QWidget* parent)
  : QMainWindow(parent),
    m_memoryReader(std::make_unique<memory::MemoryReader>()),
    m_homeScanner(std::make_unique<memory::ProcessMemoryScanner>(m_memoryReader.get())) {
  m_logWindow = std::make_unique<LogWindow>(this);
  Logger::instance().setLogWindow(m_logWindow.get());

  loadKeybindSettings();
  applyTheme();
  configureWindow();

  m_liveUpdateTimer = new QTimer(this);
  m_liveUpdateTimer->setInterval(250);
  connect(m_liveUpdateTimer, &QTimer::timeout, this, [this]() {
    if (m_scanBusy || m_memoryReader == nullptr || !m_memoryReader->attached()) {
      return;
    }
    refreshScanResultsLiveValues();
    refreshAddressListLiveValues();
  });
  m_liveUpdateTimer->start();

  m_loopWriteTimer = new QTimer(this);
  m_loopWriteTimer->setInterval(25);
  connect(m_loopWriteTimer, &QTimer::timeout, this, &MainWindow::processLoopWriteEntries);
}

MainWindow::~MainWindow() {
  if (m_scanThread != nullptr) {
    m_scanThread->quit();
    m_scanThread->wait();
    m_scanThread = nullptr;
  }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (event == nullptr) {
    return QMainWindow::eventFilter(watched, event);
  }

  if (m_scanResultsTable != nullptr && watched == m_scanResultsTable->viewport()) {
    if (event->type() == QEvent::MouseButtonPress) {
      const auto* mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseEvent->button() == Qt::LeftButton
          && mouseEvent->modifiers().testFlag(Qt::ControlModifier)) {
        if (auto* item = m_scanResultsTable->itemAt(mouseEvent->pos()); item != nullptr) {
          const int row = item->row();
          if (row >= 0 && row < m_scanResultsTable->rowCount()) {
            auto* addressItem = m_scanResultsTable->item(row, 0);
            auto* valueItem   = m_scanResultsTable->item(row, 1);
            if (addressItem != nullptr) {
              QString addressText = addressItem->text().trimmed();
              if (addressText.startsWith(("0x"), Qt::CaseInsensitive)) {
                addressText = addressText.mid(2);
              }
              bool             ok            = false;
              const qulonglong parsedAddress = addressText.toULongLong(&ok, 16);
              if (ok && parsedAddress != 0) {
                const QString type =
                    (m_valueTypeCombo == nullptr) ? ("-") : m_valueTypeCombo->currentText();
                const QString currentValue = (valueItem == nullptr) ? QString() : valueItem->text();
                promptSetValueForAddress(
                    static_cast<std::uintptr_t>(parsedAddress), type, currentValue);
                return true;
              }
            }
          }
        }
      }
      if (mouseEvent->button() == Qt::RightButton) {
        return true;
      }
    }
    if (event->type() == QEvent::ContextMenu) {
      const auto* contextEvent = static_cast<QContextMenuEvent*>(event);
      onScanResultsContextMenu(contextEvent->pos());
      return true;
    }
  }

  if (m_addressListTable != nullptr && watched == m_addressListTable->viewport()) {
    if (event->type() == QEvent::MouseButtonPress) {
      const auto* mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseEvent->button() == Qt::LeftButton
          && mouseEvent->modifiers().testFlag(Qt::ControlModifier)) {
        if (auto* item = m_addressListTable->itemAt(mouseEvent->pos()); item != nullptr) {
          const int row = item->row();
          if (row >= 0 && row < m_addressListTable->rowCount()) {
            auto* addressItem = m_addressListTable->item(row, 2);
            auto* typeItem    = m_addressListTable->item(row, 3);
            auto* valueItem   = m_addressListTable->item(row, 4);
            if (addressItem != nullptr && typeItem != nullptr) {
              std::uintptr_t   address       = 0;
              const qulonglong storedAddress = addressItem->data(Qt::UserRole).toULongLong();
              if (storedAddress != 0) {
                address = static_cast<std::uintptr_t>(storedAddress);
              } else {
                QString text = addressItem->text().trimmed();
                if (text.startsWith(("0x"), Qt::CaseInsensitive)) {
                  text = text.mid(2);
                }
                bool             ok     = false;
                const qulonglong parsed = text.toULongLong(&ok, 16);
                if (ok && parsed != 0) {
                  address = static_cast<std::uintptr_t>(parsed);
                }
              }

              if (address != 0) {
                const QString currentValue = (valueItem == nullptr) ? QString() : valueItem->text();
                promptSetValueForAddress(address, typeItem->text(), currentValue);
                return true;
              }
            }
          }
        }
      }
      if (mouseEvent->button() == Qt::RightButton) {
        return true;
      }
    }
    if (event->type() == QEvent::ContextMenu) {
      const auto* contextEvent = static_cast<QContextMenuEvent*>(event);
      onAddressListContextMenu(contextEvent->pos());
      return true;
    }
    if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::Leave) {
      m_addressListDragAnchorRow = -1;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::applyTheme() {
  setStyleSheet((R"(QMainWindow {
  background-color: #22242a;
  color: #e8eaed;
}
QMenuBar {
  background-color: #23252d;
  color: #e8eaed;
  border-bottom: 1px solid #42454e;
}
QMenuBar::item {
  spacing: 8px;
  padding: 5px 10px;
  background: transparent;
}
QMenuBar::item:selected {
  background: #353841;
}
QMenu {
  background-color: #2a2c34;
  border: 1px solid #484b55;
}
QMenu::item {
  color: #c7ccd6;
}
QMenu::item:selected {
  background-color: #3c404b;
  color: #ffffff;
}
QLabel {
  color: #e8eaed;
}
QPushButton {
  background-color: #444851;
  border: 1px solid #656a76;
  border-radius: 4px;
  color: #f2f4f7;
  padding: 4px 8px;
}
QPushButton:hover {
  background-color: #525762;
}
QPushButton:pressed {
  background-color: #3a3e47;
}
QLineEdit, QComboBox, QSpinBox {
  background-color: #1b1d22;
  border: 1px solid #4a4e58;
  border-radius: 3px;
  color: #e9ecf1;
  padding: 4px;
  selection-background-color: #4e5f82;
}
QComboBox::drop-down {
  width: 22px;
  border-left: 1px solid #4a4e58;
}
QCheckBox {
  spacing: 8px;
  padding: 1px 0px;
}
QCheckBox::indicator {
  width: 17px;
  height: 17px;
  border: 1px solid #626876;
  border-radius: 2px;
  background: #23252b;
}
QCheckBox::indicator:hover {
  border-color: #7a8396;
}
QCheckBox::indicator:checked {
  background: #5b86c5;
  border-color: #7ea4db;
}
QCheckBox::indicator:disabled {
  background: #1b1d22;
  border-color: #3d4149;
}
QProgressBar {
  border: 1px solid #4f5560;
  background-color: #191b20;
  text-align: center;
  color: #e8eaed;
}
QProgressBar::chunk {
  background-color: #4f89cc;
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
}
QSplitter::handle {
  background-color: #53565f;
})"));
}

void MainWindow::configureWindow() {
  setWindowTitle(("Farcal Engine"));
  resize(920, 780);
  configureMenuBar();
  setCentralWidget(buildCentralArea());
}

void MainWindow::configureMenuBar() {
  auto* topMenu             = menuBar();
  auto* fileMenu            = topMenu->addMenu(("File"));
  m_attachToProcessAction   = fileMenu->addAction(("Attach To Process"));
  m_attachLastProcessAction = fileMenu->addAction(("Attach Last Process"));
  fileMenu->addSeparator();
  auto* settingsAction = fileMenu->addAction(("Settings"));

  connect(
      m_attachToProcessAction, &QAction::triggered, this, &MainWindow::showAttachToProcessDialog);
  connect(m_attachLastProcessAction, &QAction::triggered, this, &MainWindow::showAttachLastProcess);
  connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettingsWindow);

  auto* memoryViewMenu     = topMenu->addMenu(("Memory View"));
  auto* memoryViewerAction = memoryViewMenu->addAction(("Memory Viewer"));
  connect(memoryViewerAction, &QAction::triggered, this, &MainWindow::showMemoryViewerWindow);

  auto* toolsMenu     = topMenu->addMenu(("Tools"));
  m_rttiScannerAction = toolsMenu->addAction(("RTTI Scanner"));
  connect(m_rttiScannerAction, &QAction::triggered, this, &MainWindow::showRttiWindow);
  m_stringScannerAction = toolsMenu->addAction(("String Scanner"));
  connect(m_stringScannerAction, &QAction::triggered, this, &MainWindow::showStringsWindow);
  m_structureDissectorAction = toolsMenu->addAction(("Structure Dissector"));
  connect(m_structureDissectorAction,
          &QAction::triggered,
          this,
          &MainWindow::showStructureDissectorWindow);
  toolsMenu->addSeparator();
  auto* loopValueManagerAction = toolsMenu->addAction(("Loop Value Manager"));
  connect(
      loopValueManagerAction, &QAction::triggered, this, &MainWindow::showLoopWriteManagerWindow);

  auto* luaMenu  = topMenu->addMenu(("Lua"));
  m_luaIdeAction = luaMenu->addAction(("IDE"));
  connect(m_luaIdeAction, &QAction::triggered, this, &MainWindow::showLuaVmWindow);

  auto* helpMenu   = topMenu->addMenu(("Help"));
  auto* infoAction = helpMenu->addAction(("Info"));
  connect(infoAction, &QAction::triggered, this, &MainWindow::showInfoWindow);

  auto* debugMenu = topMenu->addMenu(("Debug"));
  auto* logAction = debugMenu->addAction(("Show Log Window"));
  connect(logAction, &QAction::triggered, this, &MainWindow::showLogWindow);

  applyKeybindSettings();
}

void MainWindow::showMemoryViewerWindow() {
  if (m_memoryViewerWindow == nullptr) {
    m_memoryViewerWindow = std::make_unique<MemoryViewerWindow>(this);
  }

  if (m_attachedProcessId != 0 && !m_attachedProcessName.isEmpty()) {
    m_memoryViewerWindow->setAttachedProcess(m_attachedProcessId, m_attachedProcessName);
  }

  m_memoryViewerWindow->show();
  m_memoryViewerWindow->raise();
  m_memoryViewerWindow->activateWindow();
}

void MainWindow::showRttiWindow() {
  if (m_rttiWindow == nullptr) {
    m_rttiWindow = std::make_unique<RttiWindow>(this);
  }

  if (m_attachedProcessId != 0 && !m_attachedProcessName.isEmpty()) {
    m_rttiWindow->setAttachedProcess(m_attachedProcessId, m_attachedProcessName);
  }

  m_rttiWindow->show();
  m_rttiWindow->raise();
  m_rttiWindow->activateWindow();
}

void MainWindow::showInfoWindow() {
  if (m_infoWindow == nullptr) {
    m_infoWindow = std::make_unique<InfoWindow>(this);
  }

  m_infoWindow->show();
  m_infoWindow->raise();
  m_infoWindow->activateWindow();
}

void MainWindow::showLogWindow() {
  if (m_logWindow != nullptr) {
    m_logWindow->show();
    m_logWindow->raise();
    m_logWindow->activateWindow();
  }
}

void MainWindow::showSettingsWindow() {
  if (m_settingsWindow == nullptr) {
    m_settingsWindow = std::make_unique<SettingsWindow>(this);
    connect(m_settingsWindow.get(),
            &SettingsWindow::keybindsSaved,
            this,
            [this](const KeybindSettings& settings) {
              m_keybindSettings = settings;
              applyKeybindSettings();
              saveKeybindSettings();
            });
  }

  m_settingsWindow->setKeybindSettings(m_keybindSettings);
  m_settingsWindow->show();
  m_settingsWindow->raise();
  m_settingsWindow->activateWindow();
}

void MainWindow::showLoopWriteManagerWindow() {
  if (m_loopWriteManagerWindow == nullptr) {
    m_loopWriteManagerWindow = std::make_unique<LoopWriteManagerWindow>(this);
    connect(m_loopWriteManagerWindow.get(),
            &LoopWriteManagerWindow::stopSelectedRequested,
            this,
            &MainWindow::stopLoopWriteEntriesByIds);
  }

  refreshLoopWriteManagerWindow();
  m_loopWriteManagerWindow->show();
  m_loopWriteManagerWindow->raise();
  m_loopWriteManagerWindow->activateWindow();
}

void MainWindow::showStringsWindow() {
  if (m_stringsWindow == nullptr) {
    m_stringsWindow = std::make_unique<StringsWindow>(this);
  }

  if (m_attachedProcessId != 0 && !m_attachedProcessName.isEmpty()) {
    m_stringsWindow->setAttachedProcess(m_attachedProcessId, m_attachedProcessName);
  }

  m_stringsWindow->show();
  m_stringsWindow->raise();
  m_stringsWindow->activateWindow();
}

void MainWindow::showStructureDissectorWindow() {
  if (m_structureDissectorWindow == nullptr) {
    m_structureDissectorWindow = std::make_unique<StructureDissectorWindow>(this);
  }

  if (m_attachedProcessId != 0 && !m_attachedProcessName.isEmpty()) {
    m_structureDissectorWindow->setAttachedProcess(m_attachedProcessId, m_attachedProcessName);
  }

  m_structureDissectorWindow->show();
  m_structureDissectorWindow->raise();
  m_structureDissectorWindow->activateWindow();
}

void MainWindow::showLuaVmWindow() {
  if (m_luaVmWindow == nullptr) {
    m_luaVmWindow = std::make_unique<LuaVmWindow>(this);
  }

  m_luaVmWindow->show();
  m_luaVmWindow->raise();
  m_luaVmWindow->activateWindow();
}

QWidget* MainWindow::buildCentralArea() {
  auto* root       = new QWidget(this);
  auto* rootLayout = new QVBoxLayout(root);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  auto* verticalSplitter = new QSplitter(Qt::Vertical, root);
  verticalSplitter->setChildrenCollapsible(false);
  verticalSplitter->setHandleWidth(2);

  auto* topPane       = new QWidget(verticalSplitter);
  auto* topPaneLayout = new QHBoxLayout(topPane);
  topPaneLayout->setContentsMargins(8, 8, 8, 8);
  topPaneLayout->setSpacing(8);

  auto* scanPanel = buildScanPanel();
  scanPanel->setFixedWidth(300);
  topPaneLayout->addWidget(scanPanel);
  topPaneLayout->addWidget(buildScanResultsPanel(), 1);

  verticalSplitter->addWidget(topPane);
  verticalSplitter->addWidget(buildAddressListPanel());
  verticalSplitter->setStretchFactor(0, 4);
  verticalSplitter->setStretchFactor(1, 2);

  rootLayout->addWidget(verticalSplitter, 1);
  return root;
}

QWidget* MainWindow::buildScanPanel() {
  auto* panel = new QFrame(this);
  panel->setFrameShape(QFrame::NoFrame);
  panel->setStyleSheet(("QFrame { background-color: #292c34; border-right: 1px solid #4d515c; }"));

  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(8);

  auto* checkRow = new QHBoxLayout();
  checkRow->setSpacing(14);
  m_hexCheckBox          = new QCheckBox(("Hex"), panel);
  m_scanReadOnlyCheckBox = new QCheckBox(("Also scan read-only memory"), panel);
  checkRow->addWidget(m_hexCheckBox);
  checkRow->addWidget(m_scanReadOnlyCheckBox);
  checkRow->addStretch();
  layout->addLayout(checkRow);

  layout->addWidget(new QLabel(("Scan Type:"), panel));
  m_scanTypeCombo = new QComboBox(panel);
  m_scanTypeCombo->addItems({("Exact Value"),
                             ("Increased Value"),
                             ("Decreased Value"),
                             ("Changed Value"),
                             ("Unchanged Value")});
  layout->addWidget(m_scanTypeCombo);

  layout->addWidget(new QLabel(("Value Type:"), panel));
  m_valueTypeCombo = new QComboBox(panel);
  m_valueTypeCombo->addItems(
      {("1 Byte"), ("2 Bytes"), ("4 Bytes"), ("8 Bytes"), ("Float"), ("Double"), ("String")});
  m_valueTypeCombo->setCurrentIndex(2);
  layout->addWidget(m_valueTypeCombo);

  layout->addWidget(new QLabel(("Value:"), panel));
  m_valueInput = new QLineEdit(panel);
  m_valueInput->setPlaceholderText(("Enter value..."));
  layout->addWidget(m_valueInput);

  auto* optionRow = new QHBoxLayout();
  optionRow->setSpacing(12);
  m_caseSensitiveCheckBox = new QCheckBox(("Case sensitive"), panel);
  m_unicodeCheckBox       = new QCheckBox(("Unicode"), panel);
  optionRow->addWidget(m_caseSensitiveCheckBox);
  optionRow->addWidget(m_unicodeCheckBox);
  optionRow->addStretch();
  layout->addLayout(optionRow);

  auto* alignRow = new QHBoxLayout();
  alignRow->addWidget(new QLabel(("Alignment:"), panel));
  m_alignmentSpinBox = new QSpinBox(panel);
  m_alignmentSpinBox->setRange(1, 16);
  m_alignmentSpinBox->setValue(4);
  alignRow->addWidget(m_alignmentSpinBox);
  alignRow->addStretch();
  layout->addLayout(alignRow);

  auto* buttons = new QGridLayout();
  buttons->setHorizontalSpacing(10);
  buttons->setVerticalSpacing(8);
  m_firstScanButton = new QPushButton(("First Scan"), panel);
  m_nextScanButton  = new QPushButton(("Next Scan"), panel);
  m_undoScanButton  = new QPushButton(("Undo Scan"), panel);
  m_newScanButton   = new QPushButton(("New Scan"), panel);
  buttons->addWidget(m_firstScanButton, 0, 0);
  buttons->addWidget(m_nextScanButton, 0, 1);
  buttons->addWidget(m_undoScanButton, 1, 0);
  buttons->addWidget(m_newScanButton, 1, 1);
  layout->addLayout(buttons);

  m_scanProgressBar = new QProgressBar(panel);
  m_scanProgressBar->setRange(0, 100);
  m_scanProgressBar->setValue(0);
  layout->addWidget(m_scanProgressBar);

  m_foundLabel = new QLabel(("Found: 0"), panel);
  layout->addWidget(m_foundLabel);
  layout->addStretch(1);

  connect(m_firstScanButton, &QPushButton::clicked, this, &MainWindow::onFirstScanClicked);
  connect(m_nextScanButton, &QPushButton::clicked, this, &MainWindow::onNextScanClicked);
  connect(m_undoScanButton, &QPushButton::clicked, this, &MainWindow::onUndoScanClicked);
  connect(m_newScanButton, &QPushButton::clicked, this, &MainWindow::onNewScanClicked);
  connect(m_valueTypeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
    updateScanToggleState();
  });
  connect(m_scanTypeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
    updateScanToggleState();
  });
  connect(m_hexCheckBox, &QCheckBox::toggled, this, [this](bool) { updateScanToggleState(); });
  connect(m_caseSensitiveCheckBox, &QCheckBox::toggled, this, [this](bool) {
    updateScanToggleState();
  });
  connect(m_unicodeCheckBox, &QCheckBox::toggled, this, [this](bool) { updateScanToggleState(); });
  updateScanToggleState();

  return panel;
}

void MainWindow::updateScanToggleState() {
  if (m_valueTypeCombo == nullptr || m_scanTypeCombo == nullptr || m_valueInput == nullptr
      || m_hexCheckBox == nullptr || m_caseSensitiveCheckBox == nullptr
      || m_unicodeCheckBox == nullptr) {
    return;
  }

  const QString valueType    = m_valueTypeCombo->currentText().trimmed().toLower();
  const QString scanType     = m_scanTypeCombo->currentText().trimmed().toLower();
  const bool    isStringType = valueType.contains(("string"));

  m_caseSensitiveCheckBox->setEnabled(isStringType);
  m_unicodeCheckBox->setEnabled(isStringType);
  m_hexCheckBox->setEnabled(!isStringType);

  if (!isStringType) {
    const QSignalBlocker blockCase(m_caseSensitiveCheckBox);
    const QSignalBlocker blockUnicode(m_unicodeCheckBox);
    m_caseSensitiveCheckBox->setChecked(false);
    m_unicodeCheckBox->setChecked(false);
  } else if (m_hexCheckBox->isChecked()) {
    const QSignalBlocker blockHex(m_hexCheckBox);
    m_hexCheckBox->setChecked(false);
  }

  const bool needsInput = !scanType.contains(("changed")) && !scanType.contains(("unchanged"));
  m_valueInput->setEnabled(needsInput);
  if (!needsInput) {
    m_valueInput->setPlaceholderText(("No input needed for this scan type"));
  } else {
    m_valueInput->setPlaceholderText(("Enter value..."));
  }
}

QWidget* MainWindow::buildScanResultsPanel() {
  auto* panel = new QFrame(this);
  panel->setFrameShape(QFrame::NoFrame);
  panel->setStyleSheet(("QFrame { background-color: #1b1d22; }"));

  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  m_scanResultsTable = new QTableWidget(0, 3, panel);
  m_scanResultsTable->setHorizontalHeaderLabels({("Address"), ("Value"), ("Previous Value")});
  m_scanResultsTable->verticalHeader()->setVisible(false);
  m_scanResultsTable->setAlternatingRowColors(false);
  m_scanResultsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_scanResultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_scanResultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_scanResultsTable->setContextMenuPolicy(Qt::CustomContextMenu);
  m_scanResultsTable->viewport()->installEventFilter(this);
  m_scanResultsTable->horizontalHeader()->setStretchLastSection(true);
  m_scanResultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_scanResultsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_scanResultsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  connect(m_scanResultsTable,
          &QWidget::customContextMenuRequested,
          this,
          &MainWindow::onScanResultsContextMenu);
  connect(m_scanResultsTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem*) {
    std::uintptr_t address = 0;
    QString        type;
    QString        value;
    if (selectedAddressFromScanResults(address, type, value)) {
      addAddressListEntry(address, type, value);
    }
  });
  layout->addWidget(m_scanResultsTable, 1);

  return panel;
}

QWidget* MainWindow::buildAddressListPanel() {
  auto* panel = new QFrame(this);
  panel->setFrameShape(QFrame::NoFrame);
  panel->setStyleSheet(("QFrame { background-color: #2b2e36; border-top: 1px solid #5a5d65; }"));

  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  auto* headerRow = new QHBoxLayout();
  auto* title     = new QLabel(("Address List"), panel);
  QFont titleFont = title->font();
  titleFont.setBold(true);
  titleFont.setPointSize(titleFont.pointSize() + 1);
  title->setFont(titleFont);
  headerRow->addWidget(title);
  headerRow->addStretch();
  m_addressListClearButton = new QPushButton(("Clear"), panel);
  m_addressListClearButton->setFixedWidth(100);
  headerRow->addWidget(m_addressListClearButton);
  layout->addLayout(headerRow);

  m_addressListTable = new QTableWidget(0, 5, panel);
  m_addressListTable->setHorizontalHeaderLabels(
      {(""), ("Description"), ("Address"), ("Type"), ("Value")});
  m_addressListTable->verticalHeader()->setVisible(false);
  m_addressListTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_addressListTable->setSelectionMode(QAbstractItemView::MultiSelection);
  m_addressListTable->setEditTriggers(QAbstractItemView::DoubleClicked
                                      | QAbstractItemView::EditKeyPressed
                                      | QAbstractItemView::SelectedClicked);
  m_addressListTable->setContextMenuPolicy(Qt::CustomContextMenu);
  m_addressListTable->setMouseTracking(true);
  m_addressListTable->viewport()->installEventFilter(this);
  m_addressListTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  m_addressListTable->setColumnWidth(0, 40);
  m_addressListTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_addressListTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_addressListTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  m_addressListTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  connect(m_addressListTable,
          &QWidget::customContextMenuRequested,
          this,
          &MainWindow::onAddressListContextMenu);
  connect(m_addressListTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem*) {
    std::uintptr_t address = 0;
    if (selectedAddressFromAddressList(address)) {
      openAddressInMemoryViewer(address);
    }
  });
  connect(m_addressListTable, &QTableWidget::itemPressed, this, [this](QTableWidgetItem* item) {
    if (item == nullptr || m_addressListTable == nullptr) {
      return;
    }
    m_addressListDragAnchorRow = item->row();
  });
  connect(m_addressListTable, &QTableWidget::itemEntered, this, [this](QTableWidgetItem* item) {
    if (item == nullptr || m_addressListTable == nullptr) {
      return;
    }
    if (!(QApplication::mouseButtons() & Qt::LeftButton)) {
      return;
    }
    if (m_addressListDragAnchorRow < 0) {
      m_addressListDragAnchorRow = item->row();
      return;
    }

    const int start = std::min(m_addressListDragAnchorRow, item->row());
    const int end   = std::max(m_addressListDragAnchorRow, item->row());

    m_addressListTable->clearSelection();
    QTableWidgetSelectionRange range(start, 0, end, m_addressListTable->columnCount() - 1);
    m_addressListTable->setRangeSelected(range, true);
  });
  connect(m_addressListClearButton, &QPushButton::clicked, this, [this]() {
    if (m_addressListTable != nullptr) {
      m_addressListTable->setRowCount(0);
      m_addressListDragAnchorRow = -1;
    }
  });
  layout->addWidget(m_addressListTable, 1);

  return panel;
}

void MainWindow::showAttachToProcessDialog() {
  const auto selectedProcess = showAttachProcessDialog(this);
  if (!selectedProcess.has_value()) {
    return;
  }

  attachToProcess(selectedProcess->processId, selectedProcess->processName);
}

void MainWindow::showAttachLastProcess() {
  if (m_scanBusy) {
    QMessageBox::information(
        this, ("Attach Last Process"), ("Wait for the current scan to finish."));
    return;
  }

  const auto saved = loadLastAttachedProcess();
  if (!saved.has_value()) {
    QMessageBox::information(this,
                             ("Attach Last Process"),
                             ("No saved process was found in %LOCALAPPDATA%/farcalenginev2/."));
    return;
  }

  std::uint32_t pid         = saved->first;
  const QString processName = saved->second;

  bool canAttachSavedPid = false;
  {
    memory::MemoryReader probeReader;
    canAttachSavedPid = probeReader.attach(static_cast<memory::Process::Id>(pid));
  }

  if (!canAttachSavedPid) {
    const auto matchedPid = findRunningProcessIdByName(processName);
    if (matchedPid.has_value()) {
      pid = *matchedPid;
    } else {
      QMessageBox::warning(this,
                           ("Attach Last Process"),
                           QString(("Saved process '%1' is not running.")).arg(processName));
      return;
    }
  }

  attachToProcess(pid, processName);
}

void MainWindow::attachToProcess(std::uint32_t processId, const QString& processName) {
  if (m_scanBusy) {
    QMessageBox::information(this, ("Attach To Process"), ("Wait for the current scan to finish."));
    return;
  }

  if (processId == 0 || processName.isEmpty() || m_memoryReader == nullptr) {
    luavm::AttachedProcessContext::clear();
    if (m_homeScanner != nullptr) {
      m_homeScanner->reset();
    }
    refreshScanResults();
    return;
  }

  const bool attached = m_memoryReader->attach(static_cast<memory::Process::Id>(processId));
  if (!attached) {
    luavm::AttachedProcessContext::clear();
    if (m_homeScanner != nullptr) {
      m_homeScanner->reset();
    }
    refreshScanResults();
    QMessageBox::warning(this,
                         ("Attach To Process"),
                         QString(("Failed to attach memory reader to %1 (PID %2)."))
                             .arg(processName)
                             .arg(processId));
    return;
  }

  m_attachedProcessId   = processId;
  m_attachedProcessName = processName;
  luavm::AttachedProcessContext::setAttachedProcessId(processId);
  if (m_homeScanner != nullptr) {
    m_homeScanner->setReader(m_memoryReader.get());
    m_homeScanner->reset();
    refreshScanResults();
  }
  if (m_memoryViewerWindow != nullptr) {
    m_memoryViewerWindow->setAttachedProcess(m_attachedProcessId, m_attachedProcessName);
  }
  if (m_rttiWindow != nullptr) {
    m_rttiWindow->setAttachedProcess(m_attachedProcessId, m_attachedProcessName);
  }
  if (m_stringsWindow != nullptr) {
    m_stringsWindow->setAttachedProcess(m_attachedProcessId, m_attachedProcessName);
  }
  if (m_structureDissectorWindow != nullptr) {
    m_structureDissectorWindow->setAttachedProcess(m_attachedProcessId, m_attachedProcessName);
  }

  persistLastAttachedProcess(processId, processName);
  setAttachedProcessName(processName);
}

void MainWindow::setAttachedProcessName(const QString& processName) {
  if (processName.isEmpty()) {
    return;
  }
  setWindowTitle(QString(("Farcal Engine - %1")).arg(processName));
}

void MainWindow::onFirstScanClicked() {
  runScan(true);
}

void MainWindow::onNextScanClicked() {
  runScan(false);
}

void MainWindow::onUndoScanClicked() {
  if (m_scanBusy || m_homeScanner == nullptr) {
    return;
  }

  if (!m_homeScanner->undo()) {
    QMessageBox::information(this, ("Scan"), QString::fromStdString(m_homeScanner->lastError()));
    return;
  }

  refreshScanResults();
}

void MainWindow::onNewScanClicked() {
  if (m_scanBusy || m_homeScanner == nullptr) {
    return;
  }

  m_homeScanner->reset();
  if (m_scanProgressBar != nullptr) {
    m_scanProgressBar->setValue(0);
  }
  refreshScanResults();
}

void MainWindow::runScan(bool firstScan) {
  if (m_scanBusy || m_homeScanner == nullptr || m_memoryReader == nullptr) {
    return;
  }

  if (m_attachedProcessId == 0 || !m_memoryReader->attached()) {
    QMessageBox::warning(this, ("Scan"), ("Attach to a process first."));
    return;
  }

  const memory::ScanSettings settings = buildScanSettings();
  if (firstScan && settings.scanType != memory::ScanType::ExactValue) {
    QMessageBox::information(this, ("Scan"), ("First Scan currently supports Exact Value only."));
    return;
  }

  const std::string query =
      (m_valueInput == nullptr) ? std::string() : m_valueInput->text().trimmed().toStdString();

  m_scanBusy = true;
  setScanUiBusy(true);
  if (m_scanProgressBar != nullptr) {
    m_scanProgressBar->setValue(0);
  }

  QPointer<MainWindow> self(this);
  m_scanThread = QThread::create([this, self, firstScan, settings, query]() {
    const auto progress = [self](std::size_t completed, std::size_t total) {
      if (self == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          self,
          [self, completed, total]() {
            if (self != nullptr) {
              self->updateScanProgress(completed, total);
            }
          },
          Qt::QueuedConnection);
    };

    bool success = false;
    if (firstScan) {
      success = m_homeScanner->firstScan(settings, query, progress);
    } else {
      success = m_homeScanner->nextScan(settings, query, progress);
    }

    QString errorMessage;
    if (!success) {
      errorMessage = QString::fromStdString(m_homeScanner->lastError());
    }

    if (self != nullptr) {
      QMetaObject::invokeMethod(
          self,
          [self, success, errorMessage]() {
            if (self != nullptr) {
              self->onScanFinished(success, errorMessage);
            }
          },
          Qt::QueuedConnection);
    }
  });

  connect(m_scanThread, &QThread::finished, this, [this]() { m_scanThread = nullptr; });
  connect(m_scanThread, &QThread::finished, m_scanThread, &QObject::deleteLater);
  m_scanThread->start();
}

void MainWindow::onScanFinished(bool success, const QString& errorMessage) {
  m_scanBusy = false;
  setScanUiBusy(false);
  if (m_scanProgressBar != nullptr) {
    m_scanProgressBar->setValue(success ? 100 : 0);
  }

  if (!success) {
    QMessageBox::warning(this, ("Scan"), errorMessage);
  }

  refreshScanResults();
}

void MainWindow::updateScanProgress(std::size_t completed, std::size_t total) {
  if (m_scanProgressBar == nullptr || total == 0) {
    return;
  }

  const std::size_t safeCompleted = std::min(completed, total);
  const int         percent       = static_cast<int>((safeCompleted * 100) / total);
  m_scanProgressBar->setValue(percent);
}

void MainWindow::setScanUiBusy(bool busy) {
  if (m_firstScanButton != nullptr) {
    m_firstScanButton->setEnabled(!busy);
  }
  if (m_nextScanButton != nullptr) {
    m_nextScanButton->setEnabled(!busy);
  }
  if (m_undoScanButton != nullptr) {
    m_undoScanButton->setEnabled(!busy);
  }
  if (m_newScanButton != nullptr) {
    m_newScanButton->setEnabled(!busy);
  }
}

void MainWindow::refreshScanResults() {
  if (m_scanResultsTable == nullptr || m_foundLabel == nullptr) {
    return;
  }

  if (m_homeScanner == nullptr) {
    m_scanResultsTable->clearContents();
    m_scanResultsTable->setRowCount(0);
    m_foundLabel->setText(("Found: 0"));
    return;
  }

  const auto&           entries         = m_homeScanner->results();
  constexpr std::size_t kMaxVisibleRows = 20000;
  const std::size_t     visibleRows     = std::min(entries.size(), kMaxVisibleRows);

  m_scanResultsTable->setRowCount(static_cast<int>(visibleRows));
  for (std::size_t i = 0; i < visibleRows; ++i) {
    const auto& entry       = entries[i];
    auto*       addressItem = new QTableWidgetItem(
        QString(("0x%1")).arg(static_cast<qulonglong>(entry.address), 0, 16).toUpper());
    auto* valueItem    = new QTableWidgetItem(formatScanValue(entry.currentValue));
    auto* previousItem = new QTableWidgetItem(formatScanValue(entry.previousValue));
    m_scanResultsTable->setItem(static_cast<int>(i), 0, addressItem);
    m_scanResultsTable->setItem(static_cast<int>(i), 1, valueItem);
    m_scanResultsTable->setItem(static_cast<int>(i), 2, previousItem);
  }

  if (entries.size() > kMaxVisibleRows) {
    m_foundLabel->setText(
        QString(("Found: %1 (showing first %2)")).arg(entries.size()).arg(kMaxVisibleRows));
  } else {
    m_foundLabel->setText(QString(("Found: %1")).arg(entries.size()));
  }
}

memory::ScanSettings MainWindow::buildScanSettings() const {
  memory::ScanSettings settings{};
  if (m_scanTypeCombo != nullptr) {
    switch (m_scanTypeCombo->currentIndex()) {
      case 1:
        settings.scanType = memory::ScanType::IncreasedValue;
        break;
      case 2:
        settings.scanType = memory::ScanType::DecreasedValue;
        break;
      case 3:
        settings.scanType = memory::ScanType::ChangedValue;
        break;
      case 4:
        settings.scanType = memory::ScanType::UnchangedValue;
        break;
      default:
        settings.scanType = memory::ScanType::ExactValue;
        break;
    }
  }

  if (m_valueTypeCombo != nullptr) {
    switch (m_valueTypeCombo->currentIndex()) {
      case 0:
        settings.valueType = memory::ScanValueType::Int8;
        break;
      case 1:
        settings.valueType = memory::ScanValueType::Int16;
        break;
      case 2:
        settings.valueType = memory::ScanValueType::Int32;
        break;
      case 3:
        settings.valueType = memory::ScanValueType::Int64;
        break;
      case 4:
        settings.valueType = memory::ScanValueType::Float;
        break;
      case 5:
        settings.valueType = memory::ScanValueType::Double;
        break;
      case 6:
        settings.valueType = memory::ScanValueType::String;
        break;
      default:
        settings.valueType = memory::ScanValueType::Int32;
        break;
    }
  }

  settings.hexInput = m_hexCheckBox != nullptr && m_hexCheckBox->isChecked();
  settings.includeReadOnly =
      m_scanReadOnlyCheckBox != nullptr && m_scanReadOnlyCheckBox->isChecked();
  settings.caseSensitive =
      m_caseSensitiveCheckBox != nullptr && m_caseSensitiveCheckBox->isChecked();
  settings.unicode = m_unicodeCheckBox != nullptr && m_unicodeCheckBox->isChecked();
  settings.alignment =
      (m_alignmentSpinBox == nullptr) ? 1U : static_cast<std::size_t>(m_alignmentSpinBox->value());
  return settings;
}

QString MainWindow::formatScanValue(const std::vector<std::uint8_t>& bytes) const {
  if (m_homeScanner == nullptr || bytes.empty()) {
    return ("-");
  }

  const auto settings = m_homeScanner->lastSettings();

  switch (settings.valueType) {
    case memory::ScanValueType::Int8: {
      std::int8_t value = 0;
      std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
      return QString::number(static_cast<int>(value));
    }
    case memory::ScanValueType::Int16: {
      std::int16_t value = 0;
      std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
      return QString::number(value);
    }
    case memory::ScanValueType::Int32: {
      std::int32_t value = 0;
      std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
      return QString::number(value);
    }
    case memory::ScanValueType::Int64: {
      std::int64_t value = 0;
      std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
      return QString::number(value);
    }
    case memory::ScanValueType::Float: {
      float value = 0.0F;
      std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
      return QString::number(value, 'g', 8);
    }
    case memory::ScanValueType::Double: {
      double value = 0.0;
      std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
      return QString::number(value, 'g', 14);
    }
    case memory::ScanValueType::String: {
      if (settings.unicode && bytes.size() >= 2) {
        const char16_t* chars  = reinterpret_cast<const char16_t*>(bytes.data());
        const int       length = static_cast<int>(bytes.size() / sizeof(char16_t));
        return QString::fromUtf16(chars, length);
      }
      return QString::fromLatin1(reinterpret_cast<const char*>(bytes.data()),
                                 static_cast<int>(bytes.size()));
    }
  }

  return ("-");
}

void MainWindow::onScanResultsContextMenu(const QPoint& pos) {
  if (m_scanResultsTable == nullptr) {
    return;
  }

  if (auto* item = m_scanResultsTable->itemAt(pos); item != nullptr) {
    if (!item->isSelected()) {
      auto* model = m_scanResultsTable->selectionModel();
      if (model != nullptr) {
        const QModelIndex left = m_scanResultsTable->model()->index(item->row(), 0);
        const QModelIndex right =
            m_scanResultsTable->model()->index(item->row(), m_scanResultsTable->columnCount() - 1);
        model->select(QItemSelection(left, right),
                      QItemSelectionModel::Select | QItemSelectionModel::Rows);
      }
    }
    m_scanResultsTable->setCurrentCell(item->row(), item->column(), QItemSelectionModel::NoUpdate);
  }

  std::vector<int> selectedRows;
  if (m_scanResultsTable->selectionModel() != nullptr) {
    const auto rows = m_scanResultsTable->selectionModel()->selectedRows();
    selectedRows.reserve(static_cast<std::size_t>(rows.size()));
    for (const auto& index : rows) {
      if (index.isValid()) {
        selectedRows.push_back(index.row());
      }
    }
  }
  if (selectedRows.empty()) {
    const int currentRow = m_scanResultsTable->currentRow();
    if (currentRow >= 0) {
      selectedRows.push_back(currentRow);
    }
  }
  std::sort(selectedRows.begin(), selectedRows.end());
  selectedRows.erase(std::unique(selectedRows.begin(), selectedRows.end()), selectedRows.end());
  if (selectedRows.empty()) {
    return;
  }

  const auto parseScanRow = [this](int row, std::uintptr_t& outAddress, QString& outValue) -> bool {
    outAddress = 0;
    outValue.clear();

    if (m_scanResultsTable == nullptr || row < 0 || row >= m_scanResultsTable->rowCount()) {
      return false;
    }

    auto* addressItem = m_scanResultsTable->item(row, 0);
    auto* valueItem   = m_scanResultsTable->item(row, 1);
    if (addressItem == nullptr) {
      return false;
    }

    QString text = addressItem->text().trimmed();
    if (text.startsWith(("0x"), Qt::CaseInsensitive)) {
      text = text.mid(2);
    }
    bool             ok            = false;
    const qulonglong parsedAddress = text.toULongLong(&ok, 16);
    if (!ok || parsedAddress == 0) {
      return false;
    }

    outAddress = static_cast<std::uintptr_t>(parsedAddress);
    outValue   = (valueItem == nullptr) ? ("-") : valueItem->text();
    return true;
  };

  const QString type = (m_valueTypeCombo == nullptr) ? ("-") : m_valueTypeCombo->currentText();
  QMenu         menu(this);
  auto* addAction = menu.addAction(selectedRows.size() > 1 ? ("Add Selected To Address List")
                                                           : ("Add To Address List"));
  auto* setValueAction =
      menu.addAction(selectedRows.size() > 1 ? ("Set Value (Selected)") : ("Set Value"));
  auto* loopSetValueAction =
      menu.addAction(selectedRows.size() > 1 ? ("Loop Set Value (Selected)") : ("Loop Set Value"));
  QAction* chosen = menu.exec(m_scanResultsTable->viewport()->mapToGlobal(pos));
  if (chosen == addAction) {
    for (const int row : selectedRows) {
      std::uintptr_t address = 0;
      QString        value;
      if (parseScanRow(row, address, value)) {
        addAddressListEntry(address, type, value);
      }
    }
  } else if (chosen == setValueAction) {
    if (selectedRows.size() > 1) {
      QString initialValue;
      if (auto* valueItem = m_scanResultsTable->item(selectedRows.front(), 1);
          valueItem != nullptr) {
        initialValue = valueItem->text();
      }

      bool          accepted = false;
      const QString input    = QInputDialog::getText(
          this,
          ("Set Value"),
          QString(("Set value for %1 selected addresses:")).arg(selectedRows.size()),
          QLineEdit::Normal,
          initialValue,
          &accepted);
      if (!accepted) {
        return;
      }

      int successCount = 0;
      int failCount    = 0;
      for (const int row : selectedRows) {
        std::uintptr_t address = 0;
        QString        currentValue;
        if (!parseScanRow(row, address, currentValue)) {
          ++failCount;
          continue;
        }
        if (writeAddressValue(address, type, input)) {
          ++successCount;
        } else {
          ++failCount;
        }
      }

      refreshScanResultsLiveValues();
      refreshAddressListLiveValues();

      if (failCount > 0) {
        QMessageBox::warning(
            this,
            ("Set Value"),
            QString(("Wrote %1 address(es), failed %2.")).arg(successCount).arg(failCount));
      }
      return;
    }

    std::uintptr_t address = 0;
    QString        value;
    if (!parseScanRow(selectedRows.front(), address, value)) {
      return;
    }
    promptSetValueForAddress(address, type, value);
  } else if (chosen == loopSetValueAction) {
    promptLoopSetValueForScanSelection(selectedRows);
  }
}

void MainWindow::onAddressListContextMenu(const QPoint& pos) {
  if (m_addressListTable == nullptr) {
    return;
  }

  if (auto* item = m_addressListTable->itemAt(pos); item != nullptr) {
    if (!item->isSelected()) {
      auto* model = m_addressListTable->selectionModel();
      if (model != nullptr) {
        const QModelIndex left = m_addressListTable->model()->index(item->row(), 0);
        const QModelIndex right =
            m_addressListTable->model()->index(item->row(), m_addressListTable->columnCount() - 1);
        model->select(QItemSelection(left, right),
                      QItemSelectionModel::Select | QItemSelectionModel::Rows);
      }
    }
    m_addressListTable->setCurrentCell(item->row(), item->column(), QItemSelectionModel::NoUpdate);
  }

  const std::vector<int> selectedRows = selectedAddressListRows();
  if (selectedRows.empty()) {
    return;
  }

  std::uintptr_t address = 0;
  selectedAddressFromAddressList(address);

  QMenu menu(this);
  auto* editDescriptionAction = menu.addAction(
      selectedRows.size() > 1 ? ("Edit Description (Selected)") : ("Edit Description"));
  auto* setValueAction =
      menu.addAction(selectedRows.size() > 1 ? ("Set Value (Selected)") : ("Set Value"));
  auto* loopSetValueAction =
      menu.addAction(selectedRows.size() > 1 ? ("Loop Set Value (Selected)") : ("Loop Set Value"));
  menu.addSeparator();
  auto* openMemoryViewerAction = menu.addAction(("Open In Memory Viewer"));
  auto* openStructureAction    = menu.addAction(("Open In Structure Dissector"));
  menu.addSeparator();
  auto* removeAction = menu.addAction(("Remove"));
  if (selectedRows.size() != 1) {
    openMemoryViewerAction->setEnabled(false);
    openStructureAction->setEnabled(false);
  }

  QAction* chosen = menu.exec(m_addressListTable->viewport()->mapToGlobal(pos));
  if (chosen == editDescriptionAction) {
    const int referenceRow = selectedRows.front();
    QString   currentDescription;
    if (referenceRow >= 0 && referenceRow < m_addressListTable->rowCount()) {
      if (auto* descItem = m_addressListTable->item(referenceRow, 1); descItem != nullptr) {
        currentDescription = descItem->text();
      }
    }

    bool          accepted       = false;
    const QString newDescription = QInputDialog::getText(
        this,
        ("Edit Description"),
        selectedRows.size() > 1
            ? QString(("Set description for %1 selected addresses:")).arg(selectedRows.size())
            : ("Set description:"),
        QLineEdit::Normal,
        currentDescription,
        &accepted);
    if (!accepted) {
      return;
    }

    for (const int row : selectedRows) {
      if (row < 0 || row >= m_addressListTable->rowCount()) {
        continue;
      }
      auto* descItem = m_addressListTable->item(row, 1);
      if (descItem == nullptr) {
        descItem = new QTableWidgetItem();
        m_addressListTable->setItem(row, 1, descItem);
      }
      descItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
      descItem->setText(newDescription);
    }
    return;
  }
  if (chosen == setValueAction) {
    if (selectedRows.size() > 1) {
      promptSetValueForAddressSelection(selectedRows);
    } else {
      const int row  = m_addressListTable->currentRow();
      QString   type = ("4 Bytes");
      QString   currentValue;
      if (row >= 0) {
        if (auto* typeItem = m_addressListTable->item(row, 3); typeItem != nullptr) {
          type = typeItem->text();
        }
        if (auto* valueItem = m_addressListTable->item(row, 4); valueItem != nullptr) {
          currentValue = valueItem->text();
        }
      }
      promptSetValueForAddress(address, type, currentValue);
    }
    return;
  }
  if (chosen == loopSetValueAction) {
    promptLoopSetValueForAddressListSelection(selectedRows);
    return;
  }
  if (chosen == openMemoryViewerAction) {
    openAddressInMemoryViewer(address);
    return;
  }
  if (chosen == openStructureAction) {
    openAddressInStructureDissector(address);
    return;
  }
  if (chosen == removeAction) {
    std::vector<int> rowsToRemove = selectedRows;
    std::sort(rowsToRemove.begin(), rowsToRemove.end(), std::greater<int>());
    for (const int row : rowsToRemove) {
      if (row >= 0 && row < m_addressListTable->rowCount()) {
        m_addressListTable->removeRow(row);
      }
    }
  }
}

void MainWindow::addAddressListEntry(std::uintptr_t address,
                                     const QString& type,
                                     const QString& value) {
  if (m_addressListTable == nullptr || address == 0) {
    return;
  }

  for (int row = 0; row < m_addressListTable->rowCount(); ++row) {
    auto* existingAddressItem = m_addressListTable->item(row, 2);
    if (existingAddressItem == nullptr) {
      continue;
    }

    const qulonglong existingAddress = existingAddressItem->data(Qt::UserRole).toULongLong();
    if (existingAddress != static_cast<qulonglong>(address)) {
      continue;
    }

    if (auto* typeItem = m_addressListTable->item(row, 3); typeItem != nullptr) {
      typeItem->setText(type);
    }
    if (auto* valueItem = m_addressListTable->item(row, 4); valueItem != nullptr) {
      valueItem->setText(value);
    }
    if (auto* descriptionItem = m_addressListTable->item(row, 1); descriptionItem != nullptr) {
      descriptionItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
    }
    m_addressListTable->setCurrentCell(row, 0);
    return;
  }

  const int row = m_addressListTable->rowCount();
  m_addressListTable->insertRow(row);

  auto* enabledItem = new QTableWidgetItem(("*"));
  auto* descriptionItem =
      new QTableWidgetItem(QString(("Address %1")).arg(m_addressListNameSeed++));
  auto* addressItem = new QTableWidgetItem(
      QString(("0x%1")).arg(static_cast<qulonglong>(address), 0, 16).toUpper());
  auto* typeItem  = new QTableWidgetItem(type);
  auto* valueItem = new QTableWidgetItem(value);

  addressItem->setData(Qt::UserRole, static_cast<qulonglong>(address));

  const auto nonEditableFlags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
  enabledItem->setFlags(nonEditableFlags);
  addressItem->setFlags(nonEditableFlags);
  typeItem->setFlags(nonEditableFlags);
  valueItem->setFlags(nonEditableFlags);
  descriptionItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);

  m_addressListTable->setItem(row, 0, enabledItem);
  m_addressListTable->setItem(row, 1, descriptionItem);
  m_addressListTable->setItem(row, 2, addressItem);
  m_addressListTable->setItem(row, 3, typeItem);
  m_addressListTable->setItem(row, 4, valueItem);
  m_addressListTable->setCurrentCell(row, 0);
}

bool MainWindow::selectedAddressFromScanResults(std::uintptr_t& outAddress,
                                                QString&        outType,
                                                QString&        outValue) const {
  outAddress = 0;
  outType.clear();
  outValue.clear();

  if (m_scanResultsTable == nullptr) {
    return false;
  }

  const int row = m_scanResultsTable->currentRow();
  if (row < 0) {
    return false;
  }

  auto* addressItem = m_scanResultsTable->item(row, 0);
  auto* valueItem   = m_scanResultsTable->item(row, 1);
  if (addressItem == nullptr) {
    return false;
  }

  QString text = addressItem->text().trimmed();
  if (text.startsWith(("0x"), Qt::CaseInsensitive)) {
    text = text.mid(2);
  }

  bool             ok      = false;
  const qulonglong address = text.toULongLong(&ok, 16);
  if (!ok || address == 0) {
    return false;
  }

  outAddress = static_cast<std::uintptr_t>(address);
  outType    = (m_valueTypeCombo == nullptr) ? ("-") : m_valueTypeCombo->currentText();
  outValue   = (valueItem == nullptr) ? ("-") : valueItem->text();
  return true;
}

bool MainWindow::selectedAddressFromAddressList(std::uintptr_t& outAddress) const {
  outAddress = 0;

  if (m_addressListTable == nullptr) {
    return false;
  }

  const int row = m_addressListTable->currentRow();
  if (row < 0) {
    return false;
  }

  auto* addressItem = m_addressListTable->item(row, 2);
  if (addressItem == nullptr) {
    return false;
  }

  const qulonglong storedAddress = addressItem->data(Qt::UserRole).toULongLong();
  if (storedAddress != 0) {
    outAddress = static_cast<std::uintptr_t>(storedAddress);
    return true;
  }

  QString text = addressItem->text().trimmed();
  if (text.startsWith(("0x"), Qt::CaseInsensitive)) {
    text = text.mid(2);
  }

  bool             ok            = false;
  const qulonglong parsedAddress = text.toULongLong(&ok, 16);
  if (!ok || parsedAddress == 0) {
    return false;
  }

  outAddress = static_cast<std::uintptr_t>(parsedAddress);
  return true;
}

void MainWindow::openAddressInMemoryViewer(std::uintptr_t address) {
  if (address == 0) {
    return;
  }

  showMemoryViewerWindow();
  if (m_memoryViewerWindow != nullptr) {
    m_memoryViewerWindow->focusAddress(address);
  }
}

void MainWindow::openAddressInStructureDissector(std::uintptr_t address) {
  if (address == 0) {
    return;
  }

  showStructureDissectorWindow();
  if (m_structureDissectorWindow != nullptr) {
    m_structureDissectorWindow->focusAddress(address);
  }
}

bool MainWindow::writeAddressValue(std::uintptr_t address,
                                   const QString& typeName,
                                   const QString& inputText) {
  if (m_memoryReader == nullptr || !m_memoryReader->attached() || address == 0) {
    return false;
  }

  const QString type  = typeName.trimmed().toLower();
  const QString input = inputText.trimmed();
  if (input.isEmpty()) {
    return false;
  }

  if (type.contains(("string"))) {
    const QByteArray bytes = input.toLatin1();
    return !bytes.isEmpty()
           && m_memoryReader->writeBytes(
               address, bytes.constData(), static_cast<std::size_t>(bytes.size()));
  }

  if (type.contains(("float"))) {
    bool        ok    = false;
    const float value = input.toFloat(&ok);
    return ok && m_memoryReader->write<float>(address, value);
  }

  if (type.contains(("double"))) {
    bool         ok    = false;
    const double value = input.toDouble(&ok);
    return ok && m_memoryReader->write<double>(address, value);
  }

  auto parseUnsigned = [&input](bool& ok) -> qulonglong { return input.toULongLong(&ok, 0); };
  auto parseSigned   = [&input](bool& ok) -> qlonglong { return input.toLongLong(&ok, 0); };

  if (type.contains(("1 byte")) || type == ("byte")) {
    bool             okUnsigned = false;
    const qulonglong u          = parseUnsigned(okUnsigned);
    if (okUnsigned && u <= 0xFFull) {
      return m_memoryReader->write<std::uint8_t>(address, static_cast<std::uint8_t>(u));
    }
    bool            okSigned = false;
    const qlonglong s        = parseSigned(okSigned);
    if (okSigned && s >= std::numeric_limits<std::int8_t>::min()
        && s <= std::numeric_limits<std::int8_t>::max()) {
      return m_memoryReader->write<std::int8_t>(address, static_cast<std::int8_t>(s));
    }
    return false;
  }

  if (type.contains(("2 bytes")) || type.contains(("short")) || type == ("word")) {
    bool             okUnsigned = false;
    const qulonglong u          = parseUnsigned(okUnsigned);
    if (okUnsigned && u <= 0xFFFFull) {
      return m_memoryReader->write<std::uint16_t>(address, static_cast<std::uint16_t>(u));
    }
    bool            okSigned = false;
    const qlonglong s        = parseSigned(okSigned);
    if (okSigned && s >= std::numeric_limits<std::int16_t>::min()
        && s <= std::numeric_limits<std::int16_t>::max()) {
      return m_memoryReader->write<std::int16_t>(address, static_cast<std::int16_t>(s));
    }
    return false;
  }

  if (type.contains(("8 bytes")) || type.contains(("qword")) || type.contains(("int64"))) {
    bool             okUnsigned = false;
    const qulonglong u          = parseUnsigned(okUnsigned);
    if (okUnsigned) {
      return m_memoryReader->write<std::uint64_t>(address, static_cast<std::uint64_t>(u));
    }
    bool            okSigned = false;
    const qlonglong s        = parseSigned(okSigned);
    return okSigned && m_memoryReader->write<std::int64_t>(address, static_cast<std::int64_t>(s));
  }

  bool             okUnsigned = false;
  const qulonglong u          = parseUnsigned(okUnsigned);
  if (okUnsigned && u <= 0xFFFFFFFFull) {
    return m_memoryReader->write<std::uint32_t>(address, static_cast<std::uint32_t>(u));
  }
  bool            okSigned = false;
  const qlonglong s        = parseSigned(okSigned);
  return okSigned && s >= std::numeric_limits<std::int32_t>::min()
         && s <= std::numeric_limits<std::int32_t>::max()
         && m_memoryReader->write<std::int32_t>(address, static_cast<std::int32_t>(s));
}

void MainWindow::promptSetValueForAddress(std::uintptr_t address,
                                          const QString& typeName,
                                          const QString& currentValue) {
  if (address == 0) {
    return;
  }

  bool          accepted = false;
  const QString input    = QInputDialog::getText(
      this,
      ("Set Value"),
      QString(("Address 0x%1 (%2):")).arg(static_cast<qulonglong>(address), 0, 16).arg(typeName),
      QLineEdit::Normal,
      currentValue,
      &accepted);
  if (!accepted) {
    return;
  }

  if (!writeAddressValue(address, typeName, input)) {
    QMessageBox::warning(this, ("Set Value"), ("Failed to write value."));
    return;
  }

  refreshScanResultsLiveValues();
  refreshAddressListLiveValues();
}

void MainWindow::promptSetValueForAddressSelection(const std::vector<int>& selectedRowsInput) {
  if (m_addressListTable == nullptr) {
    return;
  }

  std::vector<int> rows = selectedRowsInput;
  if (rows.empty()) {
    rows = selectedAddressListRows();
  }
  if (rows.empty()) {
    return;
  }

  QString initialValue;
  if (auto* valueItem = m_addressListTable->item(rows.front(), 4); valueItem != nullptr) {
    initialValue = valueItem->text();
  }

  bool          accepted = false;
  const QString input =
      QInputDialog::getText(this,
                            ("Set Value"),
                            QString(("Set value for %1 selected addresses:")).arg(rows.size()),
                            QLineEdit::Normal,
                            initialValue,
                            &accepted);
  if (!accepted) {
    return;
  }

  int successCount = 0;
  int failCount    = 0;
  for (const int row : rows) {
    if (row < 0 || row >= m_addressListTable->rowCount()) {
      ++failCount;
      continue;
    }

    auto* addressItem = m_addressListTable->item(row, 2);
    auto* typeItem    = m_addressListTable->item(row, 3);
    if (addressItem == nullptr || typeItem == nullptr) {
      ++failCount;
      continue;
    }

    std::uintptr_t   address       = 0;
    const qulonglong storedAddress = addressItem->data(Qt::UserRole).toULongLong();
    if (storedAddress != 0) {
      address = static_cast<std::uintptr_t>(storedAddress);
    } else {
      QString text = addressItem->text().trimmed();
      if (text.startsWith(("0x"), Qt::CaseInsensitive)) {
        text = text.mid(2);
      }
      bool             ok     = false;
      const qulonglong parsed = text.toULongLong(&ok, 16);
      if (!ok || parsed == 0) {
        ++failCount;
        continue;
      }
      address = static_cast<std::uintptr_t>(parsed);
    }

    if (writeAddressValue(address, typeItem->text(), input)) {
      ++successCount;
    } else {
      ++failCount;
    }
  }

  refreshScanResultsLiveValues();
  refreshAddressListLiveValues();

  if (failCount > 0) {
    QMessageBox::warning(
        this,
        ("Set Value"),
        QString(("Wrote %1 address(es), failed %2.")).arg(successCount).arg(failCount));
  }
}

void MainWindow::promptLoopSetValueForScanSelection(const std::vector<int>& selectedRows) {
  if (selectedRows.empty() || m_scanResultsTable == nullptr) {
    return;
  }

  if (m_memoryReader == nullptr || !m_memoryReader->attached()) {
    QMessageBox::information(this, ("Loop Set Value"), ("Attach to a process first."));
    return;
  }

  QString defaultValue;
  if (auto* valueItem = m_scanResultsTable->item(selectedRows.front(), 1); valueItem != nullptr) {
    defaultValue = valueItem->text();
  }

  bool          accepted = false;
  const QString input    = QInputDialog::getText(
      this,
      ("Loop Set Value"),
      QString(("Set value for %1 selected addresses:")).arg(selectedRows.size()),
      QLineEdit::Normal,
      defaultValue,
      &accepted);
  if (!accepted) {
    return;
  }

  int intervalMs = QInputDialog::getInt(
      this, ("Loop Set Value"), ("Write interval (ms):"), 100, 10, 60000, 10, &accepted);
  if (!accepted) {
    return;
  }

  const QString type =
      (m_valueTypeCombo == nullptr) ? ("4 Bytes") : m_valueTypeCombo->currentText();
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

  int added = 0;
  for (const int row : selectedRows) {
    if (row < 0 || row >= m_scanResultsTable->rowCount()) {
      continue;
    }

    auto* addressItem = m_scanResultsTable->item(row, 0);
    if (addressItem == nullptr) {
      continue;
    }

    QString text = addressItem->text().trimmed();
    if (text.startsWith(("0x"), Qt::CaseInsensitive)) {
      text = text.mid(2);
    }

    bool             ok            = false;
    const qulonglong parsedAddress = text.toULongLong(&ok, 16);
    if (!ok || parsedAddress == 0) {
      continue;
    }

    LoopWriteEntry entry;
    entry.id          = m_nextLoopWriteEntryId++;
    entry.address     = static_cast<std::uintptr_t>(parsedAddress);
    entry.type        = type;
    entry.value       = input;
    entry.intervalMs  = intervalMs;
    entry.nextRunAtMs = nowMs;
    entry.source      = QString(("Scan Results row %1")).arg(row + 1);
    m_loopWriteEntries.push_back(entry);
    ++added;
  }

  if (added <= 0) {
    QMessageBox::warning(this, ("Loop Set Value"), ("No valid addresses were selected."));
    return;
  }

  if (m_loopWriteTimer != nullptr && !m_loopWriteTimer->isActive()) {
    m_loopWriteTimer->start();
  }
  refreshLoopWriteManagerWindow();
}

void MainWindow::promptLoopSetValueForAddressListSelection(const std::vector<int>& selectedRows) {
  if (selectedRows.empty() || m_addressListTable == nullptr) {
    return;
  }

  if (m_memoryReader == nullptr || !m_memoryReader->attached()) {
    QMessageBox::information(this, ("Loop Set Value"), ("Attach to a process first."));
    return;
  }

  QString defaultValue;
  if (auto* valueItem = m_addressListTable->item(selectedRows.front(), 4); valueItem != nullptr) {
    defaultValue = valueItem->text();
  }

  bool          accepted = false;
  const QString input    = QInputDialog::getText(
      this,
      ("Loop Set Value"),
      QString(("Set value for %1 selected addresses:")).arg(selectedRows.size()),
      QLineEdit::Normal,
      defaultValue,
      &accepted);
  if (!accepted) {
    return;
  }

  int intervalMs = QInputDialog::getInt(
      this, ("Loop Set Value"), ("Write interval (ms):"), 100, 10, 60000, 10, &accepted);
  if (!accepted) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  int          added = 0;

  for (const int row : selectedRows) {
    if (row < 0 || row >= m_addressListTable->rowCount()) {
      continue;
    }

    auto* addressItem     = m_addressListTable->item(row, 2);
    auto* typeItem        = m_addressListTable->item(row, 3);
    auto* descriptionItem = m_addressListTable->item(row, 1);
    if (addressItem == nullptr || typeItem == nullptr) {
      continue;
    }

    std::uintptr_t   address       = 0;
    const qulonglong storedAddress = addressItem->data(Qt::UserRole).toULongLong();
    if (storedAddress != 0) {
      address = static_cast<std::uintptr_t>(storedAddress);
    } else {
      QString text = addressItem->text().trimmed();
      if (text.startsWith(("0x"), Qt::CaseInsensitive)) {
        text = text.mid(2);
      }
      bool             ok     = false;
      const qulonglong parsed = text.toULongLong(&ok, 16);
      if (!ok || parsed == 0) {
        continue;
      }
      address = static_cast<std::uintptr_t>(parsed);
    }

    LoopWriteEntry entry;
    entry.id          = m_nextLoopWriteEntryId++;
    entry.address     = address;
    entry.type        = typeItem->text();
    entry.value       = input;
    entry.intervalMs  = intervalMs;
    entry.nextRunAtMs = nowMs;
    entry.source      = (descriptionItem == nullptr || descriptionItem->text().trimmed().isEmpty())
                            ? ("Address List")
                            : descriptionItem->text().trimmed();
    m_loopWriteEntries.push_back(entry);
    ++added;
  }

  if (added <= 0) {
    QMessageBox::warning(this, ("Loop Set Value"), ("No valid addresses were selected."));
    return;
  }

  if (m_loopWriteTimer != nullptr && !m_loopWriteTimer->isActive()) {
    m_loopWriteTimer->start();
  }
  refreshLoopWriteManagerWindow();
}

void MainWindow::processLoopWriteEntries() {
  if (m_loopWriteEntries.empty()) {
    if (m_loopWriteTimer != nullptr) {
      m_loopWriteTimer->stop();
    }
    return;
  }

  if (m_memoryReader == nullptr || !m_memoryReader->attached()) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  for (auto& entry : m_loopWriteEntries) {
    if (entry.address == 0) {
      continue;
    }
    if (entry.intervalMs < 10) {
      entry.intervalMs = 10;
    }
    if (nowMs < entry.nextRunAtMs) {
      continue;
    }

    const bool wrote = writeAddressValue(entry.address, entry.type, entry.value);
    (void)wrote;
    entry.nextRunAtMs = nowMs + entry.intervalMs;
  }
}

void MainWindow::refreshLoopWriteManagerWindow() {
  if (m_loopWriteManagerWindow != nullptr) {
    m_loopWriteManagerWindow->setEntries(m_loopWriteEntries);
  }
}

void MainWindow::stopLoopWriteEntriesByIds(const std::vector<std::uint64_t>& ids) {
  if (ids.empty() || m_loopWriteEntries.empty()) {
    return;
  }

  std::vector<std::uint64_t> sortedIds = ids;
  std::sort(sortedIds.begin(), sortedIds.end());
  sortedIds.erase(std::unique(sortedIds.begin(), sortedIds.end()), sortedIds.end());

  m_loopWriteEntries.erase(std::remove_if(m_loopWriteEntries.begin(),
                                          m_loopWriteEntries.end(),
                                          [&sortedIds](const LoopWriteEntry& entry) {
                                            return std::binary_search(
                                                sortedIds.begin(), sortedIds.end(), entry.id);
                                          }),
                           m_loopWriteEntries.end());

  if (m_loopWriteEntries.empty() && m_loopWriteTimer != nullptr) {
    m_loopWriteTimer->stop();
  }

  refreshLoopWriteManagerWindow();
}

std::vector<int> MainWindow::selectedAddressListRows() const {
  std::vector<int> rows;
  if (m_addressListTable == nullptr) {
    return rows;
  }

  const auto selected = m_addressListTable->selectionModel()->selectedRows();
  rows.reserve(static_cast<std::size_t>(selected.size()));
  for (const auto& index : selected) {
    if (index.isValid()) {
      rows.push_back(index.row());
    }
  }

  if (rows.empty()) {
    const int currentRow = m_addressListTable->currentRow();
    if (currentRow >= 0) {
      rows.push_back(currentRow);
    }
  }

  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
  return rows;
}

void MainWindow::refreshScanResultsLiveValues() {
  if (m_scanResultsTable == nullptr || m_homeScanner == nullptr || m_memoryReader == nullptr
      || !m_memoryReader->attached()) {
    return;
  }

  const auto& entries = m_homeScanner->results();
  if (entries.empty()) {
    return;
  }

  int firstVisible = m_scanResultsTable->rowAt(0);
  int lastVisible  = m_scanResultsTable->rowAt(m_scanResultsTable->viewport()->height() - 1);
  if (firstVisible < 0) {
    firstVisible = 0;
  }
  if (lastVisible < 0) {
    lastVisible = std::min(m_scanResultsTable->rowCount() - 1, firstVisible + 64);
  }
  lastVisible = std::min(lastVisible, m_scanResultsTable->rowCount() - 1);

  for (int row = firstVisible; row <= lastVisible; ++row) {
    if (row < 0 || static_cast<std::size_t>(row) >= entries.size()) {
      continue;
    }

    auto* valueItem = m_scanResultsTable->item(row, 1);
    if (valueItem == nullptr) {
      continue;
    }

    const std::uintptr_t address      = entries[static_cast<std::size_t>(row)].address;
    const QString        updatedValue = readLiveValueForScanRow(address, row);
    if (!updatedValue.isEmpty()) {
      valueItem->setText(updatedValue);
    }
  }
}

void MainWindow::refreshAddressListLiveValues() {
  if (m_addressListTable == nullptr || m_memoryReader == nullptr || !m_memoryReader->attached()) {
    return;
  }

  int firstVisible = m_addressListTable->rowAt(0);
  int lastVisible  = m_addressListTable->rowAt(m_addressListTable->viewport()->height() - 1);
  if (firstVisible < 0) {
    firstVisible = 0;
  }
  if (lastVisible < 0) {
    lastVisible = std::min(m_addressListTable->rowCount() - 1, firstVisible + 64);
  }
  lastVisible = std::min(lastVisible, m_addressListTable->rowCount() - 1);

  for (int row = firstVisible; row <= lastVisible; ++row) {
    auto* addressItem = m_addressListTable->item(row, 2);
    auto* typeItem    = m_addressListTable->item(row, 3);
    auto* valueItem   = m_addressListTable->item(row, 4);
    if (addressItem == nullptr || typeItem == nullptr || valueItem == nullptr) {
      continue;
    }

    std::uintptr_t   address       = 0;
    const qulonglong storedAddress = addressItem->data(Qt::UserRole).toULongLong();
    if (storedAddress != 0) {
      address = static_cast<std::uintptr_t>(storedAddress);
    } else {
      QString text = addressItem->text().trimmed();
      if (text.startsWith(("0x"), Qt::CaseInsensitive)) {
        text = text.mid(2);
      }
      bool             ok     = false;
      const qulonglong parsed = text.toULongLong(&ok, 16);
      if (!ok || parsed == 0) {
        continue;
      }
      address = static_cast<std::uintptr_t>(parsed);
    }

    const QString liveValue = readLiveValueForAddress(
        address, typeItem->text(), m_hexCheckBox != nullptr && m_hexCheckBox->isChecked());
    if (!liveValue.isEmpty()) {
      valueItem->setText(liveValue);
    }
  }
}

QString MainWindow::readLiveValueForAddress(std::uintptr_t address,
                                            const QString& typeName,
                                            bool           hexMode) const {
  if (m_memoryReader == nullptr || !m_memoryReader->attached() || address == 0) {
    return {};
  }

  const QString type = typeName.trimmed().toLower();

  if (type.contains(("string"))) {
    constexpr std::size_t kMaxStringBytes = 64;
    std::vector<char>     buffer(kMaxStringBytes, 0);
    if (!m_memoryReader->readBytes(address, buffer.data(), buffer.size())) {
      return ("??");
    }
    std::size_t len = 0;
    while (len < buffer.size() && buffer[len] != '\0') {
      ++len;
    }
    return QString::fromLatin1(buffer.data(), static_cast<int>(len));
  }

  if (type.contains(("float"))) {
    const auto value = m_memoryReader->read<float>(address);
    return value.has_value() ? QString::number(*value, 'g', 8) : ("??");
  }

  if (type.contains(("double"))) {
    const auto value = m_memoryReader->read<double>(address);
    return value.has_value() ? QString::number(*value, 'g', 14) : ("??");
  }

  if (type.contains(("1 byte")) || type == ("byte")) {
    const auto value = m_memoryReader->read<std::uint8_t>(address);
    if (!value.has_value()) {
      return ("??");
    }
    if (hexMode) {
      return QString(("0x%1")).arg(*value, 2, 16, QChar('0')).toUpper();
    }
    return QString::number(*value);
  }

  if (type.contains(("2 bytes")) || type == ("short")) {
    const auto value = m_memoryReader->read<std::int16_t>(address);
    if (!value.has_value()) {
      return ("??");
    }
    if (hexMode) {
      return QString(("0x%1"))
          .arg(static_cast<qulonglong>(static_cast<std::uint16_t>(*value)), 4, 16, QChar('0'))
          .toUpper();
    }
    return QString::number(*value);
  }

  if (type.contains(("8 bytes")) || type.contains(("qword")) || type.contains(("int64"))) {
    const auto value = m_memoryReader->read<std::int64_t>(address);
    if (!value.has_value()) {
      return ("??");
    }
    if (hexMode) {
      return QString(("0x%1")).arg(static_cast<qulonglong>(*value), 16, 16, QChar('0')).toUpper();
    }
    return QString::number(*value);
  }

  const auto value = m_memoryReader->read<std::int32_t>(address);
  if (!value.has_value()) {
    return ("??");
  }
  if (hexMode) {
    return QString(("0x%1"))
        .arg(static_cast<qulonglong>(static_cast<std::uint32_t>(*value)), 8, 16, QChar('0'))
        .toUpper();
  }
  return QString::number(*value);
}

QString MainWindow::readLiveValueForScanRow(std::uintptr_t address, int row) const {
  if (m_homeScanner == nullptr || m_memoryReader == nullptr || !m_memoryReader->attached()
      || address == 0) {
    return {};
  }

  const auto& entries = m_homeScanner->results();
  if (row < 0 || static_cast<std::size_t>(row) >= entries.size()) {
    return {};
  }

  const auto        settings = m_homeScanner->lastSettings();
  const std::size_t size     = entries[static_cast<std::size_t>(row)].currentValue.size();
  if (size == 0) {
    return {};
  }

  std::vector<std::uint8_t> bytes(size, 0);
  if (!m_memoryReader->readBytes(address, bytes.data(), size)) {
    return ("??");
  }

  switch (settings.valueType) {
    case memory::ScanValueType::Int8: {
      std::int8_t value = 0;
      std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
      return QString::number(static_cast<int>(value));
    }
    case memory::ScanValueType::Int16: {
      std::int16_t value = 0;
      std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
      return QString::number(value);
    }
    case memory::ScanValueType::Int32: {
      std::int32_t value = 0;
      std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
      return QString::number(value);
    }
    case memory::ScanValueType::Int64: {
      std::int64_t value = 0;
      std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
      return QString::number(value);
    }
    case memory::ScanValueType::Float: {
      float value = 0.0F;
      std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
      return QString::number(value, 'g', 8);
    }
    case memory::ScanValueType::Double: {
      double value = 0.0;
      std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
      return QString::number(value, 'g', 14);
    }
    case memory::ScanValueType::String: {
      if (settings.unicode && bytes.size() >= 2) {
        const char16_t* chars  = reinterpret_cast<const char16_t*>(bytes.data());
        const int       length = static_cast<int>(bytes.size() / sizeof(char16_t));
        return QString::fromUtf16(chars, length);
      }
      return QString::fromLatin1(reinterpret_cast<const char*>(bytes.data()),
                                 static_cast<int>(bytes.size()));
    }
  }

  return {};
}

QString MainWindow::settingsFilePath() const {
  QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
  if (localAppData.isEmpty()) {
    localAppData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  }

  if (localAppData.isEmpty()) {
    return {};
  }

  QDir          baseDir(localAppData);
  const QString relativeDir = ("farcalenginev2");
  if (!baseDir.mkpath(relativeDir)) {
    return {};
  }

  return baseDir.filePath(relativeDir + ("/keybinds.json"));
}

void MainWindow::loadKeybindSettings() {
  m_keybindSettings = KeybindSettings::defaults();

  const auto loadFromPath = [this](const QString& path) -> bool {
    if (path.isEmpty()) {
      return false;
    }

    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
      return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError     parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
      return false;
    }

    const QJsonObject root     = document.object();
    QJsonObject       keybinds = root.value(("keybinds")).toObject();
    if (keybinds.isEmpty()) {
      keybinds = root;
    }
    if (keybinds.isEmpty()) {
      return false;
    }

    auto parseOrFallback = [&keybinds](const QString& key, const QKeySequence& fallback) {
      const QString text = keybinds.value(key).toString().trimmed();
      if (text.isEmpty()) {
        return fallback;
      }
      const QKeySequence parsed(text, QKeySequence::PortableText);
      return parsed.isEmpty() ? fallback : parsed;
    };

    const KeybindSettings defaults = KeybindSettings::defaults();
    m_keybindSettings.openStructureDissector =
        parseOrFallback(("open_structure_dissector"), defaults.openStructureDissector);
    m_keybindSettings.openLuaVm = parseOrFallback(("open_luavm"), defaults.openLuaVm);
    m_keybindSettings.openRttiScanner =
        parseOrFallback(("open_rtti_scanner"), defaults.openRttiScanner);
    m_keybindSettings.openStringScanner =
        parseOrFallback(("open_string_scanner"), defaults.openStringScanner);
    m_keybindSettings.attachToProcess =
        parseOrFallback(("attach_to_process"), defaults.attachToProcess);
    m_keybindSettings.attachSavedProcess =
        parseOrFallback(("attach_saved_process"), defaults.attachSavedProcess);
    return true;
  };

  const QString filePath = settingsFilePath();
  if (loadFromPath(filePath)) {
    return;
  }

  if (!filePath.isEmpty()) {
    const QString legacyFilePath =
        filePath.left(filePath.lastIndexOf(("/")) + 1) + ("settings.json");
    (void)loadFromPath(legacyFilePath);
  }
}

void MainWindow::saveKeybindSettings() const {
  const QString filePath = settingsFilePath();
  if (filePath.isEmpty()) {
    return;
  }

  QJsonObject keybinds;
  keybinds.insert(("open_structure_dissector"),
                  m_keybindSettings.openStructureDissector.toString(QKeySequence::PortableText));
  keybinds.insert(("open_luavm"), m_keybindSettings.openLuaVm.toString(QKeySequence::PortableText));
  keybinds.insert(("open_rtti_scanner"),
                  m_keybindSettings.openRttiScanner.toString(QKeySequence::PortableText));
  keybinds.insert(("open_string_scanner"),
                  m_keybindSettings.openStringScanner.toString(QKeySequence::PortableText));
  keybinds.insert(("attach_to_process"),
                  m_keybindSettings.attachToProcess.toString(QKeySequence::PortableText));
  keybinds.insert(("attach_saved_process"),
                  m_keybindSettings.attachSavedProcess.toString(QKeySequence::PortableText));

  QJsonObject root;
  root.insert(("keybinds"), keybinds);
  root.insert(("saved_at_unix"), static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));

  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  file.close();
}

void MainWindow::applyKeybindSettings() {
  auto applyShortcut = [](QAction* action, const QKeySequence& sequence) {
    if (action == nullptr) {
      return;
    }
    action->setShortcut(sequence);
    action->setShortcutContext(Qt::ApplicationShortcut);
  };

  applyShortcut(m_structureDissectorAction, m_keybindSettings.openStructureDissector);
  applyShortcut(m_luaIdeAction, m_keybindSettings.openLuaVm);
  applyShortcut(m_rttiScannerAction, m_keybindSettings.openRttiScanner);
  applyShortcut(m_stringScannerAction, m_keybindSettings.openStringScanner);
  applyShortcut(m_attachToProcessAction, m_keybindSettings.attachToProcess);
  applyShortcut(m_attachLastProcessAction, m_keybindSettings.attachSavedProcess);
}

QString MainWindow::lastProcessFilePath() const {
  QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
  if (localAppData.isEmpty()) {
    localAppData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  }

  if (localAppData.isEmpty()) {
    return {};
  }

  QDir          baseDir(localAppData);
  const QString relativeDir = ("farcalenginev2");
  if (!baseDir.mkpath(relativeDir)) {
    return {};
  }

  return baseDir.filePath(relativeDir + ("/last_process.json"));
}

void MainWindow::persistLastAttachedProcess(std::uint32_t  processId,
                                            const QString& processName) const {
  if (processId == 0 || processName.trimmed().isEmpty()) {
    return;
  }

  const QString filePath = lastProcessFilePath();
  if (filePath.isEmpty()) {
    return;
  }

  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return;
  }

  QJsonObject payload;
  payload.insert(("pid"), static_cast<qint64>(processId));
  payload.insert(("process_name"), processName);
  payload.insert(("saved_at_unix"), static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));

  file.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
  file.close();
}

std::optional<std::pair<std::uint32_t, QString>> MainWindow::loadLastAttachedProcess() const {
  const QString filePath = lastProcessFilePath();
  if (filePath.isEmpty()) {
    return std::nullopt;
  }

  QFile file(filePath);
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }

  const QByteArray data = file.readAll();
  file.close();

  QJsonParseError     parseError{};
  const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
    return std::nullopt;
  }

  const QJsonObject obj         = json.object();
  const qint64      pid64       = obj.value(("pid")).toInteger(0);
  const QString     processName = obj.value(("process_name")).toString();
  if (pid64 <= 0 || processName.trimmed().isEmpty()) {
    return std::nullopt;
  }

  return std::make_optional(
      std::make_pair(static_cast<std::uint32_t>(pid64), processName.trimmed()));
}

std::optional<std::uint32_t> MainWindow::findRunningProcessIdByName(
    const QString& processName) const {
#ifdef Q_OS_WIN
  const QString target = processName.trimmed();
  if (target.isEmpty()) {
    return std::nullopt;
  }

  const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }

  PROCESSENTRY32W processEntry{};
  processEntry.dwSize = sizeof(processEntry);

  if (::Process32FirstW(snapshot, &processEntry) == FALSE) {
    ::CloseHandle(snapshot);
    return std::nullopt;
  }

  std::optional<std::uint32_t> foundPid;
  do {
    const QString exeName = QString::fromWCharArray(processEntry.szExeFile);
    if (exeName.compare(target, Qt::CaseInsensitive) == 0) {
      foundPid = static_cast<std::uint32_t>(processEntry.th32ProcessID);
      break;
    }
  } while (::Process32NextW(snapshot, &processEntry) != FALSE);

  ::CloseHandle(snapshot);
  return foundPid;
#else
  (void)processName;
  return std::nullopt;
#endif
}

}  // namespace farcal::ui
