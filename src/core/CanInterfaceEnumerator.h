#pragma once
#include <QStringList>
#include <QDir>
#include <QSet>

/**
 * Klasa pomocnicza – zwraca listę dostępnych interfejsów CAN (vcan*, can*).
 * Odświeżana przez rescan().
 */
class CanInterfaceEnumerator {
public:
    static QStringList availableCanInterfaces() {
        QDir netDir("/sys/class/net");
        QStringList entries = netDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QStringList result;
        for (const QString &name : entries) {
            if (name.startsWith("vcan") || name.startsWith("can"))
                result.append(name);
        }
        result.sort();
        return result;
    }
};
