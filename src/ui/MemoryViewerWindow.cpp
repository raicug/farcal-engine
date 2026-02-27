#include "farcal/ui/MemoryViewerWindow.hpp"
#include "q_lit.hpp"

#include "farcal/memory/MemoryReader.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QBrush>
#include <QColor>
#include <QDialog>
#include <QFrame>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cctype>
#include <cstring>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace farcal::ui {

namespace {

constexpr int            kDisassemblyRows = 4096;
constexpr int            kHexRows         = 1024;
constexpr int            kBytesPerHexRow  = 16;
constexpr std::uintptr_t kDefaultAddress  = 0x00400000;

QString formatAddress(std::uintptr_t address) {
  constexpr int kAddressWidth = sizeof(std::uintptr_t) * 2;
  return QString(("%1"))
      .arg(static_cast<qulonglong>(address), kAddressWidth, 16, QChar('0'))
      .toUpper();
}

QString formatByte(std::uint8_t value) {
  return QString(("%1")).arg(value, 2, 16, QChar('0')).toUpper();
}

std::uintptr_t alignAddressForHex(std::uintptr_t address) {
  return address & ~static_cast<std::uintptr_t>(kBytesPerHexRow - 1);
}

QTableWidgetItem* ensureItem(QTableWidget* table, int row, int column) {
  if (table == nullptr) {
    return nullptr;
  }

  QTableWidgetItem* item = table->item(row, column);
  if (item == nullptr) {
    item = new QTableWidgetItem();
    table->setItem(row, column, item);
  }

  return item;
}

void readMemoryChunked(const memory::MemoryReader& reader,
                       std::uintptr_t              address,
                       std::size_t                 size,
                       std::vector<std::uint8_t>&  outBytes,
                       std::vector<std::uint8_t>&  outValid) {
  outBytes.assign(size, 0);
  outValid.assign(size, 0);
  if (size == 0) {
    return;
  }

  constexpr std::size_t     kLargeChunk = 1024;
  constexpr std::size_t     kSmallChunk = 16;
  std::vector<std::uint8_t> buffer(kLargeChunk);

  std::size_t offset = 0;
  while (offset < size) {
    const std::size_t chunkSize = (std::min)(kLargeChunk, size - offset);
    if (reader.readBytes(address + offset, buffer.data(), chunkSize)) {
      std::memcpy(outBytes.data() + offset, buffer.data(), chunkSize);
      std::fill_n(outValid.begin() + static_cast<std::ptrdiff_t>(offset),
                  chunkSize,
                  static_cast<std::uint8_t>(1));
      offset += chunkSize;
      continue;
    }

    std::size_t localOffset = 0;
    while (localOffset < chunkSize) {
      const std::size_t smallSize = (std::min)(kSmallChunk, chunkSize - localOffset);
      if (reader.readBytes(address + offset + localOffset, buffer.data(), smallSize)) {
        std::memcpy(outBytes.data() + offset + localOffset, buffer.data(), smallSize);
        std::fill_n(outValid.begin() + static_cast<std::ptrdiff_t>(offset + localOffset),
                    smallSize,
                    static_cast<std::uint8_t>(1));
      } else {
        for (std::size_t i = 0; i < smallSize; ++i) {
          const auto value = reader.read<std::uint8_t>(address + offset + localOffset + i);
          if (value.has_value()) {
            outBytes[offset + localOffset + i] = *value;
            outValid[offset + localOffset + i] = 1;
          }
        }
      }

      localOffset += smallSize;
    }

    offset += chunkSize;
  }
}

}  // namespace

MemoryViewerWindow::MemoryViewerWindow(QWidget* parent)
  : QMainWindow(parent),
    m_memoryReader(std::make_unique<memory::MemoryReader>()),
    m_viewBaseAddress(alignAddressForHex(kDefaultAddress)),
    m_currentAddress(kDefaultAddress) {
  applyTheme();
  configureWindow();
  updateProcessState();
  clearViewerData();
}

MemoryViewerWindow::~MemoryViewerWindow() = default;

