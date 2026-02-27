#include "farcal/ui/StructureDissectorWindow.hpp"

#include "farcal/ui/Logger.hpp"
#include "q_lit.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace farcal::ui {

namespace {

constexpr int            kMaxRows             = 4096;
constexpr int            kRowChunkSize        = 64;
constexpr int            kUIUpdateChunkSize   = 32;
constexpr std::uintptr_t kDefaultStartAddress = 0x00400000;

struct DecodedValue {
  QString type    = ("Unknown");
  QString display = ("N/A");
};

bool isLikelyFloatValue(float value) {
  if (!std::isfinite(value)) {
    return false;
  }
  const float absValue = std::fabs(value);
  if (absValue == 0.0f) {
    return true;
  }
  return absValue >= 1.0e-6f && absValue <= 1.0e9f;
}

bool isLikelyDoubleValue(double value) {
  if (!std::isfinite(value)) {
    return false;
  }
  const double absValue = std::fabs(value);
  if (absValue == 0.0) {
    return true;
  }
  return absValue >= 1.0e-9 && absValue <= 1.0e12;
}

DecodedValue decodeNonPointerValue(bool          hasByte,
                                   bool          hasDword,
                                   bool          hasQword,
                                   std::uint8_t  byteValue,
                                   std::uint32_t dwordValue,
                                   std::uint64_t qwordValue) {
  if (hasQword && qwordValue != 0) {
    double asDouble = 0.0;
    std::memcpy(&asDouble, &qwordValue, sizeof(asDouble));
    if (isLikelyDoubleValue(asDouble)) {
      return {("Double"), QString::number(asDouble, 'g', 15)};
    }

    const auto signedValue = static_cast<qlonglong>(static_cast<std::int64_t>(qwordValue));
    return {
        ("Long long"),
        QString(("%1 (u:%2)")).arg(signedValue).arg(static_cast<qulonglong>(qwordValue)),
    };
  }

  if (hasDword) {
    float asFloat = 0.0f;
    std::memcpy(&asFloat, &dwordValue, sizeof(asFloat));
    if (isLikelyFloatValue(asFloat)) {
      return {("Float"), QString::number(asFloat, 'g', 7)};
    }

    const std::uint16_t upper16 = static_cast<std::uint16_t>((dwordValue >> 16) & 0xFFFFu);
    if (upper16 == 0u || upper16 == 0xFFFFu) {
      const auto asShort = static_cast<qint16>(static_cast<std::uint16_t>(dwordValue & 0xFFFFu));
      return {("Short"), QString::number(asShort)};
    }

    const auto signedValue = static_cast<qint32>(static_cast<std::int32_t>(dwordValue));
    return {
        ("Int"),
        QString(("%1 (u:%2)")).arg(signedValue).arg(static_cast<qulonglong>(dwordValue)),
    };
  }

  if (hasByte) {
    if (byteValue == 0 || byteValue == 1) {
      return {("Bool"), byteValue == 0 ? ("false") : ("true")};
    }

    if (byteValue >= 32 && byteValue <= 126) {
      return {("Char"), QString(("'%1' (%2)")).arg(QChar(byteValue)).arg(byteValue)};
    }

    return {("Byte"), QString::number(byteValue)};
  }

  return {};
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

#ifdef Q_OS_WIN
bool isCandidatePointer(std::uintptr_t value,
                        std::uintptr_t minAddress,
                        std::uintptr_t maxAddress) {
  if (value < minAddress || value >= maxAddress) {
    return false;
  }
  if ((value % alignof(std::uintptr_t)) != 0) {
    return false;
  }
  return value >= 0x10000;
}

bool isValidRttiName(const std::string& value) {
  if (value.empty() || value.size() > 512) {
    return false;
  }
  // Filter out obvious garbage
  if (value.rfind(("0x"), 0) == 0 || value.rfind(("0X"), 0) == 0) {
    return false;
  }
  // Filter out internal type_info names
  if (value == ("type_info") || value == ("std::type_info")) {
    return false;
  }
  // Must have at least one letter
  for (char ch : value) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
      return true;
    }
  }
  return false;
}

