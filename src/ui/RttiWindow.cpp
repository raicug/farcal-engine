#include "farcal/ui/RttiWindow.hpp"
#include "q_lit.hpp"

#include "farcal/memory/MemoryReader.hpp"

#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QPushButton>
#include <QStringList>
#include <QTableView>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <utility>

namespace farcal::ui {

namespace {

QString formatAddressInternal(std::uintptr_t address) {
  constexpr int kAddressWidth = sizeof(std::uintptr_t) * 2;
  return QString(("0x%1"))
      .arg(static_cast<qulonglong>(address), kAddressWidth, 16, QChar('0'))
      .toUpper();
}

QString displayDemangledName(const memory::RttiScanner::TypeInfo& entry) {
  if (!entry.demangled_name.empty()) {
    return QString::fromStdString(entry.demangled_name);
  }
  return ("<undemangled>");
}

QString formatVftables(const std::vector<std::uintptr_t>& vftables) {
  if (vftables.empty()) {
    return {};
  }

  QStringList       addresses;
  const std::size_t shown = (std::min)(vftables.size(), static_cast<std::size_t>(3));
  for (std::size_t i = 0; i < shown; ++i) {
    addresses.push_back(formatAddressInternal(vftables[i]));
  }

  QString text = addresses.join((", "));
  if (vftables.size() > shown) {
    text += QString((" (+%1)")).arg(vftables.size() - shown);
  }
  return text;
}

std::size_t countEntriesWithVftables(const std::vector<memory::RttiScanner::TypeInfo>& entries) {
  return static_cast<std::size_t>(
      std::count_if(entries.begin(), entries.end(), [](const memory::RttiScanner::TypeInfo& entry) {
        return !entry.vftables.empty();
      }));
}

void mergeScanResults(std::vector<memory::RttiScanner::TypeInfo>&  base,
                      std::vector<memory::RttiScanner::TypeInfo>&& extra) {
  if (extra.empty()) {
    return;
  }

  std::unordered_map<std::uintptr_t, std::size_t> typeToIndex;
  typeToIndex.reserve(base.size() + extra.size());
  for (std::size_t i = 0; i < base.size(); ++i) {
    typeToIndex.emplace(base[i].type_descriptor, i);
  }

  for (auto& incoming : extra) {
    const auto found = typeToIndex.find(incoming.type_descriptor);
    if (found == typeToIndex.end()) {
      typeToIndex.emplace(incoming.type_descriptor, base.size());
      base.push_back(std::move(incoming));
      continue;
    }

    auto& target = base[found->second];
    if (target.demangled_name.empty() && !incoming.demangled_name.empty()) {
      target.demangled_name = std::move(incoming.demangled_name);
    }

    for (const auto vftable : incoming.vftables) {
      if (std::find(target.vftables.begin(), target.vftables.end(), vftable)
          == target.vftables.end()) {
        target.vftables.push_back(vftable);
      }
    }
  }
}

}  // namespace

class RttiTableModel final : public QAbstractTableModel {
 public:
  explicit RttiTableModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

  void set_data_sources(const std::vector<memory::RttiScanner::TypeInfo>* entries,
                        const std::vector<int>*                           visible_rows) {
    beginResetModel();
    m_entries     = entries;
    m_visibleRows = visible_rows;
    endResetModel();
  }

  int rowCount(const QModelIndex& parent = QModelIndex()) const override {
    if (parent.isValid() || m_visibleRows == nullptr) {
      return 0;
    }
    return static_cast<int>(m_visibleRows->size());
  }

  int columnCount(const QModelIndex& parent = QModelIndex()) const override {
    if (parent.isValid()) {
      return 0;
    }
    return 3;
  }

  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
    if (!index.isValid() || role != Qt::DisplayRole || m_entries == nullptr
        || m_visibleRows == nullptr) {
      return {};
    }

    const int viewRow = index.row();
    if (viewRow < 0 || viewRow >= static_cast<int>(m_visibleRows->size())) {
      return {};
    }

    const int sourceRow = (*m_visibleRows)[static_cast<std::size_t>(viewRow)];
    if (sourceRow < 0 || sourceRow >= static_cast<int>(m_entries->size())) {
      return {};
    }

    const auto& entry = (*m_entries)[static_cast<std::size_t>(sourceRow)];
    switch (index.column()) {
      case 0:
        return displayDemangledName(entry);
      case 1:
        return formatAddressInternal(entry.type_descriptor);
      case 2:
        return formatVftables(entry.vftables);
      default:
        return {};
    }
  }

  QVariant headerData(int             section,
                      Qt::Orientation orientation,
                      int             role = Qt::DisplayRole) const override {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
      return QAbstractTableModel::headerData(section, orientation, role);
    }

    switch (section) {
      case 0:
        return ("RTTI Name (Demangled)");
      case 1:
        return ("Address");
      case 2:
        return ("VFTables");
      default:
        return {};
    }
  }

 private:
  const std::vector<memory::RttiScanner::TypeInfo>* m_entries     = nullptr;
  const std::vector<int>*                           m_visibleRows = nullptr;
};

