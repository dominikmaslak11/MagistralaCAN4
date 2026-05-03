#include <QApplication>
#include "gui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MagistralaCAN4");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("CustomLabs");

    // Styl Fusion dla spójnego wyglądu
    app.setStyle("Fusion");

    MainWindow mainWindow;
    mainWindow.setWindowTitle("MagistralaCAN4 - Sniffer CAN");
    mainWindow.resize(1280, 800);
    mainWindow.show();

    return app.exec();
}
