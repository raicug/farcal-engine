#pragma once

#include "farcal/memory/MemoryReader.hpp"

#include <QMainWindow>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

class QTableWidget;
class QWidget;

namespace farcal::ui {

class MemoryViewerWindow final : public QMainWindow {
public:
    explicit MemoryViewerWindow(QWidget* parent = nullptr);
    ~MemoryViewerWindow() override;

    void setAttachedProcess(std::uint32_t processId, const QString& processName);
    void focusAddress(std::uintptr_t address);

private:
    struct RegionEntry {
        std::uintptr_t base = 0;
        std::size_t size = 0;
        std::uint32_t state = 0;
        std::uint32_t protection = 0;
        std::uint32_t type = 0;
    };

    void applyTheme();
    void configureWindow();
    void configureMenuBar();
    void showAddressProtectionMap();
    QWidget* buildCentralArea();
    QWidget* buildDisassemblyView();
    QWidget* buildHexDumpView();
    void openGotoAddressDialog();
    void refreshViewAt(std::uintptr_t address);
    void refreshViews();
    void refreshRegionList();
    void fillDisassemblyTable(std::uintptr_t address);
    void fillHexGrid(std::uintptr_t address);
    void scheduleHexFlashReset(const std::vector<std::pair<int, int>>& changedCells, int generation);
    void clearViewerData();
    bool parseAddressText(const QString& text, std::uintptr_t& address);
    void updateProcessState();
    QString protectionCategory(std::uint32_t protection) const;

    std::unique_ptr<memory::MemoryReader> m_memoryReader;
    std::uint32_t m_processId = 0;
    QString m_processName;
    std::vector<RegionEntry> m_regions;

    QTableWidget* m_disassemblyTable = nullptr;
    QTableWidget* m_hexGrid = nullptr;

    std::uintptr_t m_viewBaseAddress = 0;
    std::uintptr_t m_currentAddress = 0;

    std::uintptr_t m_previousHexBase = 0;
    std::vector<std::uint8_t> m_previousHexBytes;
    std::vector<std::uint8_t> m_previousHexValid;
    int m_hexFlashGeneration = 0;
};

} // namespace farcal::ui
