#include "farcal/app/Application.hpp"
#include "farcal/memory/MemoryReader.hpp"
#include "farcal/memory/RttiScanner.hpp"

#if defined(QT_STATIC) && defined(Q_OS_WIN)
#include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

int main(int argc, char** argv) {
    farcal::app::Application application(argc, argv);
    return application.run();
}
