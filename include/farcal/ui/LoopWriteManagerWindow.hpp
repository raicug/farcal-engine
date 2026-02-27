#pragma once

#include "farcal/ui/LoopWriteTypes.hpp"

#include <QMainWindow>

#include <cstdint>
#include <vector>

class QLabel;
class QPushButton;
class QTableWidget;
class QWidget;

namespace farcal::ui {

class LoopWriteManagerWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit LoopWriteManagerWindow(QWidget* parent = nullptr);

  void setEntries(const std::vector<LoopWriteEntry>& entries);

 signals:
  void stopSelectedRequested(const std::vector<std::uint64_t>& ids);

 private:
  void applyTheme();
  void configureWindow();
  QWidget* buildCentralArea();
  void onStopSelectedClicked();

  QLabel* m_statusLabel = nullptr;
  QTableWidget* m_table = nullptr;
  QPushButton* m_stopSelectedButton = nullptr;
};

}  // namespace farcal::ui