void MemoryViewerWindow::setAttachedProcess(std::uint32_t processId, const QString& processName) {
  m_processId   = processId;
  m_processName = processName;

  if (m_memoryReader == nullptr || processId == 0 || processName.isEmpty()) {
    if (m_memoryReader != nullptr) {
      m_memoryReader->detach();
    }
    m_regions.clear();
    clearViewerData();
    updateProcessState();
    return;
  }

  const bool attached = m_memoryReader->attach(static_cast<memory::Process::Id>(processId));
  if (!attached) {
    m_processId = 0;
    m_processName.clear();
    m_memoryReader->detach();
    m_regions.clear();
    clearViewerData();
    updateProcessState();
    QMessageBox::warning(
        this, ("Memory Viewer"), ("Failed to attach memory viewer to the selected process."));
    return;
  }

  refreshRegionList();

  if (!m_regions.empty()) {
    m_currentAddress = m_regions.front().base;
  } else {
    m_currentAddress = kDefaultAddress;
  }

  m_viewBaseAddress = alignAddressForHex(m_currentAddress);
  refreshViews();
  updateProcessState();
}

void MemoryViewerWindow::focusAddress(std::uintptr_t address) {
  if (address == 0) {
    return;
  }

  m_currentAddress  = address;
  m_viewBaseAddress = alignAddressForHex(address);

  if (m_memoryReader != nullptr && m_memoryReader->attached()) {
    refreshViewAt(address);
  }
}

void MemoryViewerWindow::applyTheme() {
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
QFrame#panel {
  background-color: #2b2e36;
  border: 1px solid #4a4e58;
  border-radius: 6px;
}
QTableWidget {
  background-color: #1a1c21;
  border: 1px solid #4a4e58;
  border-radius: 6px;
  color: #e8eaed;
  gridline-color: #353841;
}
QHeaderView::section {
  background-color: #35373d;
  color: #e8eaed;
  border: 1px solid #4f535e;
  padding: 5px;
}
QTableWidget::item:selected {
  background-color: #3c404b;
  color: #ffffff;
}
QSplitter::handle {
  background-color: #53565f;
})"));
}

void MemoryViewerWindow::configureWindow() {
  resize(1200, 780);
  configureMenuBar();
  setCentralWidget(buildCentralArea());

  auto* gotoAction = new QAction(("Go To Address"), this);
  gotoAction->setShortcut(QKeySequence(Qt::Key_G));
  gotoAction->setShortcutContext(Qt::WindowShortcut);
  connect(gotoAction, &QAction::triggered, this, &MemoryViewerWindow::openGotoAddressDialog);
  addAction(gotoAction);

  auto* refreshAction = new QAction(("Refresh"), this);
  refreshAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
  refreshAction->setShortcutContext(Qt::WindowShortcut);
  connect(refreshAction, &QAction::triggered, this, &MemoryViewerWindow::refreshViews);
  addAction(refreshAction);
}

void MemoryViewerWindow::configureMenuBar() {
  auto* topMenu = menuBar();
  topMenu->addMenu(("File"));

  auto* searchMenu = topMenu->addMenu(("Search"));
  auto* gotoAction = searchMenu->addAction(("Go To Address"));
  connect(gotoAction, &QAction::triggered, this, &MemoryViewerWindow::openGotoAddressDialog);

  auto* viewMenu            = topMenu->addMenu(("View"));
  auto* protectionMapAction = viewMenu->addAction(("Address Protection Map"));
  connect(protectionMapAction,
          &QAction::triggered,
          this,
          &MemoryViewerWindow::showAddressProtectionMap);
  topMenu->addMenu(("Debug"));

  auto* toolsMenu     = topMenu->addMenu(("Tools"));
  auto* refreshAction = toolsMenu->addAction(("Refresh"));
  connect(refreshAction, &QAction::triggered, this, &MemoryViewerWindow::refreshViews);

  topMenu->addMenu(("Kernel Tools"));
}

