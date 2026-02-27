#include "farcal/ui/Logger.hpp"
#include "farcal/ui/LogWindow.hpp"

namespace farcal::ui
{

    Logger &Logger::instance()
    {
        static Logger instance;
        return instance;
    }

    void Logger::setLogWindow(LogWindow *window)
    {
        m_logWindow = window;
    }

    void Logger::log(const QString &message)
    {
        if (m_logWindow != nullptr)
        {
            m_logWindow->appendLog(message);
        }
    }

    void Logger::showWindow()
    {
        if (m_logWindow != nullptr)
        {
            m_logWindow->show();
            m_logWindow->raise();
            m_logWindow->activateWindow();
        }
    }

    void Logger::hideWindow()
    {
        if (m_logWindow != nullptr)
        {
            m_logWindow->hide();
        }
    }

} // namespace farcal::ui
