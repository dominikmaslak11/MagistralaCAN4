#include <QApplication>
#include <QSurfaceFormat>
#include <QIcon>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include "gui/MainWindow.h"
#include "core/CanFrame.h"

static QFile g_logFile;
static QTextStream g_logStream;

static void initLog()
{
    g_logFile.setFileName("magistrala_debug.log");
    if (g_logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        g_logStream.setDevice(&g_logFile);
        g_logStream << "=== MagistralaCAN4 Debug Log ===\n";
        g_logStream << "Started: " << QDateTime::currentDateTime().toString() << "\n";
        g_logStream.flush();
    }
}

static void logMsg(const QString &msg)
{
    if (g_logFile.isOpen()) {
        g_logStream << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " " << msg << "\n";
        g_logStream.flush();
    }
}

int main(int argc, char *argv[])
{
    initLog();
    logMsg("main() entered");
    logMsg(QString("argc=%1 argv[0]=%2").arg(argc).arg(argc > 0 ? argv[0] : "(none)"));

    // Rejestracja CanFrame dla queued signal/slot (wymagane przy >512B)
    logMsg("qRegisterMetaType<CanFrame>...");
    qRegisterMetaType<CanFrame>("CanFrame");

    // Wymuś akcelerację GPU (OpenGL/Vulkan) - Qt6 domyślnie używa RHI
    logMsg("Creating QApplication...");
    QApplication app(argc, argv);
    logMsg("QApplication created");
    app.setApplicationName("MagistralaCAN4");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("CustomLabs");

    // Ikona aplikacji (pasek zadań KDE, środowisko)
    logMsg("Setting window icon...");
    app.setWindowIcon(QIcon(":/ico.png"));

    // Styl Fusion dla spójnego wyglądu z akceleracją sprzętową
    logMsg("Setting Fusion style...");
    app.setStyle("Fusion");

    logMsg("Creating MainWindow...");
    MainWindow mainWindow;
    logMsg("MainWindow created");
    mainWindow.setWindowTitle("MagistralaCAN4 - Sniffer CAN (GPU accelerated)");
    mainWindow.resize(1280, 800);
    logMsg("Calling mainWindow.show()...");
    mainWindow.show();
    logMsg("Entering event loop...");

    int ret = app.exec();
    logMsg(QString("Event loop exited with code %1").arg(ret));
    return ret;
}