void MemoryViewerWindow::showAddressProtectionMap() {
  if (m_memoryReader == nullptr || !m_memoryReader->attached()) {
    QMessageBox::information(this, ("Address Protection Map"), ("Attach to a process first."));
    return;
  }

  refreshRegionList();

  auto* dialog = new QDialog(this);
  dialog->setWindowTitle(("Address Protection Map"));
  dialog->resize(920, 560);

  auto* layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(8);

  auto* table = new QTableWidget(dialog);
  table->setColumnCount(6);
  table->setHorizontalHeaderLabels(
      {("Base"), ("End"), ("Size"), ("Protection"), ("Category"), ("State")});
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->verticalHeader()->setVisible(false);
  table->setAlternatingRowColors(false);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);

  table->setRowCount(static_cast<int>(m_regions.size()));
  for (int row = 0; row < static_cast<int>(m_regions.size()); ++row) {
    const auto&          region = m_regions[static_cast<std::size_t>(row)];
    const std::uintptr_t end    = region.base + static_cast<std::uintptr_t>(region.size);

    auto* baseItem = new QTableWidgetItem(formatAddress(region.base));
    auto* endItem  = new QTableWidgetItem(formatAddress(end));
    auto* sizeItem = new QTableWidgetItem(
        QString(("0x%1")).arg(static_cast<qulonglong>(region.size), 0, 16).toUpper());
    auto* protectionItem =
        new QTableWidgetItem(QString(("0x%1")).arg(region.protection, 0, 16).toUpper());
    auto* categoryItem = new QTableWidgetItem(protectionCategory(region.protection));

    QString stateText;
#ifdef Q_OS_WIN
    if (region.state == MEM_COMMIT) {
      stateText = ("Commit");
    } else if (region.state == MEM_RESERVE) {
      stateText = ("Reserve");
    } else if (region.state == MEM_FREE) {
      stateText = ("Free");
    } else {
      stateText = QString(("0x%1")).arg(region.state, 0, 16).toUpper();
    }
#else
    stateText = QString::number(region.state);
#endif
    auto* stateItem = new QTableWidgetItem(stateText);

    table->setItem(row, 0, baseItem);
    table->setItem(row, 1, endItem);
    table->setItem(row, 2, sizeItem);
    table->setItem(row, 3, protectionItem);
    table->setItem(row, 4, categoryItem);
    table->setItem(row, 5, stateItem);
  }

  layout->addWidget(table, 1);
  dialog->setLayout(layout);
  dialog->exec();
}

QWidget* MemoryViewerWindow::buildCentralArea() {
  auto* root       = new QWidget(this);
  auto* rootLayout = new QVBoxLayout(root);
  rootLayout->setContentsMargins(10, 10, 10, 10);
  rootLayout->setSpacing(10);

  auto* verticalSplit = new QSplitter(Qt::Vertical, root);
  verticalSplit->setChildrenCollapsible(false);
  verticalSplit->setHandleWidth(3);
  verticalSplit->addWidget(buildDisassemblyView());
  verticalSplit->addWidget(buildHexDumpView());
  verticalSplit->setStretchFactor(0, 2);
  verticalSplit->setStretchFactor(1, 3);

  rootLayout->addWidget(verticalSplit, 1);
  return root;
}

QWidget* MemoryViewerWindow::buildDisassemblyView() {
  auto* panel = new QFrame(this);
  panel->setObjectName(("panel"));

  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(8);

  m_disassemblyTable = new QTableWidget(0, 4, panel);
  m_disassemblyTable->setHorizontalHeaderLabels({("Address"), ("Bytes"), ("Opcode"), ("Comment")});
  m_disassemblyTable->verticalHeader()->setVisible(false);
  m_disassemblyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_disassemblyTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_disassemblyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_disassemblyTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_disassemblyTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_disassemblyTable->setWordWrap(false);
  m_disassemblyTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  m_disassemblyTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
  m_disassemblyTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
  m_disassemblyTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  m_disassemblyTable->setColumnWidth(0, 160);
  m_disassemblyTable->setColumnWidth(1, 260);
  m_disassemblyTable->setColumnWidth(2, 340);
  layout->addWidget(m_disassemblyTable, 1);

  connect(
      m_disassemblyTable,
      &QTableWidget::currentCellChanged,
      this,
      [this](int currentRow, int, int, int) {
        if (m_disassemblyTable == nullptr || m_hexGrid == nullptr || currentRow < 0) {
          return;
        }

        QTableWidgetItem* addressItem = m_disassemblyTable->item(currentRow, 0);
        if (addressItem == nullptr) {
          return;
        }

        bool       ok      = false;
        const auto address = static_cast<std::uintptr_t>(addressItem->text().toULongLong(&ok, 16));
        if (!ok) {
          return;
        }

        m_currentAddress = address;

        if (address < m_viewBaseAddress) {
          return;
        }

        const auto localOffset = static_cast<std::size_t>(address - m_viewBaseAddress);
        const auto maxBytes =
            static_cast<std::size_t>(kHexRows) * static_cast<std::size_t>(kBytesPerHexRow);
        if (localOffset >= maxBytes) {
          return;
        }

        const int targetRow =
            static_cast<int>(localOffset / static_cast<std::size_t>(kBytesPerHexRow));
        const int targetColumn =
            1 + static_cast<int>(localOffset % static_cast<std::size_t>(kBytesPerHexRow));
        if (targetRow < 0 || targetRow >= m_hexGrid->rowCount()) {
          return;
        }

        QSignalBlocker blocker(m_hexGrid);
        m_hexGrid->setCurrentCell(targetRow, targetColumn);
        if (QTableWidgetItem* item = m_hexGrid->item(targetRow, targetColumn); item != nullptr) {
          m_hexGrid->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        }
      });

  return panel;
}