RttiWindow::RttiWindow(QWidget* parent) : QMainWindow(parent) {
  applyTheme();
  configureWindow();
  updateWindowState();
}

RttiWindow::~RttiWindow() {
  if (m_scanThread != nullptr) {
    m_scanThread->wait();
    delete m_scanThread;
    m_scanThread = nullptr;
  }
}

void RttiWindow::setAttachedProcess(std::uint32_t processId, const QString& processName) {
  m_processId   = processId;
  m_processName = processName;

  if (m_processId == 0 || m_processName.isEmpty()) {
    m_entries.clear();
    m_filteredRows.clear();
    if (m_tableModel != nullptr) {
      m_tableModel->set_data_sources(&m_entries, &m_filteredRows);
    }
    updateWindowState();
    return;
  }

  refreshScan();
}

void RttiWindow::applyTheme() {
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
QTableView {
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
QTableView::item:selected {
  background-color: #3c404b;
  color: #ffffff;
})"));
}

void RttiWindow::configureWindow() {
  resize(1080, 760);
  setCentralWidget(buildCentralArea());
}

QWidget* RttiWindow::buildCentralArea() {
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
  topRow->addWidget(new QLabel(("Filter:"), panel));

  m_filterInput = new QLineEdit(panel);
  m_filterInput->setPlaceholderText(("Filter demangled RTTI name..."));
  topRow->addWidget(m_filterInput, 1);

  m_refreshButton = new QPushButton(("Refresh"), panel);
  topRow->addWidget(m_refreshButton);
  panelLayout->addLayout(topRow);

  m_statusLabel = new QLabel(panel);
  panelLayout->addWidget(m_statusLabel);

  m_table      = new QTableView(panel);
  m_tableModel = new RttiTableModel(m_table);
  m_tableModel->set_data_sources(&m_entries, &m_filteredRows);

  m_table->setModel(m_tableModel);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setSortingEnabled(false);
  m_table->verticalHeader()->setVisible(false);
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  m_table->setContextMenuPolicy(Qt::CustomContextMenu);
  panelLayout->addWidget(m_table, 1);

  rootLayout->addWidget(panel, 1);

  connect(m_filterInput, &QLineEdit::textChanged, this, &RttiWindow::applyFilter);
  connect(m_refreshButton, &QPushButton::clicked, this, &RttiWindow::refreshScan);
  connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    if (m_table == nullptr) {
      return;
    }

    const QModelIndex index = m_table->indexAt(pos);
    if (!index.isValid()) {
      return;
    }

    QMenu    menu(this);
    QAction* copyAction   = menu.addAction(("Copy"));
    QAction* chosenAction = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (chosenAction != copyAction) {
      return;
    }

    const QString value = index.data(Qt::DisplayRole).toString();
    if (!value.isEmpty()) {
      QApplication::clipboard()->setText(value);
    }
  });

  return root;
}

void RttiWindow::refreshScan() {
  if (m_processId == 0 || m_processName.isEmpty()) {
    m_entries.clear();
    applyFilter({});
    updateWindowState();
    return;
  }

  if (m_scanInProgress) {
    m_rescanPending = true;
    return;
  }

  m_scanInProgress               = true;
  m_rescanPending                = false;
  const std::uint64_t generation = ++m_scanGeneration;

  m_entries.clear();
  m_filteredRows.clear();
  if (m_tableModel != nullptr) {
    m_tableModel->set_data_sources(&m_entries, &m_filteredRows);
  }

  if (m_refreshButton != nullptr) {
    m_refreshButton->setEnabled(false);
  }
  if (m_statusLabel != nullptr) {
    m_statusLabel->setText(("Scanning RTTI..."));
  }

  const std::uint32_t processId = m_processId;
  QThread*            thread    = QThread::create([this, processId, generation]() {
    memory::MemoryReader reader;
    if (!reader.attach(static_cast<memory::Process::Id>(processId))) {
      return;
    }

    memory::RttiScanner              scanner(&reader);
    memory::RttiScanner::ScanOptions fast_options{};
    fast_options.max_results                   = 60000;
    fast_options.max_candidates                = 4 * 1024 * 1024;
    fast_options.pointer_stride                = sizeof(std::uintptr_t);
    fast_options.max_name_length               = 256;
    fast_options.max_vftables_per_type         = 8;
    fast_options.require_executable_first_slot = true;
    fast_options.include_writable_regions      = false;
    fast_options.demangle_names                = true;

    auto results = scanner.find_all(fast_options);

    const std::size_t withVftables = countEntriesWithVftables(results);
    const bool        sparseVftables = !results.empty() && (withVftables * 5 < results.size());
    if (results.empty() || sparseVftables) {
      memory::RttiScanner::ScanOptions fallback_options{};
      fallback_options.max_results                   = 60000;
      fallback_options.max_candidates                = 16 * 1024 * 1024;
      fallback_options.pointer_stride                = sizeof(std::uintptr_t);
      fallback_options.max_name_length               = 256;
      fallback_options.max_vftables_per_type         = 24;
      fallback_options.require_executable_first_slot = true;
      fallback_options.include_writable_regions      = true;
      fallback_options.demangle_names                = true;

      auto fallbackResults = scanner.find_all(fallback_options);
      if (results.empty()) {
        results = std::move(fallbackResults);
      } else {
        mergeScanResults(results, std::move(fallbackResults));
      }
    }

    constexpr std::size_t kBatchSize = 1500;
    for (std::size_t offset = 0; offset < results.size(); offset += kBatchSize) {
      const std::size_t count = (std::min)(kBatchSize, results.size() - offset);
      std::vector<memory::RttiScanner::TypeInfo> batch;
      batch.reserve(count);
      batch.insert(
          batch.end(),
          std::make_move_iterator(results.begin() + static_cast<std::ptrdiff_t>(offset)),
          std::make_move_iterator(results.begin() + static_cast<std::ptrdiff_t>(offset + count)));

      auto batchPtr =
          std::make_shared<std::vector<memory::RttiScanner::TypeInfo>>(std::move(batch));
      QMetaObject::invokeMethod(
          this,
          [this, generation, batchPtr]() mutable {
            appendScanBatch(generation, std::move(*batchPtr));
          },
          Qt::QueuedConnection);
    }
  });

  m_scanThread = thread;
  connect(thread, &QThread::finished, this, [this, thread, generation]() {
    if (m_scanThread != thread) {
      thread->deleteLater();
      return;
    }

    m_scanThread     = nullptr;
    m_scanInProgress = false;
    onScanFinished(generation);

    if (m_refreshButton != nullptr) {
      m_refreshButton->setEnabled(true);
    }

    const bool shouldRescan = m_rescanPending;
    m_rescanPending         = false;
    thread->deleteLater();

    if (shouldRescan) {
      refreshScan();
    }
  });

  thread->start();
}

