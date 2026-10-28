#include "flex/ui/main_window.hpp"

#include <QApplication>
#include <iostream>
#include <memory>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("FLEX 1.0");
    app.setApplicationDisplayName("FLEX 1.0 — Desktop AI Assistant");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("FLEX");

    std::cout << "[FLEX 1.0] Starting Native C/C++ Qt Desktop Assistant...\n";

    // Initialize backend coordinator with default SQLite storage and Ollama config
    auto coordinator = std::make_shared<flex::BackendCoordinator>();

    flex::ui::MainWindow mainWindow(coordinator);
    mainWindow.show();

    return app.exec();
}