QWidget* MemoryViewerWindow::buildHexDumpView() {
  auto* panel = new QFrame(this);
  panel->setObjectName(("panel"));

  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(8);

  m_hexGrid = new QTableWidget(0, 18, panel);
  QStringList headers;
  headers << ("Address");
  for (int i = 0; i < kBytesPerHexRow; ++i) {
    headers << QString(("%1")).arg(i, 2, 16, QChar('0')).toUpper();
  }
  headers << ("ASCII");

  m_hexGrid->setHorizontalHeaderLabels(headers);
  m_hexGrid->verticalHeader()->setVisible(false);
  m_hexGrid->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_hexGrid->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_hexGrid->setSelectionBehavior(QAbstractItemView::SelectItems);
  m_hexGrid->setShowGrid(false);
  m_hexGrid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_hexGrid->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_hexGrid->setWordWrap(false);
  m_hexGrid->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  m_hexGrid->setColumnWidth(0, 160);
  for (int i = 1; i <= kBytesPerHexRow; ++i) {
    m_hexGrid->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Fixed);
    m_hexGrid->setColumnWidth(i, 36);
  }
  m_hexGrid->horizontalHeader()->setSectionResizeMode(17, QHeaderView::Stretch);

  layout->addWidget(m_hexGrid, 1);

  connect(m_hexGrid,
          &QTableWidget::currentCellChanged,
          this,
          [this](int currentRow, int currentColumn, int, int) {
            if (currentRow < 0 || currentColumn < 1 || currentColumn > kBytesPerHexRow) {
              return;
            }

            const auto offset = static_cast<std::uintptr_t>(currentRow)
                                    * static_cast<std::uintptr_t>(kBytesPerHexRow)
                                + static_cast<std::uintptr_t>(currentColumn - 1);
            m_currentAddress = m_viewBaseAddress + offset;

            if (m_disassemblyTable == nullptr) {
              return;
            }

            if (m_currentAddress < m_viewBaseAddress) {
              return;
            }

            const auto disassemblyOffset =
                static_cast<std::size_t>(m_currentAddress - m_viewBaseAddress);
            if (disassemblyOffset >= static_cast<std::size_t>(m_disassemblyTable->rowCount())) {
              return;
            }

            const int      targetRow = static_cast<int>(disassemblyOffset);
            QSignalBlocker blocker(m_disassemblyTable);
            m_disassemblyTable->setCurrentCell(targetRow, 0);
            if (QTableWidgetItem* item = m_disassemblyTable->item(targetRow, 0); item != nullptr) {
              m_disassemblyTable->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            }
          });

  return panel;
}

void MemoryViewerWindow::openGotoAddressDialog() {
  const QString defaultText =
      formatAddress(m_currentAddress == 0 ? kDefaultAddress : m_currentAddress);
  bool          accepted = false;
  const QString input    = QInputDialog::getText(this,
                                              ("Go To Address"),
                                              ("Hex address (e.g. 00400000 or 0x00400000):"),
                                              QLineEdit::Normal,
                                              defaultText,
                                              &accepted);

  if (!accepted) {
    return;
  }

  std::uintptr_t address = 0;
  if (!parseAddressText(input, address)) {
    QMessageBox::warning(this, ("Go To Address"), ("Invalid address."));
    return;
  }

  refreshViewAt(address);
}

void MemoryViewerWindow::refreshViewAt(std::uintptr_t address) {
  m_currentAddress  = address;
  m_viewBaseAddress = alignAddressForHex(address);
  refreshViews();
}

