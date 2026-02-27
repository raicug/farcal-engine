#pragma once

#include "q_lit.hpp"

#include <QString>
#include <memory>

namespace farcal::ui {
class LogWindow;

class Logger {
 public:
  static Logger& instance();

  void setLogWindow(LogWindow* window);
  void log(const QString& message);

  void showWindow();
  void hideWindow();

 private:
  Logger()                         = default;
  ~Logger()                        = default;
  Logger(const Logger&)            = delete;
  Logger& operator=(const Logger&) = delete;

  LogWindow* m_logWindow = nullptr;
};

// Convenience macros
#define LOG_DEBUG(msg) farcal::ui::Logger::instance().log(QString(("[DEBUG] ")) + msg)
#define LOG_INFO(msg) farcal::ui::Logger::instance().log(QString(("[INFO] ")) + msg)
#define LOG_WARNING(msg) farcal::ui::Logger::instance().log(QString(("[WARNING] ")) + msg)
#define LOG_ERROR(msg) farcal::ui::Logger::instance().log(QString(("[ERROR] ")) + msg)

}  // namespace farcal::ui
