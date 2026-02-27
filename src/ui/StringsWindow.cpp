#include "farcal/ui/StringsWindow.hpp"
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
#include <QTableView>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>

namespace farcal::ui {

namespace {

QString formatAddressInternal(std::uintptr_t address) {
  constexpr int kAddressWidth = sizeof(std::uintptr_t) * 2;
  return QString(("0x%1"))
      .arg(static_cast<qulonglong>(address), kAddressWidth, 16, QChar('0'))
      .toUpper();
}

bool stringMatchesFilter(const std::string& text, const QString& query) {
  if (query.isEmpty()) {
    return true;
  }
  return QString::fromStdString(text).contains(query, Qt::CaseInsensitive);
}

}  // namespace

class StringsTableModel final : public QAbstractTableModel {
 public:
  explicit StringsTableModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

  void set_data_sources(const std::vector<memory::StringScanner::StringEntry>* entries,
                        const std::vector<int>*                                visible_rows) {
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
    return 2;
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
        return formatAddressInternal(entry.address);
      case 1:
        return QString::fromStdString(entry.text);
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
        return ("Address");
      case 1:
        return ("String");
      default:
        return {};
    }
  }

 private:
  const std::vector<memory::StringScanner::StringEntry>* m_entries     = nullptr;
  const std::vector<int>*                                m_visibleRows = nullptr;
};

StringsWindow::StringsWindow(QWidget* parent) : QMainWindow(parent) {
  applyTheme();
  configureWindow();
  updateWindowState();
}

StringsWindow::~StringsWindow() {
  if (m_scanThread != nullptr) {
    m_scanThread->wait();
    delete m_scanThread;
    m_scanThread = nullptr;
  }
}

void StringsWindow::setAttachedProcess(std::uint32_t processId, const QString& processName) {
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

void StringsWindow::applyTheme() {
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

void StringsWindow::configureWindow() {
  resize(1080, 760);
  setCentralWidget(buildCentralArea());
}

QWidget* StringsWindow::buildCentralArea() {
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
  m_filterInput->setPlaceholderText(("Filter strings..."));
  topRow->addWidget(m_filterInput, 1);

  m_refreshButton = new QPushButton(("Refresh"), panel);
  topRow->addWidget(m_refreshButton);
  panelLayout->addLayout(topRow);

  m_statusLabel = new QLabel(panel);
  panelLayout->addWidget(m_statusLabel);

  m_table      = new QTableView(panel);
  m_tableModel = new StringsTableModel(m_table);
  m_tableModel->set_data_sources(&m_entries, &m_filteredRows);

  m_table->setModel(m_tableModel);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setSortingEnabled(false);
  m_table->verticalHeader()->setVisible(false);
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_table->setContextMenuPolicy(Qt::CustomContextMenu);
  panelLayout->addWidget(m_table, 1);

  rootLayout->addWidget(panel, 1);

  connect(m_filterInput, &QLineEdit::textChanged, this, &StringsWindow::applyFilter);
  connect(m_refreshButton, &QPushButton::clicked, this, &StringsWindow::refreshScan);
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

void StringsWindow::refreshScan() {
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
    m_statusLabel->setText(("Scanning strings..."));
  }

  const std::uint32_t processId = m_processId;
  QThread*            thread    = QThread::create([this, processId, generation]() {
    memory::MemoryReader reader;
    if (!reader.attach(static_cast<memory::Process::Id>(processId))) {
      return;
    }

    memory::StringScanner              scanner(&reader);
    memory::StringScanner::ScanOptions options{};
    options.min_length               = 4;
    options.max_length               = 512;
    options.max_results              = 250000;
    options.chunk_size               = 1024 * 1024;
    options.scan_ascii               = true;
    options.scan_utf16               = true;
    options.include_writable_regions = true;
    options.worker_threads =
        static_cast<std::size_t>((std::max)(1, QThread::idealThreadCount() - 1));
    scanner.find_all_batched(
        options, 4000, [this, generation](std::vector<memory::StringScanner::StringEntry>&& batch) {
          if (batch.empty()) {
            return;
          }

          auto batchPtr =
              std::make_shared<std::vector<memory::StringScanner::StringEntry>>(std::move(batch));
          QMetaObject::invokeMethod(
              this,
              [this, generation, batchPtr]() mutable {
                appendScanBatch(generation, std::move(*batchPtr));
              },
              Qt::QueuedConnection);
        });
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

void StringsWindow::appendScanBatch(std::uint64_t                                     generation,
                                    std::vector<memory::StringScanner::StringEntry>&& batch) {
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
      const auto& entry = m_entries[static_cast<std::size_t>(row)];
      if (stringMatchesFilter(entry.text, activeFilter)) {
        m_filteredRows.push_back(row);
      }
    }
  }

  if (m_tableModel != nullptr) {
    m_tableModel->set_data_sources(&m_entries, &m_filteredRows);
  }

  updateWindowState();
}

void StringsWindow::onScanFinished(std::uint64_t generation) {
  if (generation != m_scanGeneration) {
    return;
  }
  applyFilter(m_filterInput == nullptr ? QString{} : m_filterInput->text());
  updateWindowState();
}

void StringsWindow::applyFilter(const QString& query) {
  m_filteredRows.clear();
  m_filteredRows.reserve(m_entries.size());

  const QString normalized = query.trimmed();
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    const auto&   entry   = m_entries[i];
    const QString text    = QString::fromStdString(entry.text);
    const bool    matches = normalized.isEmpty() || text.contains(normalized, Qt::CaseInsensitive);
    if (matches) {
      m_filteredRows.push_back(static_cast<int>(i));
    }
  }

  if (m_tableModel != nullptr) {
    m_tableModel->set_data_sources(&m_entries, &m_filteredRows);
  }

  updateWindowState();
}

void StringsWindow::updateWindowState() {
  if (m_processId != 0 && !m_processName.isEmpty()) {
    setWindowTitle(QString(("String Scanner - %1")).arg(m_processName));
  } else {
    setWindowTitle(("String Scanner"));
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
        QString(("Scanning strings... Attached: %1 (PID %2)")).arg(m_processName).arg(m_processId));
    return;
  }

  m_statusLabel->setText(QString(("Attached: %1 (PID %2)  |  Strings: %3  |  Visible: %4"))
                             .arg(m_processName)
                             .arg(m_processId)
                             .arg(m_entries.size())
                             .arg(m_filteredRows.size()));
}

}  // namespace farcal::ui
