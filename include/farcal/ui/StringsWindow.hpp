#pragma once

#include "farcal/memory/StringScanner.hpp"

#include <QMainWindow>
#include <QString>

#include <cstdint>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;
class QThread;
class QWidget;

namespace farcal::ui {

class StringsTableModel;

class StringsWindow final : public QMainWindow {
public:
    explicit StringsWindow(QWidget* parent = nullptr);
    ~StringsWindow() override;

    void setAttachedProcess(std::uint32_t processId, const QString& processName);

private:
    void applyTheme();
    void configureWindow();
    QWidget* buildCentralArea();
    void refreshScan();
    void appendScanBatch(std::uint64_t generation, std::vector<memory::StringScanner::StringEntry>&& batch);
    void onScanFinished(std::uint64_t generation);
    void applyFilter(const QString& query);
    void updateWindowState();

    std::uint32_t m_processId = 0;
    QString m_processName;

    std::vector<memory::StringScanner::StringEntry> m_entries;
    std::vector<int> m_filteredRows;

    QLineEdit* m_filterInput = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTableView* m_table = nullptr;
    StringsTableModel* m_tableModel = nullptr;

    QThread* m_scanThread = nullptr;
    bool m_scanInProgress = false;
    bool m_rescanPending = false;
    std::uint64_t m_scanGeneration = 0;
};

} // namespace farcal::ui
