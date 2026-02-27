#include "farcal/app/Application.hpp"

namespace farcal::app {

Application::Application(int& argc, char** argv)
    : m_qtApplication(argc, argv) {
}

int Application::run() {
    m_mainWindow.show();
    return m_qtApplication.exec();
}

} // namespace farcal::app