void MemoryViewerWindow::refreshViews() {
  if (m_memoryReader == nullptr || !m_memoryReader->attached()) {
    clearViewerData();
    updateProcessState();
    return;
  }

  if (!m_regions.empty()) {
    const bool inKnownRegion =
        std::any_of(m_regions.begin(), m_regions.end(), [this](const RegionEntry& region) {
          const auto endAddress = region.base + static_cast<std::uintptr_t>(region.size);
          return m_currentAddress >= region.base && m_currentAddress < endAddress;
        });
    if (!inKnownRegion) {
      m_currentAddress = m_regions.front().base;
    }
  }

  m_viewBaseAddress = alignAddressForHex(m_currentAddress);
  fillDisassemblyTable(m_viewBaseAddress);
  fillHexGrid(m_viewBaseAddress);
}

void MemoryViewerWindow::refreshRegionList() {
  m_regions.clear();

#ifdef Q_OS_WIN
  if (m_memoryReader == nullptr || !m_memoryReader->attached()) {
    return;
  }

  const HANDLE processHandle = m_memoryReader->process().nativeHandle();
  if (processHandle == nullptr) {
    return;
  }

  SYSTEM_INFO systemInfo{};
  ::GetSystemInfo(&systemInfo);

  std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
  const std::uintptr_t maxAddress =
      reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);

  while (cursor < maxAddress) {
    MEMORY_BASIC_INFORMATION mbi{};
    const SIZE_T             result =
        ::VirtualQueryEx(processHandle, reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi));

    if (result == 0) {
      cursor += 0x1000;
      continue;
    }

    RegionEntry entry{};
    entry.base       = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    entry.size       = static_cast<std::size_t>(mbi.RegionSize);
    entry.state      = static_cast<std::uint32_t>(mbi.State);
    entry.protection = static_cast<std::uint32_t>(mbi.Protect);
    entry.type       = static_cast<std::uint32_t>(mbi.Type);

    const std::uintptr_t nextCursor = entry.base + entry.size;
    if (nextCursor <= cursor) {
      break;
    }
    cursor = nextCursor;

    if (entry.state != MEM_COMMIT || entry.size == 0) {
      continue;
    }

    m_regions.push_back(entry);
  }
#endif
}

void MemoryViewerWindow::fillDisassemblyTable(std::uintptr_t address) {
  if (m_disassemblyTable == nullptr) {
    return;
  }

  const int rowCount = kDisassemblyRows;
  if (m_disassemblyTable->rowCount() != rowCount) {
    QSignalBlocker blocker(m_disassemblyTable);
    m_disassemblyTable->clearContents();
    m_disassemblyTable->setRowCount(rowCount);
  }

  std::vector<std::uint8_t> bytes;
  std::vector<std::uint8_t> valid;
  if (m_memoryReader != nullptr && m_memoryReader->attached()) {
    readMemoryChunked(*m_memoryReader, address, static_cast<std::size_t>(rowCount), bytes, valid);
  } else {
    bytes.assign(static_cast<std::size_t>(rowCount), 0);
    valid.assign(static_cast<std::size_t>(rowCount), 0);
  }

  const QBrush normalTextBrush(QColor(("#e8eaed")));

  m_disassemblyTable->setUpdatesEnabled(false);
  {
    QSignalBlocker blocker(m_disassemblyTable);
    for (int row = 0; row < rowCount; ++row) {
      const auto    rowAddress = address + static_cast<std::uintptr_t>(row);
      const bool    hasByte    = valid[static_cast<std::size_t>(row)] != 0;
      const QString byteText = hasByte ? formatByte(bytes[static_cast<std::size_t>(row)]) : ("??");
      const QString commentText =
          hasByte && std::isprint(static_cast<unsigned char>(bytes[static_cast<std::size_t>(row)]))
              ? QString(QChar(bytes[static_cast<std::size_t>(row)]))
              : ("?");

      auto* addressItem = ensureItem(m_disassemblyTable, row, 0);
      auto* bytesItem   = ensureItem(m_disassemblyTable, row, 1);
      auto* opcodeItem  = ensureItem(m_disassemblyTable, row, 2);
      auto* commentItem = ensureItem(m_disassemblyTable, row, 3);

      if (addressItem != nullptr) {
        addressItem->setText(formatAddress(rowAddress));
        addressItem->setForeground(normalTextBrush);
      }
      if (bytesItem != nullptr) {
        bytesItem->setText(byteText);
        bytesItem->setForeground(normalTextBrush);
      }
      if (opcodeItem != nullptr) {
        opcodeItem->setText(QString(("db ")) + byteText);
        opcodeItem->setForeground(normalTextBrush);
      }
      if (commentItem != nullptr) {
        commentItem->setText(commentText);
        commentItem->setForeground(normalTextBrush);
      }
    }

    int selectedRow = 0;
    if (m_currentAddress >= address) {
      const auto offset = static_cast<std::size_t>(m_currentAddress - address);
      if (offset < static_cast<std::size_t>(rowCount)) {
        selectedRow = static_cast<int>(offset);
      }
    }

    m_disassemblyTable->setCurrentCell(selectedRow, 0);
  }

  if (QTableWidgetItem* item = m_disassemblyTable->item(m_disassemblyTable->currentRow(), 0);
      item != nullptr) {
    m_disassemblyTable->scrollToItem(item, QAbstractItemView::PositionAtCenter);
  }
  m_disassemblyTable->setUpdatesEnabled(true);
}