QString resolvePointerRtti(const memory::RttiScanner&                   scanner,
                           const memory::MemoryReader&                  reader,
                           std::uintptr_t                               candidate,
                           std::uintptr_t                               minAddress,
                           std::uintptr_t                               maxAddress,
                           std::unordered_map<std::uintptr_t, QString>& rttiCache) {
  auto isLikelyAddress = [&](std::uintptr_t address) -> bool {
    if (address < minAddress || address >= maxAddress) {
      return false;
    }
    if ((address % alignof(std::uintptr_t)) != 0) {
      return false;
    }
    return address >= 0x10000;
  };

  const auto lookupSingle = [&](std::uintptr_t address) -> QString {
    if (!isLikelyAddress(address)) {
      return {};
    }

    const auto cacheIt = rttiCache.find(address);
    if (cacheIt != rttiCache.end()) {
      return cacheIt->second;
    }

    QString    resolvedRtti;
    const auto rttiName = scanner.get_rtti_of_address(address, true);
    if (rttiName.has_value() && isValidRttiName(*rttiName)) {
      resolvedRtti = QString::fromStdString(*rttiName);
    }

    rttiCache.emplace(address, resolvedRtti);
    return resolvedRtti;
  };

  const auto tryObjectBaseGuess = [&](std::uintptr_t address) -> QString {
    if (!isLikelyAddress(address)) {
      return {};
    }

    constexpr std::uintptr_t kMaxBacktrack = 0x40;
    constexpr std::uintptr_t kStep         = sizeof(std::uintptr_t);
    for (std::uintptr_t offset = 0; offset <= kMaxBacktrack; offset += kStep) {
      if (address < offset) {
        break;
      }

      const std::uintptr_t objectBase = address - offset;
      if (const QString guessed = lookupSingle(objectBase); !guessed.isEmpty()) {
        return guessed;
      }
    }

    return {};
  };

  if (const QString direct = lookupSingle(candidate); !direct.isEmpty()) {
    return direct;
  }
  if (const QString objectBaseGuess = tryObjectBaseGuess(candidate); !objectBaseGuess.isEmpty()) {
    return objectBaseGuess;
  }

  const auto level1 = reader.read<std::uintptr_t>(candidate);
  if (!level1.has_value() || *level1 == 0 || *level1 == candidate || !isLikelyAddress(*level1)) {
    return {};
  }

  if (const QString firstIndirect = lookupSingle(*level1); !firstIndirect.isEmpty()) {
    return firstIndirect;
  }
  if (const QString firstIndirectObjectBase = tryObjectBaseGuess(*level1);
      !firstIndirectObjectBase.isEmpty()) {
    return firstIndirectObjectBase;
  }

  const auto level2 = reader.read<std::uintptr_t>(*level1);
  if (!level2.has_value() || *level2 == 0 || *level2 == *level1 || *level2 == candidate
      || !isLikelyAddress(*level2)) {
    return {};
  }

  if (const QString secondIndirect = lookupSingle(*level2); !secondIndirect.isEmpty()) {
    return secondIndirect;
  }
  return tryObjectBaseGuess(*level2);
}
#endif

}  // namespace

StructureDissectorWindow::StructureDissectorWindow(QWidget* parent)
  : QMainWindow(parent),
    m_memoryReader(std::make_unique<memory::MemoryReader>()),
    m_rttiScanner(std::make_unique<memory::RttiScanner>(m_memoryReader.get())) {
  applyTheme();
  configureWindow();
  updateWindowState();
}

StructureDissectorWindow::~StructureDissectorWindow() {
  m_shouldStop.store(true, std::memory_order_release);

  if (m_fillThread != nullptr) {
    if (!m_fillThread->wait(3000)) {
      m_fillThread->terminate();
      m_fillThread->wait(1000);
    }
    delete m_fillThread;
    m_fillThread = nullptr;
  }
}

void StructureDissectorWindow::setAttachedProcess(std::uint32_t  processId,
                                                  const QString& processName) {
  m_shouldStop.store(true, std::memory_order_release);

  if (m_fillThread != nullptr) {
    if (!m_fillThread->wait(2000)) {
      m_fillThread->terminate();
      m_fillThread->wait(500);
    }
    delete m_fillThread;
    m_fillThread = nullptr;
  }

  m_fillInProgress = false;
  m_refillPending  = false;
  m_shouldStop.store(false, std::memory_order_release);

  m_processId   = processId;
  m_processName = processName;
  ++m_fillGeneration;

  if (m_memoryReader == nullptr || processId == 0 || processName.isEmpty()) {
    if (m_memoryReader != nullptr) {
      m_memoryReader->detach();
    }
    if (m_tree != nullptr) {
      m_tree->clear();
    }
    updateWindowState();
    return;
  }

  const bool attached = m_memoryReader->attach(static_cast<memory::Process::Id>(processId));
  if (!attached) {
    m_processId = 0;
    m_processName.clear();
    m_memoryReader->detach();
    if (m_rttiScanner) {
      m_rttiScanner->setReader(nullptr);
    }
    if (m_tree != nullptr) {
      m_tree->clear();
    }
    updateWindowState();
    QMessageBox::warning(
        this, ("Structure Dissector"), ("Failed to attach to the selected process."));
    return;
  }

  if (m_rttiScanner) {
    m_rttiScanner->setReader(m_memoryReader.get());
  }

  if (m_startAddressInput != nullptr && m_startAddressInput->text().trimmed().isEmpty()) {
    m_startAddressInput->setText(formatAddress(kDefaultStartAddress));
  }

  updateWindowState();
}

void StructureDissectorWindow::focusAddress(std::uintptr_t address) {
  if (address == 0 || m_startAddressInput == nullptr) {
    return;
  }

  m_startAddressInput->setText(formatAddress(address));
  if (m_memoryReader != nullptr && m_memoryReader->attached()) {
    refreshFromInput();
  }
}

void StructureDissectorWindow::applyTheme() {
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
QLineEdit {
  background-color: #1b1d22;
  border: 1px solid #4a4e58;
  border-radius: 3px;
  color: #e9ecf1;
  padding: 4px;
  selection-background-color: #4e5f82;
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
}
QTableWidget::item:selected {
  background-color: #3c404b;
  color: #ffffff;
})"));
}

void StructureDissectorWindow::configureWindow() {
  resize(980, 760);
  createMenuBar();
  setCentralWidget(buildCentralArea());
}

void StructureDissectorWindow::createMenuBar() {
  auto* menuBar  = new QMenuBar(this);
  auto* viewMenu = menuBar->addMenu(("View"));

  auto* rebaseAction = viewMenu->addAction(("Rebase Addresses..."));
  connect(rebaseAction, &QAction::triggered, this, &StructureDissectorWindow::showRebaseDialog);

  setMenuBar(menuBar);
}

