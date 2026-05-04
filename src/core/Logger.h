#pragma once
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>

class Logger {
public:
    static void log(const QString &message) {
        QString path = QDir::homePath() + "/magistrala_can4.log";
        QFile file(path);
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            out << QDateTime::currentDateTime().toString("[yyyy-MM-dd hh:mm:ss.zzz] ") << message << "\n";
            file.close();
        }
        // Dodatkowo wypisz na konsolę (do debugowania)
        qDebug() << message;
    }
};
