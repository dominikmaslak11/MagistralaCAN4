#!/usr/bin/env bash
# add_model_export.sh – eksport/import modeli (transfer learning)
set -e

echo "=== Dodawanie eksportu/importu modeli ==="

# 1. Nagłówek – nowe przyciski i sloty
sed -i '/QPushButton  \*m_saveBtn;/a\    QPushButton  *m_exportModelsBtn;\n    QPushButton  *m_importModelsBtn;' src/core/AssociativeLearner.h
sed -i '/void loadSession();/a\    void exportModels();\n    void importModels();' src/core/AssociativeLearner.h

# 2. Konstruktor – tworzenie przycisków (zaraz za przyciskami save/load)
sed -i '/m_loadBtn = new QPushButton("📂 Wczytaj sesję");/a\
    m_exportModelsBtn = new QPushButton("📤 Eksportuj modele");\
    m_importModelsBtn = new QPushButton("📥 Importuj modele");' src/core/AssociativeLearner.cpp

# Dodanie ich do layoutu (po serLayout)
sed -i '/serLayout->addWidget(m_loadBtn);/a\    serLayout->addWidget(m_exportModelsBtn); serLayout->addWidget(m_importModelsBtn);' src/core/AssociativeLearner.cpp

# Podłączenie sygnałów
sed -i '/connect(m_loadBtn, &QPushButton::clicked, this, &AssociativeLearner::loadSession);/a\    connect(m_exportModelsBtn, &QPushButton::clicked, this, &AssociativeLearner::exportModels);\n    connect(m_importModelsBtn, &QPushButton::clicked, this, &AssociativeLearner::importModels);' src/core/AssociativeLearner.cpp

# 3. Implementacja exportModels() i importModels() na końcu pliku .cpp
cat >> src/core/AssociativeLearner.cpp << 'EOF'

// ---------- Eksport / import modeli ----------
void AssociativeLearner::exportModels() {
    QString path = QFileDialog::getSaveFileName(this, "Eksportuj modele", "", "JSON (*.json)");
    if (path.isEmpty()) return;

    QJsonObject root;
    root["adaptiveBefore"] = (qint64)m_adaptiveBefore;
    root["adaptiveAfter"]  = (qint64)m_adaptiveAfter;

    // Modele liniowe (predykcja wartości)
    QJsonArray linearArr;
    for (auto it = m_linearModels.begin(); it != m_linearModels.end(); ++it) {
        QJsonObject modelObj;
        modelObj["id"]    = (int)it.key().first;
        modelObj["byte"]  = it.key().second;
        modelObj["a"]     = it.value().first;
        modelObj["b"]     = it.value().second;
        linearArr.append(modelObj);
    }
    root["linearModels"] = linearArr;

    // Łańcuch Markowa
    QJsonObject markovObj;
    QJsonArray transArr;
    for (auto it = m_transitions.begin(); it != m_transitions.end(); ++it) {
        QJsonObject fromObj;
        fromObj["fromId"] = (int)it.key();
        QJsonObject targets;
        for (auto t = it.value().begin(); t != it.value().end(); ++t)
            targets[QString::number(t.key())] = t.value();
        fromObj["targets"] = targets;
        transArr.append(fromObj);
    }
    markovObj["transitions"] = transArr;

    QJsonObject bestNextObj;
    for (auto it = m_markovBestNext.begin(); it != m_markovBestNext.end(); ++it)
        bestNextObj[QString::number(it.key())] = (int)it.value();
    markovObj["bestNext"] = bestNextObj;

    QJsonObject probObj;
    for (auto it = m_markovProb.begin(); it != m_markovProb.end(); ++it)
        probObj[QString::number(it.key())] = it.value();
    markovObj["probabilities"] = probObj;
    root["markov"] = markovObj;

    // Model normalny (anomalie)
    QJsonArray meanArr, stdArr;
    for (double v : m_normalMean) meanArr.append(v);
    for (double v : m_normalStd)  stdArr.append(v);
    root["normalMean"] = meanArr;
    root["normalStd"]  = stdArr;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

void AssociativeLearner::importModels() {
    QString path = QFileDialog::getOpenFileName(this, "Importuj modele", "", "JSON (*.json)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject root = doc.object();

    m_adaptiveBefore = root["adaptiveBefore"].toVariant().toLongLong();
    m_adaptiveAfter  = root["adaptiveAfter"].toVariant().toLongLong();

    // Modele liniowe
    m_linearModels.clear();
    QJsonArray linearArr = root["linearModels"].toArray();
    for (const auto &val : linearArr) {
        QJsonObject obj = val.toObject();
        uint32_t id = obj["id"].toInt();
        int byte = obj["byte"].toInt();
        double a = obj["a"].toDouble();
        double b = obj["b"].toDouble();
        m_linearModels[{id, byte}] = {a, b};
    }
    if (!m_linearModels.isEmpty()) {
        m_predictionTimer->start(500);
    }

    // Markow
    if (root.contains("markov")) {
        QJsonObject markovObj = root["markov"].toObject();
        m_transitions.clear();
        QJsonArray transArr = markovObj["transitions"].toArray();
        for (const auto &tval : transArr) {
            QJsonObject fromObj = tval.toObject();
            uint32_t fromId = fromObj["fromId"].toInt();
            QJsonObject targets = fromObj["targets"].toObject();
            for (auto it = targets.begin(); it != targets.end(); ++it) {
                uint32_t toId = it.key().toUInt();
                int count = it.value().toInt();
                m_transitions[fromId][toId] = count;
            }
        }

        m_markovBestNext.clear();
        QJsonObject bestNextObj = markovObj["bestNext"].toObject();
        for (auto it = bestNextObj.begin(); it != bestNextObj.end(); ++it)
            m_markovBestNext[it.key().toUInt()] = it.value().toInt();

        m_markovProb.clear();
        QJsonObject probObj = markovObj["probabilities"].toObject();
        for (auto it = probObj.begin(); it != probObj.end(); ++it)
            m_markovProb[it.key().toUInt()] = it.value().toDouble();

        if (!m_transitions.isEmpty())
            m_markovTimer->start(1000);
    }

    // Model normalny
    m_normalMean.clear();
    QJsonArray meanArr = root["normalMean"].toArray();
    for (const auto &v : meanArr) m_normalMean.append(v.toDouble());

    m_normalStd.clear();
    QJsonArray stdArr = root["normalStd"].toArray();
    for (const auto &v : stdArr) m_normalStd.append(v.toDouble());

    // Odśwież widoki
    updateCandidates();
    updatePredictionDisplay();
    predictNextFrames();
}
EOF

echo "=== Transfer learning dodany. Kompiluj: cd build && make -j\$(nproc) ==="