QWidget* StructureDissectorWindow::buildCentralArea() {
  auto* root       = new QWidget(this);
  auto* rootLayout = new QVBoxLayout(root);
  rootLayout->setContentsMargins(10, 10, 10, 10);
  rootLayout->setSpacing(8);

  auto* panel = new QFrame(root);
  panel->setObjectName(("panel"));

  auto* panelLayout = new QVBoxLayout(panel);
  panelLayout->setContentsMargins(10, 10, 10, 10);
  panelLayout->setSpacing(8);

  auto* topRow = new QHBoxLayout();
  topRow->addWidget(new QLabel(("Start Address:"), panel));

  m_startAddressInput = new QLineEdit(panel);
  m_startAddressInput->setPlaceholderText(("0x00400000"));
  m_startAddressInput->setText(formatAddress(kDefaultStartAddress));
  topRow->addWidget(m_startAddressInput, 1);

  m_refreshButton = new QPushButton(("Refresh"), panel);
  topRow->addWidget(m_refreshButton);
  panelLayout->addLayout(topRow);

  m_statusLabel = new QLabel(panel);
  panelLayout->addWidget(m_statusLabel);

  m_tree = new QTreeWidget(panel);
  m_tree->setColumnCount(8);
  m_tree->setHeaderLabels(
      {("Address"), ("RTTI"), ("Offset"), ("Type"), ("Byte"), ("Dword"), ("Qword"), ("Value")});
  m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
  m_tree->setAlternatingRowColors(false);
  auto* header = m_tree->header();
  header->setStretchLastSection(false);
  header->setSectionResizeMode(0, QHeaderView::Interactive);
  header->setSectionResizeMode(1, QHeaderView::Stretch);
  header->setSectionResizeMode(2, QHeaderView::Interactive);
  header->setSectionResizeMode(3, QHeaderView::Interactive);
  header->setSectionResizeMode(4, QHeaderView::Interactive);
  header->setSectionResizeMode(5, QHeaderView::Interactive);
  header->setSectionResizeMode(6, QHeaderView::Interactive);
  header->setSectionResizeMode(7, QHeaderView::Stretch);
  m_tree->setColumnWidth(0, 210);
  m_tree->setColumnWidth(2, 90);
  m_tree->setColumnWidth(3, 95);
  m_tree->setColumnWidth(4, 85);
  m_tree->setColumnWidth(5, 120);
  m_tree->setColumnWidth(6, 200);
  m_tree->setColumnWidth(7, 260);
  m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_tree, &QTreeWidget::itemExpanded, this, &StructureDissectorWindow::onItemExpanded);
  connect(m_tree,
          &QWidget::customContextMenuRequested,
          this,
          &StructureDissectorWindow::onTreeContextMenu);
  panelLayout->addWidget(m_tree, 1);

  rootLayout->addWidget(panel, 1);

  connect(
      m_refreshButton, &QPushButton::clicked, this, &StructureDissectorWindow::refreshFromInput);
  connect(m_startAddressInput,
          &QLineEdit::returnPressed,
          this,
          &StructureDissectorWindow::refreshFromInput);

  return root;
}

void StructureDissectorWindow::refreshFromInput() {
  std::uintptr_t address = 0;
  if (!parseAddressText(m_startAddressInput == nullptr ? QString{} : m_startAddressInput->text(),
                        address)) {
    if (m_statusLabel != nullptr) {
      m_statusLabel->setText(("Invalid start address."));
    }
    return;
  }

  startFillAddressTable(address);
}

