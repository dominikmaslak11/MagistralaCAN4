#include "PluginLoader.h"
#include <QDir>
#include <QDebug>
#include <QApplication>

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
    for (auto *p : m_plugins) {
        if (p->isRelevant(frame))
            p->processFrame(frame);
    }
}
