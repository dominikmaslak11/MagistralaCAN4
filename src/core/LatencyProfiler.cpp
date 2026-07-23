#include "LatencyProfiler.h"
#include <QFile>
#include <QJsonDocument>
#include <QDebug>

LatencyProfiler::LatencyProfiler(QObject *parent) : QObject(parent) {}

void LatencyProfiler::addSample(const LatencySample &sample) {
    QString key = sample.modelName.isEmpty() ? QStringLiteral("unknown") : sample.modelName;
    m_samples[key].push_back(sample);

    int current = static_cast<int>(m_samples[key].size());
    emit sampleAdded(key, current, m_trialsPerModel);

    if (current >= m_trialsPerModel) {
        qDebug() << "[LatencyProfiler] Model" << key
                 << "complete:" << current << "/" << m_trialsPerModel << "samples";
        emit modelComplete(key);
    }
}

std::vector<LatencySample> LatencyProfiler::samplesForModel(const QString &model) const {
    auto it = m_samples.find(model);
    if (it != m_samples.end()) return it->second;
    return {};
}

QStringList LatencyProfiler::modelNames() const {
    QStringList names;
    for (auto it = m_samples.constBegin(); it != m_samples.constEnd(); ++it)
        names.append(it.key());
    return names;
}

bool LatencyProfiler::isModelComplete(const QString &model) const {
    auto it = m_samples.find(model);
    if (it == m_samples.end()) return false;
    return static_cast<int>(it->second.size()) >= m_trialsPerModel;
}

int LatencyProfiler::sampleCount(const QString &model) const {
    auto it = m_samples.find(model);
    if (it == m_samples.end()) return 0;
    return static_cast<int>(it->second.size());
}

void LatencyProfiler::reset() {
    m_samples.clear();
}

// ── Statistics ─────────────────────────────────────────────────────────────────

LatencyStats LatencyProfiler::computeStats(const QString &model) const {
    LatencyStats stats;
    stats.modelName = model;

    auto it = m_samples.find(model);
    if (it == m_samples.end() || it->second.empty()) return stats;

    const auto &samples = it->second;
    stats.sampleCount = static_cast<int>(samples.size());

    std::vector<double> detMs, txUpMs, llmMs, compMs, otaMs, totalMs;
    detMs.reserve(samples.size());
    txUpMs.reserve(samples.size());
    llmMs.reserve(samples.size());
    compMs.reserve(samples.size());
    otaMs.reserve(samples.size());
    totalMs.reserve(samples.size());

    for (const auto &s : samples) {
        double d   = s.tDetUs  / 1000.0;
        double tx  = s.tTxUpUs / 1000.0;
        double lm  = s.tLlmUs  / 1000.0;
        double cp  = s.tCompUs / 1000.0;
        double ota = s.tOtaUs  / 1000.0;
        double tot = d + tx + lm + cp + ota;

        detMs.push_back(d);
        txUpMs.push_back(tx);
        llmMs.push_back(lm);
        compMs.push_back(cp);
        otaMs.push_back(ota);
        totalMs.push_back(tot);
    }

    stats.meanDetMs   = computeMean(detMs);
    stats.meanTxUpMs  = computeMean(txUpMs);
    stats.meanLlmMs   = computeMean(llmMs);
    stats.meanCompMs  = computeMean(compMs);
    stats.meanOtaMs   = computeMean(otaMs);
    stats.meanTotalMs = computeMean(totalMs);

    stats.sigmaDetMs   = computeStdDev(detMs,   stats.meanDetMs);
    stats.sigmaTxUpMs  = computeStdDev(txUpMs,  stats.meanTxUpMs);
    stats.sigmaLlmMs   = computeStdDev(llmMs,   stats.meanLlmMs);
    stats.sigmaCompMs  = computeStdDev(compMs,  stats.meanCompMs);
    stats.sigmaOtaMs   = computeStdDev(otaMs,   stats.meanOtaMs);
    stats.sigmaTotalMs = computeStdDev(totalMs, stats.meanTotalMs);

    return stats;
}

double LatencyProfiler::computeMean(const std::vector<double> &values) {
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (double v : values) sum += v;
    return sum / static_cast<double>(values.size());
}

double LatencyProfiler::computeStdDev(const std::vector<double> &values, double mean) {
    if (values.size() < 2) return 0.0;
    double sumSq = 0.0;
    for (double v : values) {
        double diff = v - mean;
        sumSq += diff * diff;
    }
    return std::sqrt(sumSq / static_cast<double>(values.size() - 1)); // sample stddev (N-1)
}

// ── JSON export ────────────────────────────────────────────────────────────────

QJsonArray LatencyProfiler::toJsonArray() const {
    QJsonArray arr;
    for (auto it = m_samples.constBegin(); it != m_samples.constEnd(); ++it) {
        for (const auto &s : it.value()) {
            QJsonObject obj;
            obj["model"]       = s.modelName;
            obj["canId"]       = QString("0x%1").arg(s.canId, 3, 16, QChar('0'));
            obj["tDetUs"]      = static_cast<qint64>(s.tDetUs);
            obj["tTxUpUs"]     = static_cast<qint64>(s.tTxUpUs);
            obj["tLlmUs"]      = static_cast<qint64>(s.tLlmUs);
            obj["tCompUs"]     = static_cast<qint64>(s.tCompUs);
            obj["tOtaUs"]      = static_cast<qint64>(s.tOtaUs);
            obj["trialIndex"]  = static_cast<int>(s.trialIndex);
            obj["success"]     = s.success;
            if (!s.errorMsg.isEmpty())
                obj["error"] = s.errorMsg;
            arr.append(obj);
        }
    }
    return arr;
}

QJsonObject LatencyProfiler::statsToJson() const {
    QJsonObject root;
    QJsonArray statsArr;

    for (auto it = m_samples.constBegin(); it != m_samples.constEnd(); ++it) {
        LatencyStats st = computeStats(it.key());
        QJsonObject obj;
        obj["model"]         = st.modelName;
        obj["sampleCount"]   = st.sampleCount;
        obj["meanTotalMs"]   = st.meanTotalMs;
        obj["sigmaTotalMs"]  = st.sigmaTotalMs;

        QJsonObject means;
        means["tDetMs"]   = st.meanDetMs;
        means["tTxUpMs"]  = st.meanTxUpMs;
        means["tLlmMs"]   = st.meanLlmMs;
        means["tCompMs"]  = st.meanCompMs;
        means["tOtaMs"]   = st.meanOtaMs;
        obj["means"] = means;

        QJsonObject sigmas;
        sigmas["tDetMs"]   = st.sigmaDetMs;
        sigmas["tTxUpMs"]  = st.sigmaTxUpMs;
        sigmas["tLlmMs"]   = st.sigmaLlmMs;
        sigmas["tCompMs"]  = st.sigmaCompMs;
        sigmas["tOtaMs"]   = st.sigmaOtaMs;
        obj["sigmas"] = sigmas;

        statsArr.append(obj);
    }

    root["statistics"] = statsArr;
    return root;
}

QJsonObject LatencyProfiler::fullReport() const {
    QJsonObject report;
    report["samples"] = toJsonArray();
    report["stats"]   = statsToJson();
    report["trialsPerModel"] = m_trialsPerModel;
    report["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return report;
}

bool LatencyProfiler::saveReport(const QString &filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[LatencyProfiler] Cannot open file:" << filePath;
        return false;
    }
    QJsonDocument doc(fullReport());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    qDebug() << "[LatencyProfiler] Report saved to" << filePath;
    return true;
}