bool StructureDissectorWindow::parseAddressText(const QString&  text,
                                                std::uintptr_t& address) const {
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

void StructureDissectorWindow::startFillAddressTable(std::uintptr_t startAddress) {
  if (m_memoryReader == nullptr || !m_memoryReader->attached() || m_tree == nullptr
      || m_processId == 0) {
    if (m_tree != nullptr) {
      m_tree->clear();
    }
    if (m_statusLabel != nullptr) {
      m_statusLabel->setText(("No process attached."));
    }
    updateWindowState();
    return;
  }

  if (m_fillInProgress) {
    m_refillPending       = true;
    m_pendingStartAddress = startAddress;
    return;
  }

  m_fillInProgress      = true;
  m_refillPending       = false;
  m_pendingStartAddress = startAddress;
  m_shouldStop.store(false, std::memory_order_release);
  const std::uint64_t generation = ++m_fillGeneration;

  if (m_refreshButton != nullptr) {
    m_refreshButton->setEnabled(false);
  }
  if (m_statusLabel != nullptr) {
    m_statusLabel->setText(("Reading structure..."));
  }
  m_tree->clear();

  const std::uint32_t                processId = m_processId;
  QPointer<StructureDissectorWindow> self(this);

  QThread* thread = QThread::create([self, processId, startAddress, generation]() {
    if (!self || self->m_shouldStop.load(std::memory_order_acquire)) {
      return;
    }

    memory::MemoryReader reader;
    if (!reader.attach(static_cast<memory::Process::Id>(processId))) {
      LOG_ERROR(("Structure Dissector: Failed to attach reader to process"));
      if (self) {
        QMetaObject::invokeMethod(
            self,
            [self, generation]() {
              if (self) {
                self->onFillFinished(generation, ("Failed to attach reader."));
              }
            },
            Qt::QueuedConnection);
      }
      return;
    }

    LOG_INFO(QString(("Structure Dissector: Attached to process %1")).arg(processId));

    memory::RttiScanner scanner(&reader);
    LOG_INFO(("Structure Dissector: RttiScanner initialized"));
    std::unordered_map<std::uintptr_t, QString> rttiCache;
    rttiCache.reserve(1024);

#ifdef Q_OS_WIN
    SYSTEM_INFO systemInfo{};
    ::GetSystemInfo(&systemInfo);
    const std::uintptr_t minAddress =
        reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    const std::uintptr_t maxAddress =
        reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);
    if (startAddress < minAddress || startAddress >= maxAddress) {
      if (self) {
        QMetaObject::invokeMethod(
            self,
            [self, generation]() {
              if (self) {
                self->onFillFinished(generation,
                                     ("Start address is outside valid process address range."));
              }
            },
            Qt::QueuedConnection);
      }
      return;
    }
    const std::uintptr_t available = maxAddress - startAddress;
    const int totalRows = static_cast<int>((std::min)(std::uintptr_t{kMaxRows}, available));
#else
    const int totalRows = kMaxRows;
#endif

    if (totalRows <= 0) {
      if (self) {
        QMetaObject::invokeMethod(
            self,
            [self, generation]() {
              if (self) {
                self->onFillFinished(generation,
                                     ("No readable addresses available from this start address."));
              }
            },
            Qt::QueuedConnection);
      }
      return;
    }

    for (int baseRow = 0; baseRow < totalRows; baseRow += kRowChunkSize) {
      if (!self || self->m_shouldStop.load(std::memory_order_acquire)) {
        return;
      }

      const int            chunkRows    = (std::min)(kRowChunkSize, totalRows - baseRow);
      const std::uintptr_t chunkAddress = startAddress + static_cast<std::uintptr_t>(baseRow);
      const std::size_t    readSpan = static_cast<std::size_t>(chunkRows) + sizeof(std::uint64_t);

      std::vector<std::uint8_t> bytes;
      std::vector<std::uint8_t> valid;

      try {
        readMemoryChunked(reader, chunkAddress, readSpan, bytes, valid);
      } catch (...) {
        continue;
      }

      if (!self || self->m_shouldStop.load(std::memory_order_acquire)) {
        return;
      }

      auto batch = std::make_shared<std::vector<RowDisplay>>();
      batch->reserve(static_cast<std::size_t>(chunkRows));

      for (int localRow = 0; localRow < chunkRows; ++localRow) {
        const int            row     = baseRow + localRow;
        const std::uintptr_t address = startAddress + static_cast<std::uintptr_t>(row);
        const std::size_t    idx     = static_cast<std::size_t>(localRow);

        const bool hasByte  = idx < valid.size() && valid[idx] != 0;
        const bool hasDword = idx + sizeof(std::uint32_t) <= valid.size() && valid[idx] != 0
                              && valid[idx + 1] != 0 && valid[idx + 2] != 0 && valid[idx + 3] != 0;
        const bool hasQword = idx + sizeof(std::uint64_t) <= valid.size() && valid[idx] != 0
                              && valid[idx + 1] != 0 && valid[idx + 2] != 0 && valid[idx + 3] != 0
                              && valid[idx + 4] != 0 && valid[idx + 5] != 0 && valid[idx + 6] != 0
                              && valid[idx + 7] != 0;

        std::uint32_t dwordValue = 0;
        if (hasDword && idx + sizeof(dwordValue) <= bytes.size()) {
          std::memcpy(
              &dwordValue, bytes.data() + static_cast<std::ptrdiff_t>(idx), sizeof(dwordValue));
        }

        std::uint64_t qwordValue = 0;
        if (hasQword && idx + sizeof(qwordValue) <= bytes.size()) {
          std::memcpy(
              &qwordValue, bytes.data() + static_cast<std::ptrdiff_t>(idx), sizeof(qwordValue));
        }

        RowDisplay display{};
        display.row     = row;
        display.address = formatAddress(address);
        display.offset  = QString(("0x%1")).arg(row, 0, 16).toUpper();
        display.byteValue =
            hasByte ? QString(("0x%1")).arg(bytes[idx], 2, 16, QChar('0')).toUpper() : ("??");
        display.dwordValue = hasDword
                                 ? QString(("0x%1")).arg(dwordValue, 8, 16, QChar('0')).toUpper()
                                 : ("????????");
        display.qwordValue = hasQword
                                 ? QString(("0x%1"))
                                       .arg(static_cast<qulonglong>(qwordValue), 16, 16, QChar('0'))
                                       .toUpper()
                                 : ("????????????????");

        const std::uint8_t byteValue = hasByte ? bytes[idx] : 0;
        const DecodedValue decoded =
            decodeNonPointerValue(hasByte, hasDword, hasQword, byteValue, dwordValue, qwordValue);
        display.type         = decoded.type;
        display.valueDisplay = decoded.display;

        // Determine type and if it's a pointer
        if (hasQword && qwordValue != 0) {
#ifdef Q_OS_WIN
          const std::uintptr_t candidate = static_cast<std::uintptr_t>(qwordValue);
          if (isCandidatePointer(candidate, minAddress, maxAddress)) {
            display.type         = ("Pointer");
            display.isPointer    = true;
            display.valueDisplay = formatAddress(candidate);

            LOG_DEBUG(QString(("Attempting RTTI lookup for pointer 0x%1")).arg(candidate, 0, 16));
            display.rtti =
                resolvePointerRtti(scanner, reader, candidate, minAddress, maxAddress, rttiCache);
            if (!display.rtti.isEmpty()) {
              LOG_INFO(
                  QString(("RTTI found for 0x%1: %2")).arg(candidate, 0, 16).arg(display.rtti));
            } else {
              LOG_DEBUG(QString(("No RTTI found for 0x%1")).arg(candidate, 0, 16));
            }
          }
#endif
        }

        batch->push_back(std::move(display));
      }

      if (!self || self->m_shouldStop.load(std::memory_order_acquire)) {
        return;
      }

      const int processedRows = baseRow + chunkRows;

      if (self) {
        QMetaObject::invokeMethod(
            self,
            [self, generation, totalRows, processedRows, batch]() {
              if (self) {
                self->appendRowBatch(generation, totalRows, processedRows, batch);
              }
            },
            Qt::QueuedConnection);

        // Small delay every 4 chunks to let UI breathe
        const int chunkIndex = baseRow / kRowChunkSize;
        if (chunkIndex % 4 == 0) {
          QThread::msleep(5);
        }
      }
    }

    if (!self || self->m_shouldStop.load(std::memory_order_acquire)) {
      return;
    }

    const QString finalStatus =
        QString(("Showing %1 addresses from %2")).arg(totalRows).arg(formatAddress(startAddress));
    if (self) {
      QMetaObject::invokeMethod(
          self,
          [self, generation, finalStatus]() {
            if (self) {
              self->onFillFinished(generation, finalStatus);
            }
          },
          Qt::QueuedConnection);
    }
  });

  m_fillThread = thread;
  connect(thread, &QThread::finished, this, [this, thread]() {
    if (m_fillThread != thread) {
      thread->deleteLater();
      return;
    }

    m_fillThread     = nullptr;
    m_fillInProgress = false;
    if (m_refreshButton != nullptr) {
      m_refreshButton->setEnabled(true);
    }

    const bool           shouldRefill  = m_refillPending;
    const std::uintptr_t refillAddress = m_pendingStartAddress;
    m_refillPending                    = false;
    thread->deleteLater();

    if (shouldRefill && !m_shouldStop.load(std::memory_order_acquire)) {
      startFillAddressTable(refillAddress);
    }
  });
  thread->start();
}