void RttiWindow::appendScanBatch(std::uint64_t                                generation,
                                 std::vector<memory::RttiScanner::TypeInfo>&& batch) {
  if (generation != m_scanGeneration || batch.empty()) {
    return;
  }

  const QString activeFilter =
      m_filterInput == nullptr ? QString{} : m_filterInput->text().trimmed();
  const bool filterEmpty = activeFilter.isEmpty();

  const int start = static_cast<int>(m_entries.size());
  m_entries.insert(m_entries.end(),
                   std::make_move_iterator(batch.begin()),
                   std::make_move_iterator(batch.end()));

  if (filterEmpty) {
    m_filteredRows.reserve(m_entries.size());
    for (int row = start; row < static_cast<int>(m_entries.size()); ++row) {
      m_filteredRows.push_back(row);
    }
  } else {
    for (int row = start; row < static_cast<int>(m_entries.size()); ++row) {
      const auto&   entry     = m_entries[static_cast<std::size_t>(row)];
      const QString demangled = QString::fromStdString(entry.demangled_name);
      if (demangled.contains(activeFilter, Qt::CaseInsensitive)) {
        m_filteredRows.push_back(row);
      }
    }
  }

  if (m_tableModel != nullptr) {
    m_tableModel->set_data_sources(&m_entries, &m_filteredRows);
  }

  updateWindowState();
}

void RttiWindow::onScanFinished(std::uint64_t generation) {
  if (generation != m_scanGeneration) {
    return;
  }
  applyFilter(m_filterInput == nullptr ? QString{} : m_filterInput->text());
  updateWindowState();
}

void RttiWindow::applyFilter(const QString& query) {
  m_filteredRows.clear();
  m_filteredRows.reserve(m_entries.size());

  const QString normalized = query.trimmed();
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    const auto&   entry     = m_entries[i];
    const QString demangled = QString::fromStdString(entry.demangled_name);
    const bool    matches =
        normalized.isEmpty() || demangled.contains(normalized, Qt::CaseInsensitive);
    if (matches) {
      m_filteredRows.push_back(static_cast<int>(i));
    }
  }

  if (m_tableModel != nullptr) {
    m_tableModel->set_data_sources(&m_entries, &m_filteredRows);
  }

  updateWindowState();
}

void RttiWindow::updateWindowState() {
  if (m_processId != 0 && !m_processName.isEmpty()) {
    setWindowTitle(QString(("RTTI Scanner - %1")).arg(m_processName));
  } else {
    setWindowTitle(("RTTI Scanner"));
  }

  if (m_statusLabel == nullptr) {
    return;
  }

  if (m_processId == 0 || m_processName.isEmpty()) {
    m_statusLabel->setText(("No process attached."));
    return;
  }

  if (m_scanInProgress) {
    m_statusLabel->setText(
        QString(("Scanning RTTI... Attached: %1 (PID %2)")).arg(m_processName).arg(m_processId));
    return;
  }

  m_statusLabel->setText(QString(("Attached: %1 (PID %2)  |  RTTI entries: %3  |  Visible: %4"))
                             .arg(m_processName)
                             .arg(m_processId)
                             .arg(m_entries.size())
                             .arg(m_filteredRows.size()));
}

QString RttiWindow::formatAddress(std::uintptr_t address) {
  return formatAddressInternal(address);
}

}  // namespace farcal::ui