void MemoryViewerWindow::fillHexGrid(std::uintptr_t address) {
  if (m_hexGrid == nullptr) {
    return;
  }

  const int rowCount = kHexRows;
  if (m_hexGrid->rowCount() != rowCount) {
    QSignalBlocker blocker(m_hexGrid);
    m_hexGrid->clearContents();
    m_hexGrid->setRowCount(rowCount);
  }

  const std::uintptr_t baseAddress = alignAddressForHex(address);
  const std::size_t    totalBytes =
      static_cast<std::size_t>(rowCount) * static_cast<std::size_t>(kBytesPerHexRow);

  std::vector<std::uint8_t> bytes;
  std::vector<std::uint8_t> valid;
  if (m_memoryReader != nullptr && m_memoryReader->attached()) {
    readMemoryChunked(*m_memoryReader, baseAddress, totalBytes, bytes, valid);
  } else {
    bytes.assign(totalBytes, 0);
    valid.assign(totalBytes, 0);
  }

  ++m_hexFlashGeneration;
  const int                        generation = m_hexFlashGeneration;
  std::vector<std::pair<int, int>> changedCells;
  changedCells.reserve(totalBytes / 8);

  const bool hasPrevious = m_previousHexBase == baseAddress
                           && m_previousHexBytes.size() == totalBytes
                           && m_previousHexValid.size() == totalBytes;

  const QBrush normalTextBrush(QColor(("#e8eaed")));
  const QBrush changedTextBrush(QColor(("#69de6f")));

  m_hexGrid->setUpdatesEnabled(false);
  {
    QSignalBlocker blocker(m_hexGrid);
    for (int row = 0; row < rowCount; ++row) {
      const std::size_t rowOffset =
          static_cast<std::size_t>(row) * static_cast<std::size_t>(kBytesPerHexRow);
      const auto rowAddress = baseAddress + static_cast<std::uintptr_t>(rowOffset);

      auto* addressItem = ensureItem(m_hexGrid, row, 0);
      if (addressItem != nullptr) {
        addressItem->setText(formatAddress(rowAddress));
        addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsSelectable);
        addressItem->setForeground(normalTextBrush);
      }

      QString ascii;
      ascii.reserve(kBytesPerHexRow);

      for (int byteColumn = 0; byteColumn < kBytesPerHexRow; ++byteColumn) {
        const std::size_t index    = rowOffset + static_cast<std::size_t>(byteColumn);
        auto*             byteItem = ensureItem(m_hexGrid, row, byteColumn + 1);
        if (byteItem == nullptr) {
          continue;
        }

        const bool hasValue = valid[index] != 0;
        if (!hasValue) {
          byteItem->setText(("??"));
          byteItem->setForeground(normalTextBrush);
          ascii += '?';
          continue;
        }

        const auto byteValue = bytes[index];
        byteItem->setText(formatByte(byteValue));
        byteItem->setForeground(normalTextBrush);

        if (hasPrevious && m_previousHexValid[index] != 0
            && m_previousHexBytes[index] != byteValue) {
          byteItem->setForeground(changedTextBrush);
          changedCells.emplace_back(row, byteColumn + 1);
        }

        ascii += std::isprint(static_cast<unsigned char>(byteValue)) ? QChar(byteValue) : '.';
      }

      auto* asciiItem = ensureItem(m_hexGrid, row, 17);
      if (asciiItem != nullptr) {
        asciiItem->setText(ascii);
        asciiItem->setFlags(asciiItem->flags() & ~Qt::ItemIsSelectable);
        asciiItem->setForeground(normalTextBrush);
      }
    }

    std::size_t selectionOffset = 0;
    if (m_currentAddress >= baseAddress) {
      selectionOffset = static_cast<std::size_t>(m_currentAddress - baseAddress);
      if (selectionOffset >= totalBytes) {
        selectionOffset = 0;
      }
    }

    const int selectionRow =
        static_cast<int>(selectionOffset / static_cast<std::size_t>(kBytesPerHexRow));
    const int selectionColumn =
        1 + static_cast<int>(selectionOffset % static_cast<std::size_t>(kBytesPerHexRow));
    m_hexGrid->setCurrentCell(selectionRow, selectionColumn);
  }

  if (QTableWidgetItem* item = m_hexGrid->item(m_hexGrid->currentRow(), m_hexGrid->currentColumn());
      item != nullptr) {
    m_hexGrid->scrollToItem(item, QAbstractItemView::PositionAtCenter);
  }
  m_hexGrid->setUpdatesEnabled(true);

  m_previousHexBase  = baseAddress;
  m_previousHexBytes = std::move(bytes);
  m_previousHexValid = std::move(valid);

  if (!changedCells.empty()) {
    scheduleHexFlashReset(changedCells, generation);
  }
}