void StructureDissectorWindow::appendRowBatch(
    std::uint64_t                                   generation,
    int                                             totalRows,
    int                                             processedRows,
    const std::shared_ptr<std::vector<RowDisplay>>& batch) {
  if (generation != m_fillGeneration || m_tree == nullptr || batch == nullptr || batch->empty()) {
    return;
  }

  for (const auto& row : *batch) {
    if (row.row < 0 || row.row >= totalRows) {
      continue;
    }

    auto* item = new QTreeWidgetItem();
    item->setText(0, row.address);
    item->setText(1, row.rtti);
    item->setText(2, row.offset);
    item->setText(3, row.type);
    item->setText(4, row.byteValue);
    item->setText(5, row.dwordValue);
    item->setText(6, row.qwordValue);
    item->setText(7, row.valueDisplay);

    // Only add expandable arrow if it's a pointer
    if (row.isPointer) {
      auto* dummyChild = new QTreeWidgetItem();
      dummyChild->setText(0, ("Loading..."));
      item->addChild(dummyChild);
    }

    m_tree->addTopLevelItem(item);
  }

  if (m_statusLabel != nullptr) {
    m_statusLabel->setText(
        QString(("Reading structure... %1/%2")).arg(processedRows).arg(totalRows));
  }
}

void StructureDissectorWindow::onFillFinished(std::uint64_t  generation,
                                              const QString& finalStatus) {
  if (generation != m_fillGeneration) {
    return;
  }
  if (m_statusLabel != nullptr) {
    m_statusLabel->setText(finalStatus);
  }
}

void StructureDissectorWindow::updateWindowState() {
  if (m_processId != 0 && !m_processName.isEmpty()) {
    setWindowTitle(QString(("Structure Dissector - %1")).arg(m_processName));
  } else {
    setWindowTitle(("Structure Dissector"));
  }

  if (m_statusLabel == nullptr) {
    return;
  }

  if (m_processId == 0 || m_processName.isEmpty() || m_memoryReader == nullptr
      || !m_memoryReader->attached()) {
    m_statusLabel->setText(("No process attached."));
  }
}

QString StructureDissectorWindow::formatAddress(std::uintptr_t address) {
  constexpr int kWidth = sizeof(std::uintptr_t) * 2;
  return QString(("0x%1")).arg(static_cast<qulonglong>(address), kWidth, 16, QChar('0')).toUpper();
}

