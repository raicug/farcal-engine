#pragma once

#include <QMainWindow>

namespace farcal::ui {

class InfoWindow final : public QMainWindow {
public:
    explicit InfoWindow(QWidget* parent = nullptr);
    ~InfoWindow() override;

private:
    void applyTheme();
    void configureWindow();
    QWidget* buildCentralArea();
};

} // namespace farcal::ui
