#include <QApplication>
#include <QSurfaceFormat>
#include <QIcon>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QTimer>
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

    // Wymuś akcelerację GPU (OpenGL) przez Qt6 RHI
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapInterval(1);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    QSurfaceFormat::setDefaultFormat(format);
    logMsg("QSurfaceFormat configured: OpenGL 3.3 Core, depth=24, stencil=8, vsync=on");

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

    // Ładowanie arkusza stylów QSS (dark theme domyślnie)
    logMsg("Loading QSS stylesheet...");
    QFile qssFile(":/resources/style_dark.qss");
    if (qssFile.open(QFile::ReadOnly | QFile::Text)) {
        QString qss = qssFile.readAll();
        app.setStyleSheet(qss);
        qssFile.close();
        logMsg("QSS dark theme loaded");
    } else {
        logMsg("WARNING: Could not load style_dark.qss from resources");
    }

    logMsg("Creating MainWindow...");
    MainWindow mainWindow;
    logMsg("MainWindow created");
    mainWindow.setWindowTitle("MagistralaCAN4 - Sniffer CAN (GPU accelerated)");
    mainWindow.resize(1280, 800);
    QString remoteCanUrl;
    QString remoteCanToken = "icsim";
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--remote-can-url" && i + 1 < args.size())
            remoteCanUrl = args[++i];
        else if (args[i] == "--remote-can-token" && i + 1 < args.size())
            remoteCanToken = args[++i];
    }

    logMsg("Calling mainWindow.show()...");
    mainWindow.show();
    if (!remoteCanUrl.isEmpty()) {
        logMsg(QString("Scheduling Remote CAN auto-connect: %1").arg(remoteCanUrl));
        QTimer::singleShot(700, &mainWindow, [&mainWindow, remoteCanUrl, remoteCanToken]() {
            mainWindow.connectRemoteCan(remoteCanUrl, remoteCanToken);
        });
    }
    logMsg("Entering event loop...");

    int ret = app.exec();
    logMsg(QString("Event loop exited with code %1").arg(ret));
    return ret;
}
