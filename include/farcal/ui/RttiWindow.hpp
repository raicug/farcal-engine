#pragma once

#include "farcal/memory/RttiScanner.hpp"

#include <QMainWindow>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;
class QThread;
class QWidget;

namespace farcal::ui {

class RttiTableModel;

class RttiWindow final : public QMainWindow {
public:
    explicit RttiWindow(QWidget* parent = nullptr);
    ~RttiWindow() override;

    void setAttachedProcess(std::uint32_t processId, const QString& processName);

    static QString formatAddress(std::uintptr_t address);

private:
    void applyTheme();
    void configureWindow();
    QWidget* buildCentralArea();
    void refreshScan();
    void appendScanBatch(std::uint64_t generation, std::vector<memory::RttiScanner::TypeInfo>&& batch);
    void onScanFinished(std::uint64_t generation);
    void applyFilter(const QString& query);
    void updateWindowState();

    std::uint32_t m_processId = 0;
    QString m_processName;

    std::vector<memory::RttiScanner::TypeInfo> m_entries;
    std::vector<int> m_filteredRows;

    QLineEdit* m_filterInput = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTableView* m_table = nullptr;
    RttiTableModel* m_tableModel = nullptr;

    QThread* m_scanThread = nullptr;
    bool m_scanInProgress = false;
    bool m_rescanPending = false;
    std::uint64_t m_scanGeneration = 0;
};

} // namespace farcal::ui
