#pragma once

#include "farcal/ui/MainWindow.hpp"

#include <QApplication>

namespace farcal::app {

class Application {
public:
    Application(int& argc, char** argv);

    int run();

private:
    QApplication m_qtApplication;
    ui::MainWindow m_mainWindow;
};

} // namespace farcal::app
