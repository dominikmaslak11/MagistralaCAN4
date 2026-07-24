#include <QApplication>
#include <QCoreApplication>
#include <QSurfaceFormat>
#include <QIcon>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QDateTime>
#include <QTimer>
#include <QMap>
#include <random>
#include <sys/time.h>
#include "gui/MainWindow.h"
#include "core/CanFrame.h"
#include "core/ColdStartDetector.h"
#include "core/LlmQueryClient.h"
#include "core/LatencyProfiler.h"
#include "core/ExperimentRunner.h"

// ═══════════════════════════════════════════════════════════════════════════
// Eksperyment 1.1 — tryb headless (bez GUI, bez fizycznego ESP32)
// Uruchamiany flagą --run-experiment <plik_kluczy_api> <katalog_wyjsciowy> [N_prob]
// ═══════════════════════════════════════════════════════════════════════════

static QMap<QString, QString> parseApiKeysFile(const QString &path)
{
    QMap<QString, QString> keys;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return keys;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int idx = line.indexOf(':');
        if (idx < 0) continue;
        keys[line.left(idx).trimmed()] = line.mid(idx + 1).trimmed();
    }
    return keys;
}

static uint64_t nowUsMain()
{
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;
}

static CanFrame genSyntheticCanFrame(std::mt19937 &rng)
{
    static const uint32_t kCanIds[] = {0x100, 0x158, 0x200, 0x280, 0x300,
                                       0x3E8, 0x400, 0x500, 0x6B0, 0x7DF};
    std::uniform_int_distribution<int> idPick(0, 9);
    std::uniform_int_distribution<int> dlcPick(3, 8);
    std::uniform_int_distribution<int> bytePick(0, 255);

    CanFrame f;
    f.id = kCanIds[idPick(rng)];
    f.dlc = static_cast<uint8_t>(dlcPick(rng));
    for (int i = 0; i < f.dlc; ++i)
        f.data[i] = static_cast<uint8_t>(bytePick(rng));
    f.timestamp = nowUsMain();
    return f;
}

static int runHeadlessExperiment(int argc, char *argv[], const QString &apiKeysPath,
                                  const QString &outDir, int trialsPerModel)
{
    QCoreApplication app(argc, argv);
    qRegisterMetaType<CanFrame>("CanFrame");

    const QMap<QString, QString> keys = parseApiKeysFile(apiKeysPath);
    if (keys.isEmpty()) {
        qCritical() << "[Experiment 1.1] Nie udalo sie wczytac kluczy API z" << apiKeysPath;
        return 1;
    }
    QDir().mkpath(outDir);

    auto *detector = new ColdStartDetector(&app);
    auto *llmClient = new LlmQueryClient(&app);
    auto *profiler = new LatencyProfiler(&app);
    auto *runner = new ExperimentRunner(&app);

    runner->setDetector(detector);
    runner->setLlmClient(llmClient);
    runner->setProfiler(profiler);
    runner->setTrialsPerModel(trialsPerModel);
    runner->setReportPath(outDir + "/latency_report_full.json");

    const QString claudeModel   = QStringLiteral("claude-sonnet-5");
    const QString openaiModel   = QStringLiteral("gpt-5.6-sol");
    const QString deepseekModel = QStringLiteral("deepseek-v4-pro");
    const QString geminiModel   = QStringLiteral("gemini-3.6-flash");

    runner->addModel(claudeModel);
    runner->setApiKey(claudeModel, keys.value(QStringLiteral("CLAUDE API Key")));
    runner->addModel(openaiModel);
    runner->setApiKey(openaiModel, keys.value(QStringLiteral("CODEX API Key")));
    runner->addModel(deepseekModel);
    runner->setApiKey(deepseekModel, keys.value(QStringLiteral("DeepSeek API Key")));
    runner->addModel(geminiModel);
    runner->setApiKey(geminiModel, keys.value(QStringLiteral("Gemini API Key")));

    std::mt19937 rng{std::random_device{}()};

    QObject::connect(runner, &ExperimentRunner::progressChanged,
        [](double frac, const QString &status) {
            qInfo().noquote() << QString("[%1%] %2").arg(frac * 100.0, 5, 'f', 1).arg(status);
        });
    QObject::connect(runner, &ExperimentRunner::modelCompleted,
        [](const QString &model, const LatencyStats &stats) {
            qInfo().noquote() << QString("=== %1 zakonczony: t_llm=%2ms +/- %3ms (N=%4) ===")
                .arg(model).arg(stats.meanLlmMs, 0, 'f', 1).arg(stats.sigmaLlmMs, 0, 'f', 1)
                .arg(stats.sampleCount);
        });
    QObject::connect(runner, &ExperimentRunner::experimentFinished,
        [&app](const QString &path) {
            qInfo().noquote() << "Eksperyment zakonczony, raport:" << path;
            app.quit();
        });
    QObject::connect(runner, &ExperimentRunner::experimentError,
        [&app](const QString &msg) {
            qCritical().noquote() << "Blad eksperymentu:" << msg;
            app.exit(1);
        });

    // Wstrzykiwacz syntetycznych ramek CAN — bez fizycznego ESP32/magistrali CAN.
    // injectManualColdStart() sama sprawdza stan i jest no-opem poza WaitingForDetection,
    // więc bezpiecznie odpytujemy ją cyklicznie.
    auto *injectTimer = new QTimer(&app);
    injectTimer->setInterval(20);
    QObject::connect(injectTimer, &QTimer::timeout, [runner, &rng]() {
        if (!runner->isRunning()) return;
        CanFrame frame = genSyntheticCanFrame(rng);
        runner->injectManualColdStart(frame.id, frame);
    });
    injectTimer->start();

    runner->start();
    return app.exec();
}

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
    // Tryb headless dla Eksperymentu 1.1 (Cold Start Latency Breakdown) — musi być
    // sprawdzony PRZED utworzeniem QApplication, żeby nie wymagać serwera X/Wayland.
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == QStringLiteral("--run-experiment")) {
            if (i + 2 >= argc) {
                qCritical() << "Uzycie: --run-experiment <plik_kluczy_api> <katalog_wyjsciowy> [N_prob_na_model=30]";
                return 1;
            }
            const QString apiKeysPath = argv[i + 1];
            const QString outDir = argv[i + 2];
            int trials = 30;
            if (i + 3 < argc) trials = QString(argv[i + 3]).toInt();
            return runHeadlessExperiment(argc, argv, apiKeysPath, outDir, trials);
        }
    }

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