void StructureDissectorWindow::showRebaseDialog() {
  auto* dialog = new QDialog(this);
  dialog->setWindowTitle(("Rebase Addresses"));
  dialog->setModal(true);

  auto* layout = new QVBoxLayout(dialog);

  auto* label = new QLabel(("Enter rebase offset (hex):"), dialog);
  layout->addWidget(label);

  auto* offsetInput = new QLineEdit(dialog);
  offsetInput->setPlaceholderText(("0x0 or -0x1000"));
  if (m_rebaseOffset != 0) {
    if (m_rebaseOffset > 0) {
      offsetInput->setText(
          QString(("0x%1")).arg(static_cast<qulonglong>(m_rebaseOffset), 0, 16).toUpper());
    } else {
      offsetInput->setText(
          QString(("-0x%1")).arg(static_cast<qulonglong>(-m_rebaseOffset), 0, 16).toUpper());
    }
  }
  layout->addWidget(offsetInput);

  auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  layout->addWidget(buttonBox);

  connect(buttonBox, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

  if (dialog->exec() == QDialog::Accepted) {
    QString text = offsetInput->text().trimmed();
    if (text.isEmpty()) {
      m_rebaseOffset = 0;
      applyRebase(0);
      delete dialog;
      return;
    }

    bool negative = text.startsWith('-');
    if (negative) {
      text = text.mid(1);
    }

    if (text.startsWith(("0x"), Qt::CaseInsensitive)) {
      text = text.mid(2);
    }

    bool       ok    = false;
    qulonglong value = text.toULongLong(&ok, 16);
    if (ok) {
      std::intptr_t offset =
          negative ? -static_cast<std::intptr_t>(value) : static_cast<std::intptr_t>(value);
      applyRebase(offset);
    } else {
      QMessageBox::warning(this, ("Invalid Offset"), ("Please enter a valid hexadecimal offset."));
    }
  }

  delete dialog;
}

void StructureDissectorWindow::applyRebase(std::intptr_t offset) {
  m_rebaseOffset = offset;

  if (m_tree == nullptr) {
    return;
  }

  m_tree->setUpdatesEnabled(false);

  for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
    auto* item = m_tree->topLevelItem(i);
    if (item == nullptr) {
      continue;
    }

    QString addressText = item->text(0);
    if (addressText.startsWith(("0x"), Qt::CaseInsensitive)) {
      addressText = addressText.mid(2);
    }

    bool       ok              = false;
    qulonglong originalAddress = addressText.toULongLong(&ok, 16);
    if (ok) {
      std::intptr_t rebasedAddress = static_cast<std::intptr_t>(originalAddress) + offset;
      if (rebasedAddress >= 0) {
        item->setText(0, formatAddress(static_cast<std::uintptr_t>(rebasedAddress)));
      }
    }
  }

  m_tree->setUpdatesEnabled(true);

  if (m_statusLabel != nullptr) {
    if (offset == 0) {
      m_statusLabel->setText(("Rebase cleared."));
    } else if (offset > 0) {
      m_statusLabel->setText(
          QString(("Rebased by +0x%1")).arg(static_cast<qulonglong>(offset), 0, 16).toUpper());
    } else {
      m_statusLabel->setText(
          QString(("Rebased by -0x%1")).arg(static_cast<qulonglong>(-offset), 0, 16).toUpper());
    }
  }
}

void StructureDissectorWindow::onItemExpanded(QTreeWidgetItem* item) {
  if (item == nullptr || item->childCount() == 0) {
    return;
  }

  // Check if this is the dummy "Loading..." child
  auto* firstChild = item->child(0);
  if (firstChild != nullptr && firstChild->text(0) == ("Loading...")) {
    loadChildrenForItem(item);
  }
}

void StructureDissectorWindow::loadChildrenForItem(QTreeWidgetItem* item, int childCount) {
  if (item == nullptr || m_memoryReader == nullptr || !m_memoryReader->attached()) {
    return;
  }

  // Remove the dummy child
  while (item->childCount() > 0) {
    delete item->takeChild(0);
  }

  // Parse the qword value as a pointer (column 6)
  QString qwordText = item->text(6);
  if (qwordText == ("????????????????") || qwordText.isEmpty()) {
    return;
  }

  if (qwordText.startsWith(("0x"), Qt::CaseInsensitive)) {
    qwordText = qwordText.mid(2);
  }

  bool       ok             = false;
  qulonglong pointerAddress = qwordText.toULongLong(&ok, 16);
  if (!ok || pointerAddress == 0) {
    return;
  }

#ifdef Q_OS_WIN
  SYSTEM_INFO systemInfo{};
  ::GetSystemInfo(&systemInfo);
  const std::uintptr_t minAddress =
      reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
  const std::uintptr_t maxAddress =
      reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);
