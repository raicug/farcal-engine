#pragma once

#include "farcal/memory/ProcessMemoryScanner.hpp"
#include "farcal/memory/MemoryReader.hpp"
#include "farcal/ui/LoopWriteTypes.hpp"
#include "farcal/ui/SettingsTypes.hpp"

#include <QMainWindow>
#include <QString>

#include <QPointer>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QAction;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QThread;
class QLabel;
class QPoint;
class QTimer;

namespace farcal::ui {

class InfoWindow;
class LogWindow;
class MemoryViewerWindow;
class RttiWindow;
class StringsWindow;
class StructureDissectorWindow;
class LuaVmWindow;
class SettingsWindow;
class LoopWriteManagerWindow;

class MainWindow final : public QMainWindow {
 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void     applyTheme();
  void     configureWindow();
  void     configureMenuBar();
  void     showMemoryViewerWindow();
  void     showRttiWindow();
  void     showStringsWindow();
  void     showStructureDissectorWindow();
  void     showLuaVmWindow();
  void     showInfoWindow();
  void     showLogWindow();
  void     showSettingsWindow();
  void     showLoopWriteManagerWindow();
  QWidget* buildCentralArea();
  QWidget* buildScanPanel();
  QWidget* buildScanResultsPanel();
  QWidget* buildAddressListPanel();
  void     showAttachToProcessDialog();
  void     showAttachLastProcess();
  void     attachToProcess(std::uint32_t processId, const QString& processName);
  void     setAttachedProcessName(const QString& processName);
  void     runScan(bool firstScan);
  void     onFirstScanClicked();
  void     onNextScanClicked();
  void     onUndoScanClicked();
  void     onNewScanClicked();
  void     onScanFinished(bool success, const QString& errorMessage);
  void     updateScanProgress(std::size_t completed, std::size_t total);
  void     setScanUiBusy(bool busy);
  void     refreshScanResults();
  void     updateScanToggleState();
  memory::ScanSettings buildScanSettings() const;
  QString              formatScanValue(const std::vector<std::uint8_t>& bytes) const;
  void     onScanResultsContextMenu(const QPoint& pos);
  void     onAddressListContextMenu(const QPoint& pos);
  void     addAddressListEntry(std::uintptr_t address, const QString& type, const QString& value);
  bool     selectedAddressFromScanResults(std::uintptr_t& outAddress, QString& outType, QString& outValue) const;
  bool     selectedAddressFromAddressList(std::uintptr_t& outAddress) const;
  void     openAddressInMemoryViewer(std::uintptr_t address);
  void     openAddressInStructureDissector(std::uintptr_t address);
  bool     writeAddressValue(std::uintptr_t address, const QString& typeName, const QString& inputText);
  void     promptSetValueForAddress(std::uintptr_t address, const QString& typeName, const QString& currentValue);
  void     promptSetValueForAddressSelection(const std::vector<int>& selectedRows = {});
  std::vector<int> selectedAddressListRows() const;
  void     refreshScanResultsLiveValues();
  void     refreshAddressListLiveValues();
  QString  readLiveValueForAddress(std::uintptr_t address, const QString& typeName, bool hexMode) const;
  QString  readLiveValueForScanRow(std::uintptr_t address, int row) const;
  QString  lastProcessFilePath() const;
  void     persistLastAttachedProcess(std::uint32_t processId, const QString& processName) const;
  std::optional<std::pair<std::uint32_t, QString>> loadLastAttachedProcess() const;
  std::optional<std::uint32_t> findRunningProcessIdByName(const QString& processName) const;
  QString  settingsFilePath() const;
  void     loadKeybindSettings();
  void     saveKeybindSettings() const;
  void     applyKeybindSettings();
  void     promptLoopSetValueForScanSelection(const std::vector<int>& selectedRows);
  void     promptLoopSetValueForAddressListSelection(const std::vector<int>& selectedRows);
  void     processLoopWriteEntries();
  void     refreshLoopWriteManagerWindow();
  void     stopLoopWriteEntriesByIds(const std::vector<std::uint64_t>& ids);

  std::unique_ptr<memory::MemoryReader> m_memoryReader;
  std::unique_ptr<memory::ProcessMemoryScanner> m_homeScanner;

  std::uint32_t m_attachedProcessId = 0;
  QString       m_attachedProcessName;

  std::unique_ptr<MemoryViewerWindow>       m_memoryViewerWindow;
  std::unique_ptr<RttiWindow>               m_rttiWindow;
  std::unique_ptr<StringsWindow>            m_stringsWindow;
  std::unique_ptr<StructureDissectorWindow> m_structureDissectorWindow;
  std::unique_ptr<LuaVmWindow>              m_luaVmWindow;
  std::unique_ptr<InfoWindow>               m_infoWindow;
  std::unique_ptr<LogWindow>                m_logWindow;
  std::unique_ptr<SettingsWindow>           m_settingsWindow;
  std::unique_ptr<LoopWriteManagerWindow>   m_loopWriteManagerWindow;

  KeybindSettings m_keybindSettings = KeybindSettings::defaults();

  QAction* m_attachToProcessAction = nullptr;
  QAction* m_attachLastProcessAction = nullptr;
  QAction* m_rttiScannerAction = nullptr;
  QAction* m_stringScannerAction = nullptr;
  QAction* m_structureDissectorAction = nullptr;
  QAction* m_luaIdeAction = nullptr;

  QCheckBox* m_hexCheckBox = nullptr;
  QCheckBox* m_scanReadOnlyCheckBox = nullptr;
  QComboBox* m_scanTypeCombo = nullptr;
  QComboBox* m_valueTypeCombo = nullptr;
  QLineEdit* m_valueInput = nullptr;
  QCheckBox* m_caseSensitiveCheckBox = nullptr;
  QCheckBox* m_unicodeCheckBox = nullptr;
  QSpinBox* m_alignmentSpinBox = nullptr;
  QPushButton* m_firstScanButton = nullptr;
  QPushButton* m_nextScanButton = nullptr;
  QPushButton* m_undoScanButton = nullptr;
  QPushButton* m_newScanButton = nullptr;
  QProgressBar* m_scanProgressBar = nullptr;
  QLabel* m_foundLabel = nullptr;
  QTableWidget* m_scanResultsTable = nullptr;
  QTableWidget* m_addressListTable = nullptr;
  QPushButton* m_addressListClearButton = nullptr;
  QThread* m_scanThread = nullptr;
  QTimer* m_liveUpdateTimer = nullptr;
  QTimer* m_loopWriteTimer = nullptr;
  bool m_scanBusy = false;
  std::uint32_t m_addressListNameSeed = 1;
  int m_addressListDragAnchorRow = -1;
  std::uint64_t m_nextLoopWriteEntryId = 1;
  std::vector<LoopWriteEntry> m_loopWriteEntries;
};

}  // namespace farcal::ui
