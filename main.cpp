#include <QApplication>
#include <QSurfaceFormat>
#include <QIcon>
#include "gui/MainWindow.h"

int main(int argc, char *argv[])
{
    // Wymuś akcelerację GPU (OpenGL/Vulkan) - Qt6 domyślnie używa RHI
    QApplication app(argc, argv);
    app.setApplicationName("MagistralaCAN4");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("CustomLabs");

    // Ikona aplikacji (pasek zadań KDE, środowisko)
    app.setWindowIcon(QIcon(":/ico.png"));

    // Styl Fusion dla spójnego wyglądu z akceleracją sprzętową
    app.setStyle("Fusion");

    MainWindow mainWindow;
    mainWindow.setWindowTitle("MagistralaCAN4 - Sniffer CAN (GPU accelerated)");
    mainWindow.resize(1280, 800);
    mainWindow.show();

    return app.exec();
}
