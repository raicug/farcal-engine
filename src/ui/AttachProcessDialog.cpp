#include "farcal/ui/AttachProcessDialog.hpp"
#include "q_lit.hpp"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace farcal::ui {

AttachProcessDialog::AttachProcessDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(("Attach To Process"));
  resize(640, 440);

  auto* dialogLayout = new QVBoxLayout(this);
  auto* helperText   = new QLabel(("Select a window:"), this);
  dialogLayout->addWidget(helperText);

  m_searchInput = new QLineEdit(this);
  m_searchInput->setPlaceholderText(("Search process or window title..."));
  dialogLayout->addWidget(m_searchInput);

  m_processList = new QListWidget(this);
  m_processList->setSelectionMode(QAbstractItemView::SingleSelection);
  dialogLayout->addWidget(m_processList, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  dialogLayout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(
      m_processList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { accept(); });
  connect(m_searchInput, &QLineEdit::textChanged, this, [this](const QString& query) {
    applyFilter(query);
  });

  populateProcessList();
}

std::optional<AttachProcessDialog::Selection> AttachProcessDialog::selection() const {
  if (m_processList == nullptr) {
    return std::nullopt;
  }

  const QListWidgetItem* selectedItem = m_processList->currentItem();
  if (selectedItem == nullptr) {
    return std::nullopt;
  }

  const auto processId = static_cast<std::uint32_t>(selectedItem->data(Qt::UserRole).toULongLong());
  const QString processName = selectedItem->data(Qt::UserRole + 1).toString();
  if (processId == 0 || processName.isEmpty()) {
    return std::nullopt;
  }

  return Selection{processId, processName};
}

void AttachProcessDialog::populateProcessList() {
  if (m_processList == nullptr) {
    return;
  }

#ifdef Q_OS_WIN
  struct EnumContext {
    std::vector<WindowEntry>* entries;
  };

  std::vector<WindowEntry> entries;
  EnumContext              context{&entries};

  const auto callback = [](HWND hwnd, LPARAM lParam) -> BOOL {
    auto* enumContext = reinterpret_cast<EnumContext*>(lParam);
    if (!IsWindowVisible(hwnd)) {
      return TRUE;
    }

    const int titleLength = GetWindowTextLengthW(hwnd);
    if (titleLength <= 0) {
      return TRUE;
    }

    std::wstring rawTitle(static_cast<size_t>(titleLength) + 1, L'\0');
    const int    copiedLength =
        GetWindowTextW(hwnd, rawTitle.data(), static_cast<int>(rawTitle.size()));
    if (copiedLength <= 0) {
      return TRUE;
    }
    rawTitle.resize(static_cast<size_t>(copiedLength));

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0) {
      return TRUE;
    }

    QString processName   = ("Unknown");
    HANDLE  processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (processHandle != nullptr) {
      std::wstring rawPath(1024, L'\0');
      DWORD        pathLength = static_cast<DWORD>(rawPath.size());
      if (QueryFullProcessImageNameW(processHandle, 0, rawPath.data(), &pathLength)) {
        rawPath.resize(pathLength);
        processName = QFileInfo(QString::fromStdWString(rawPath)).fileName();
      }
      CloseHandle(processHandle);
    }

    if (processName.isEmpty()) {
      processName = ("Unknown");
    }

    enumContext->entries->push_back({
        static_cast<std::uint32_t>(processId),
        processName,
        QString::fromStdWString(rawTitle),
    });
    return TRUE;
  };

  EnumWindows(callback, reinterpret_cast<LPARAM>(&context));

  std::sort(entries.begin(), entries.end(), [](const WindowEntry& lhs, const WindowEntry& rhs) {
    const int processCompare =
        QString::compare(lhs.processName, rhs.processName, Qt::CaseInsensitive);
    if (processCompare != 0) {
      return processCompare < 0;
    }
    return QString::compare(lhs.windowTitle, rhs.windowTitle, Qt::CaseInsensitive) < 0;
  });

  for (const auto& entry : entries) {
    auto* item =
        new QListWidgetItem(entry.processName + (" - ") + entry.windowTitle, m_processList);
    item->setData(Qt::UserRole, static_cast<qulonglong>(entry.processId));
    item->setData(Qt::UserRole + 1, entry.processName);
    m_processList->addItem(item);
  }

  if (m_processList->count() > 0) {
    m_processList->setCurrentRow(0);
  }
#endif
}

void AttachProcessDialog::applyFilter(const QString& query) {
  if (m_processList == nullptr) {
    return;
  }

  for (int row = 0; row < m_processList->count(); ++row) {
    QListWidgetItem* item = m_processList->item(row);
    if (item == nullptr) {
      continue;
    }
    const bool matches = item->text().contains(query, Qt::CaseInsensitive);
    item->setHidden(!matches);
  }

  QListWidgetItem* current = m_processList->currentItem();
  if (current == nullptr || current->isHidden()) {
    selectFirstVisibleItem();
  }
}

void AttachProcessDialog::selectFirstVisibleItem() {
  if (m_processList == nullptr) {
    return;
  }

  for (int row = 0; row < m_processList->count(); ++row) {
    QListWidgetItem* candidate = m_processList->item(row);
    if (candidate != nullptr && !candidate->isHidden()) {
      m_processList->setCurrentItem(candidate);
      return;
    }
  }

  m_processList->setCurrentItem(nullptr);
}

std::optional<AttachProcessDialog::Selection> showAttachProcessDialog(QWidget* parent) {
#ifdef Q_OS_WIN
  AttachProcessDialog dialog(parent);
  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }

  return dialog.selection();
#else
  QMessageBox::warning(
      parent, ("Attach To Process"), ("Attach To Process is only available on Windows."));
  return std::nullopt;
#endif
}

}  // namespace farcal::ui