#endif

  // Read bytes from the pointer address
  const int                 kChildRows = childCount;
  std::vector<std::uint8_t> bytes;
  std::vector<std::uint8_t> valid;

  try {
    readMemoryChunked(
        *m_memoryReader, static_cast<std::uintptr_t>(pointerAddress), kChildRows + 8, bytes, valid);
  } catch (...) {
    auto* errorChild = new QTreeWidgetItem();
    errorChild->setText(0, ("Failed to read memory"));
    item->addChild(errorChild);
    return;
  }

  // Create child items for each byte
  std::unordered_map<std::uintptr_t, QString> rttiCache;
  rttiCache.reserve(static_cast<std::size_t>((std::min)(kChildRows, 512)));
  for (int i = 0; i < kChildRows; ++i) {
    const std::uintptr_t address = static_cast<std::uintptr_t>(pointerAddress) + i;
    const std::size_t    idx     = static_cast<std::size_t>(i);

    const bool hasByte  = idx < valid.size() && valid[idx] != 0;
    const bool hasDword = idx + sizeof(std::uint32_t) <= valid.size() && valid[idx] != 0
                          && valid[idx + 1] != 0 && valid[idx + 2] != 0 && valid[idx + 3] != 0;
    const bool hasQword = idx + sizeof(std::uint64_t) <= valid.size() && valid[idx] != 0
                          && valid[idx + 1] != 0 && valid[idx + 2] != 0 && valid[idx + 3] != 0
                          && valid[idx + 4] != 0 && valid[idx + 5] != 0 && valid[idx + 6] != 0
                          && valid[idx + 7] != 0;

    std::uint32_t dwordValue = 0;
    if (hasDword && idx + sizeof(dwordValue) <= bytes.size()) {
      std::memcpy(&dwordValue, bytes.data() + static_cast<std::ptrdiff_t>(idx), sizeof(dwordValue));
    }

    std::uint64_t qwordValue = 0;
    if (hasQword && idx + sizeof(qwordValue) <= bytes.size()) {
      std::memcpy(&qwordValue, bytes.data() + static_cast<std::ptrdiff_t>(idx), sizeof(qwordValue));
    }

    // Determine type and if it's a pointer
    const std::uint8_t byteValue = hasByte ? bytes[idx] : 0;
    const DecodedValue decoded =
        decodeNonPointerValue(hasByte, hasDword, hasQword, byteValue, dwordValue, qwordValue);

    QString type         = decoded.type;
    QString valueDisplay = decoded.display;
    QString rtti;
    bool    isPointer = false;
    if (hasQword && qwordValue != 0) {
#ifdef Q_OS_WIN
      const std::uintptr_t candidate = static_cast<std::uintptr_t>(qwordValue);
      if (isCandidatePointer(candidate, minAddress, maxAddress)) {
        type         = ("Pointer");
        valueDisplay = formatAddress(candidate);
        isPointer    = true;

        LOG_DEBUG(QString(("Child pointer detected at 0x%1, value: 0x%2"))
                      .arg(address, 0, 16)
                      .arg(qwordValue, 0, 16));

        // Try to get RTTI for this pointer
        if (m_rttiScanner) {
          rtti = resolvePointerRtti(
              *m_rttiScanner, *m_memoryReader, candidate, minAddress, maxAddress, rttiCache);
          if (!rtti.isEmpty()) {
            LOG_INFO(QString(("Child RTTI found for 0x%1: %2")).arg(qwordValue, 0, 16).arg(rtti));
          } else {
            LOG_DEBUG(QString(("No child RTTI for 0x%1")).arg(qwordValue, 0, 16));
          }
        }
      }
#endif
    }

    auto* childItem = new QTreeWidgetItem();
    childItem->setText(0, formatAddress(address));
    childItem->setText(1, rtti);
    childItem->setText(2, QString(("0x%1")).arg(i, 0, 16).toUpper());
    childItem->setText(3, type);
    childItem->setText(
        4, hasByte ? QString(("0x%1")).arg(bytes[idx], 2, 16, QChar('0')).toUpper() : ("??"));
    childItem->setText(
        5,
        hasDword ? QString(("0x%1")).arg(dwordValue, 8, 16, QChar('0')).toUpper() : ("????????"));
    childItem->setText(6,
                       hasQword ? QString(("0x%1"))
                                      .arg(static_cast<qulonglong>(qwordValue), 16, 16, QChar('0'))
                                      .toUpper()
                                : ("????????????????"));
    childItem->setText(7, valueDisplay);

    // Only add expandable arrow if this child is a pointer
    if (isPointer) {
      auto* dummyGrandchild = new QTreeWidgetItem();
      dummyGrandchild->setText(0, ("Loading..."));
      childItem->addChild(dummyGrandchild);
    }

    item->addChild(childItem);
  }
}

bool StructureDissectorWindow::writeValueToItem(QTreeWidgetItem* item,
                                                const QString&   mode,
                                                const QString&   inputText) {
  if (item == nullptr || m_memoryReader == nullptr || !m_memoryReader->attached()) {
    return false;
  }

  std::uintptr_t address = 0;
  if (!parseAddressText(item->text(0), address) || address == 0) {
    return false;
  }

  const QString trimmed = inputText.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  QString effectiveMode = mode;
  if (effectiveMode == ("Auto")) {
    const QString type = item->text(3).trimmed();
    if (type == ("Pointer")) {
      effectiveMode = ("Pointer");
    } else if (type == ("Byte")) {
      effectiveMode = ("Byte");
    } else if (type == ("Bool")) {
      effectiveMode = ("Bool");
    } else if (type == ("Char")) {
      effectiveMode = ("Char");
    } else if (type == ("Short")) {
      effectiveMode = ("Short");
    } else if (type == ("Int")) {
      effectiveMode = ("Int");
    } else if (type == ("Long long")) {
      effectiveMode = ("LongLong");
    } else if (type == ("Float")) {
      effectiveMode = ("Float");
    } else if (type == ("Double")) {
      effectiveMode = ("Double");
    } else {
      effectiveMode = ("Qword");
    }
  }

  bool success = false;

  if (effectiveMode == ("String")) {
    const QByteArray bytes = trimmed.toLatin1();
    success                = !bytes.isEmpty()
              && m_memoryReader->writeBytes(
                  address, bytes.constData(), static_cast<std::size_t>(bytes.size()));
  } else if (effectiveMode == ("Float")) {
    bool        ok    = false;
    const float value = trimmed.toFloat(&ok);
    success           = ok && m_memoryReader->write<float>(address, value);
  } else if (effectiveMode == ("Double")) {
    bool         ok    = false;
    const double value = trimmed.toDouble(&ok);
    success            = ok && m_memoryReader->write<double>(address, value);
  } else if (effectiveMode == ("Bool")) {
    const QString lower = trimmed.toLower();
    if (lower == ("true") || lower == ("1")) {
      success = m_memoryReader->write<std::uint8_t>(address, 1);
    } else if (lower == ("false") || lower == ("0")) {
      success = m_memoryReader->write<std::uint8_t>(address, 0);
    } else {
      success = false;
    }
  } else if (effectiveMode == ("Char")) {
    std::uint8_t value = 0;
    if (trimmed.size() == 1) {
      value   = static_cast<std::uint8_t>(trimmed.at(0).toLatin1());
      success = m_memoryReader->write<std::uint8_t>(address, value);
    } else {
      bool             ok     = false;
      const qulonglong parsed = trimmed.toULongLong(&ok, 0);
      success                 = ok && parsed <= 0xFFull
                && m_memoryReader->write<std::uint8_t>(address, static_cast<std::uint8_t>(parsed));
    }
  } else if (effectiveMode == ("Byte")) {
    bool             ok    = false;
    const qulonglong value = trimmed.toULongLong(&ok, 0);
    success                = ok && value <= 0xFFull
              && m_memoryReader->write<std::uint8_t>(address, static_cast<std::uint8_t>(value));
  } else if (effectiveMode == ("Dword")) {
    bool             ok    = false;
    const qulonglong value = trimmed.toULongLong(&ok, 0);
    success                = ok && value <= 0xFFFFFFFFull
              && m_memoryReader->write<std::uint32_t>(address, static_cast<std::uint32_t>(value));
  } else if (effectiveMode == ("Qword") || effectiveMode == ("Pointer")) {
    bool             ok    = false;
    const qulonglong value = trimmed.toULongLong(&ok, 0);
    success =
        ok && m_memoryReader->write<std::uint64_t>(address, static_cast<std::uint64_t>(value));
  } else if (effectiveMode == ("Short")) {
    bool            ok    = false;
    const qlonglong value = trimmed.toLongLong(&ok, 0);
    success               = ok && value >= std::numeric_limits<qint16>::min()
              && value <= std::numeric_limits<qint16>::max()
              && m_memoryReader->write<std::int16_t>(address, static_cast<std::int16_t>(value));
  } else if (effectiveMode == ("Int")) {
    bool            ok    = false;
    const qlonglong value = trimmed.toLongLong(&ok, 0);
    success               = ok && value >= std::numeric_limits<qint32>::min()
              && value <= std::numeric_limits<qint32>::max()
              && m_memoryReader->write<std::int32_t>(address, static_cast<std::int32_t>(value));
  } else if (effectiveMode == ("LongLong")) {
    bool            ok    = false;
    const qlonglong value = trimmed.toLongLong(&ok, 0);
    success = ok && m_memoryReader->write<std::int64_t>(address, static_cast<std::int64_t>(value));
  }

  if (success) {
    refreshFromInput();
  }
  return success;
}

