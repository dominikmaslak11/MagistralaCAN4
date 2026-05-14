#pragma once
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>

class Logger {
public:
    /// Logs a message to file and console.
    static void log(const QString &message) {
        QFile file(logPath());
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            out << QDateTime::currentDateTime().toString("[yyyy-MM-dd hh:mm:ss.zzz] ") << message << "\n";
            file.close();
        }
        qDebug() << message;
    }

    /// Override the log file path (call before log()). Pass empty string to restore default.
    static void setLogPath(const QString &path) {
        s_logPath = path;
    }

    /// Returns the current log file path.
    static QString logPath() {
        return s_logPath.isEmpty()
            ? QDir::homePath() + "/magistrala_can4.log"
            : s_logPath;
    }

private:
    static inline QString s_logPath;
};
