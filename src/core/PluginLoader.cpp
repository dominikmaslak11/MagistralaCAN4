#include "PluginLoader.h"
#include <QDir>
#include <QDebug>
#include <QApplication>
#include <QSet>
#include <csignal>
#include <csetjmp>

// Stan dla setjmp — thread-local, więc bezpieczne przy wielu wątkach
static thread_local jmp_buf g_pluginJumpBuf;
static thread_local ProtocolPlugin *g_pluginInCall = nullptr;
static thread_local QSet<ProtocolPlugin *> *g_crashedSet = nullptr;

static void pluginCrashHandler(int) {
    if (g_pluginInCall && g_crashedSet) {
        qWarning() << "PluginLoader: plugin" << g_pluginInCall->name()
                   << "wykonał niedozwoloną operację (SIGSEGV) — odizolowany";
        g_crashedSet->insert(g_pluginInCall);
    }
    longjmp(g_pluginJumpBuf, 1);
}

PluginLoader::PluginLoader(QObject *parent) : QObject(parent) {}

PluginLoader::~PluginLoader() {
    for (auto *lib : m_libraries) {
        lib->unload();
        delete lib;
    }
    m_plugins.clear();
}

int PluginLoader::loadFromDirectory(const QString &dir) {
    QDir d(dir);
    if (!d.exists()) return 0;
    int count = 0;
    for (const auto &fi : d.entryInfoList({"*.so"}, QDir::Files)) {
        auto *lib = new QLibrary(fi.absoluteFilePath(), this);
        if (!lib->load()) {
            qWarning() << "PluginLoader: nie można załadować" << fi.fileName() << ":" << lib->errorString();
            delete lib; continue;
        }
        auto factory = reinterpret_cast<ProtocolPlugin *(*)()>(lib->resolve("createPlugin"));
        if (!factory) {
            qWarning() << "PluginLoader: brak createPlugin w" << fi.fileName();
            lib->unload(); delete lib; continue;
        }
        ProtocolPlugin *plugin = factory();
        if (!plugin) { lib->unload(); delete lib; continue; }
        m_libraries.append(lib);
        m_plugins.append(plugin);
        qDebug() << "PluginLoader: załadowano" << plugin->name() << "–" << plugin->description();
        count++;
    }
    return count;
}

void PluginLoader::broadcastFrame(const CanFrame &frame) {
    // Zainstaluj handler SIGSEGV (przywrócimy po pętli)
    auto oldHandler = std::signal(SIGSEGV, pluginCrashHandler);

    for (auto *p : m_plugins) {
        if (m_crashedPlugins.contains(p)) continue;

        g_pluginInCall = p;
        g_crashedSet = &m_crashedPlugins;

        if (setjmp(g_pluginJumpBuf) == 0) {
            if (p->isRelevant(frame))
                p->processFrame(frame);
        }
        // Po siglongjmp lub normalnym powrocie — kontynuuj
    }

    g_pluginInCall = nullptr;
    g_crashedSet = nullptr;
    std::signal(SIGSEGV, oldHandler); // przywróć poprzedni handler
}