void MemoryViewerWindow::scheduleHexFlashReset(const std::vector<std::pair<int, int>>& changedCells,
                                               int                                     generation) {
  if (m_hexGrid == nullptr || changedCells.empty()) {
    return;
  }

  QTimer::singleShot(420, this, [this, changedCells, generation]() {
    if (m_hexGrid == nullptr || generation != m_hexFlashGeneration) {
      return;
    }

    const QBrush normalTextBrush(QColor(("#e8eaed")));
    for (const auto& cell : changedCells) {
      const int row    = cell.first;
      const int column = cell.second;
      if (row < 0 || column < 0 || row >= m_hexGrid->rowCount()
          || column >= m_hexGrid->columnCount()) {
        continue;
      }

      QTableWidgetItem* item = m_hexGrid->item(row, column);
      if (item != nullptr) {
        item->setForeground(normalTextBrush);
      }
    }
  });
}

void MemoryViewerWindow::clearViewerData() {
  if (m_disassemblyTable != nullptr) {
    QSignalBlocker blocker(m_disassemblyTable);
    m_disassemblyTable->clearContents();
    m_disassemblyTable->setRowCount(0);
  }

  if (m_hexGrid != nullptr) {
    QSignalBlocker blocker(m_hexGrid);
    m_hexGrid->clearContents();
    m_hexGrid->setRowCount(0);
  }

  m_previousHexBase = 0;
  m_previousHexBytes.clear();
  m_previousHexValid.clear();
  ++m_hexFlashGeneration;
}

bool MemoryViewerWindow::parseAddressText(const QString& text, std::uintptr_t& address) {
  QString value = text.trimmed();
  if (value.isEmpty()) {
    return false;
  }

  if (value.startsWith(("0x"), Qt::CaseInsensitive)) {
    value = value.mid(2);
  }

  bool       ok     = false;
  const auto parsed = value.toULongLong(&ok, 16);
  if (!ok) {
    return false;
  }

  address = static_cast<std::uintptr_t>(parsed);
  return true;
}

void MemoryViewerWindow::updateProcessState() {
  if (m_processId != 0 && !m_processName.isEmpty()) {
    setWindowTitle(QString(("Memory Viewer - %1")).arg(m_processName));
    return;
  }

  setWindowTitle(("Memory Viewer"));
}

QString MemoryViewerWindow::protectionCategory(std::uint32_t protection) const {
#ifdef Q_OS_WIN
  if ((protection & PAGE_GUARD) != 0) {
    return ("Guard");
  }
  if ((protection & PAGE_NOACCESS) != 0) {
    return ("No Access");
  }

  const std::uint32_t base = protection & 0xFF;
  switch (base) {
    case PAGE_READONLY:
      return ("Read");
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
      return ("Read/Write");
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
      return ("Execute+Read");
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
      return ("Execute+Read/Write");
    default:
      break;
  }
#endif
  return ("Other");
}

}  // namespace farcal::ui
