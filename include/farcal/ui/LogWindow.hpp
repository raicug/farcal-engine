#pragma once

#include <QMainWindow>
#include <QString>

class QTextEdit;
class QPushButton;

namespace farcal::ui
{

    class LogWindow final : public QMainWindow
    {
        Q_OBJECT
    public:
        explicit LogWindow(QWidget *parent = nullptr);
        ~LogWindow() override = default;

        void appendLog(const QString &message);

    private:
        void applyTheme();
        void configureWindow();

        QTextEdit *m_logText = nullptr;
        QPushButton *m_clearButton = nullptr;
    };

} // namespace farcal::ui
