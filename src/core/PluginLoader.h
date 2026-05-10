#pragma once
#include <QObject>
#include <QVector>
#include <QLibrary>
#include "ProtocolPlugin.h"

/**
 * @brief Ładuje i zarządza wtyczkami protokołów.
 *
 * Skanuje katalog `plugins/` w poszukiwaniu bibliotek `.so`,
 * ładuje je i udostępnia listę aktywnych wtyczek.
 */
class PluginLoader : public QObject {
    Q_OBJECT
public:
    explicit PluginLoader(QObject *parent = nullptr);
    ~PluginLoader() override;

    /// Ładuje wszystkie wtyczki z katalogu.
    int loadFromDirectory(const QString &dir);

    /// Lista załadowanych wtyczek.
    [[nodiscard]] const QVector<ProtocolPlugin *> &plugins() const { return m_plugins; }

public slots:
    /// Przekazuje ramkę do wszystkich wtyczek.
    void broadcastFrame(const CanFrame &frame);

private:
    QVector<QLibrary *> m_libraries;
    QVector<ProtocolPlugin *> m_plugins;
    QSet<ProtocolPlugin *> m_crashedPlugins;  // pluginy, które crashowały — pomijane
};
