#pragma once

#include <QDialog>
#include <QString>

#include <cstdint>
#include <optional>

class QLineEdit;
class QListWidget;

namespace farcal::ui {

class AttachProcessDialog final : public QDialog {
public:
    struct Selection {
        std::uint32_t processId = 0;
        QString processName;
    };

    explicit AttachProcessDialog(QWidget* parent = nullptr);

    std::optional<Selection> selection() const;

private:
    struct WindowEntry {
        std::uint32_t processId = 0;
        QString processName;
        QString windowTitle;
    };

    void populateProcessList();
    void applyFilter(const QString& query);
    void selectFirstVisibleItem();

    QLineEdit* m_searchInput = nullptr;
    QListWidget* m_processList = nullptr;
};

std::optional<AttachProcessDialog::Selection> showAttachProcessDialog(QWidget* parent = nullptr);

} // namespace farcal::ui
