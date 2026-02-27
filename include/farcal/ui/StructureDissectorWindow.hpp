#pragma once

#include "farcal/memory/MemoryReader.hpp"
#include "farcal/memory/RttiScanner.hpp"

#include <QMainWindow>
#include <QPointer>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QThread;
class QWidget;

namespace farcal::ui
{

    class StructureDissectorWindow final : public QMainWindow
    {
        Q_OBJECT
    public:
        explicit StructureDissectorWindow(QWidget *parent = nullptr);
        ~StructureDissectorWindow() override;

        void setAttachedProcess(std::uint32_t processId, const QString &processName);
        void focusAddress(std::uintptr_t address);

    private:
        struct RowDisplay
        {
            int row = 0;
            QString address;
            QString rtti;
            QString offset;
            QString byteValue;
            QString dwordValue;
            QString qwordValue;
            QString valueDisplay;
            QString type;
            bool isPointer = false;
        };

        void applyTheme();
        void configureWindow();
        void createMenuBar();
        QWidget *buildCentralArea();
        void refreshFromInput();
        void showRebaseDialog();
        void applyRebase(std::intptr_t offset);
        void onItemExpanded(QTreeWidgetItem *item);
        void onTreeContextMenu(const QPoint &pos);
        void loadChildrenForItem(QTreeWidgetItem *item, int childCount = 64);
        bool parseAddressText(const QString &text, std::uintptr_t &address) const;
        bool writeValueToItem(QTreeWidgetItem *item, const QString& mode, const QString& inputText);
        void startFillAddressTable(std::uintptr_t startAddress);
        void appendRowBatch(
            std::uint64_t generation,
            int totalRows,
            int processedRows,
            const std::shared_ptr<std::vector<RowDisplay>> &batch);
        void onFillFinished(std::uint64_t generation, const QString &finalStatus);
        void updateWindowState();
        static QString formatAddress(std::uintptr_t address);

        std::unique_ptr<memory::MemoryReader> m_memoryReader;
        std::unique_ptr<memory::RttiScanner> m_rttiScanner;
        std::uint32_t m_processId = 0;
        QString m_processName;

        QLineEdit *m_startAddressInput = nullptr;
        QPushButton *m_refreshButton = nullptr;
        QLabel *m_statusLabel = nullptr;
        QTreeWidget *m_tree = nullptr;

        QThread *m_fillThread = nullptr;
        bool m_fillInProgress = false;
        bool m_refillPending = false;
        std::uintptr_t m_pendingStartAddress = 0;
        std::uint64_t m_fillGeneration = 0;
        std::atomic<bool> m_shouldStop{false};
        std::intptr_t m_rebaseOffset = 0;
    };

} // namespace farcal::ui