void StructureDissectorWindow::onTreeContextMenu(const QPoint& pos) {
  auto* item = m_tree->itemAt(pos);
  if (item == nullptr) {
    return;
  }

  QMenu menu(this);
  auto* setValueMenu    = menu.addMenu(("Set Value"));
  auto* setAutoAction   = setValueMenu->addAction(("Auto"));
  auto* setByteAction   = setValueMenu->addAction(("Byte"));
  auto* setDwordAction  = setValueMenu->addAction(("Dword"));
  auto* setQwordAction  = setValueMenu->addAction(("Qword"));
  auto* setFloatAction  = setValueMenu->addAction(("Float"));
  auto* setDoubleAction = setValueMenu->addAction(("Double"));
  auto* setStringAction = setValueMenu->addAction(("String"));

  QAction* add1024Action = nullptr;
  QAction* add2048Action = nullptr;
  QAction* add4096Action = nullptr;
  if (item->text(3) == ("Pointer")) {
    menu.addSeparator();
    add1024Action = menu.addAction(("Add 1024 bytes"));
    add2048Action = menu.addAction(("Add 2048 bytes"));
    add4096Action = menu.addAction(("Add 4096 bytes"));
  }

  QAction* chosenAction = menu.exec(m_tree->viewport()->mapToGlobal(pos));
  if (chosenAction == nullptr) {
    return;
  }

  if (chosenAction == setAutoAction || chosenAction == setByteAction
      || chosenAction == setDwordAction || chosenAction == setQwordAction
      || chosenAction == setFloatAction || chosenAction == setDoubleAction
      || chosenAction == setStringAction) {
    const QString mode = (chosenAction == setAutoAction)     ? ("Auto")
                         : (chosenAction == setByteAction)   ? ("Byte")
                         : (chosenAction == setDwordAction)  ? ("Dword")
                         : (chosenAction == setQwordAction)  ? ("Qword")
                         : (chosenAction == setFloatAction)  ? ("Float")
                         : (chosenAction == setDoubleAction) ? ("Double")
                                                             : ("String");

    bool          accepted     = false;
    const QString currentValue = item->text(7);
    const QString entered      = QInputDialog::getText(this,
                                                  ("Set Value"),
                                                  QString(("Enter %1 value:")).arg(mode),
                                                  QLineEdit::Normal,
                                                  currentValue,
                                                  &accepted);
    if (!accepted) {
      return;
    }

    if (!writeValueToItem(item, mode, entered)) {
      QMessageBox::warning(this, ("Set Value"), ("Failed to write value to selected address."));
    }
    return;
  }

  int bytesToAdd = 0;
  if (chosenAction == add1024Action) {
    bytesToAdd = 1024;
  } else if (chosenAction == add2048Action) {
    bytesToAdd = 2048;
  } else if (chosenAction == add4096Action) {
    bytesToAdd = 4096;
  }

  if (bytesToAdd > 0) {
    // Remove existing children
    while (item->childCount() > 0) {
      delete item->takeChild(0);
    }
    // Add dummy child to make it expandable again
    auto* dummyChild = new QTreeWidgetItem();
    dummyChild->setText(0, ("Loading..."));
    item->addChild(dummyChild);
    // Expand with new byte count
    loadChildrenForItem(item, bytesToAdd);
    item->setExpanded(true);
  }
}

}  // namespace farcal::ui
