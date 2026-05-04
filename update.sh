#!/usr/bin/env bash
# add_lua_generator.sh – generator skryptów Lua z reguł asocjacyjnych
set -e

echo "=== Dodawanie generatora skryptów Lua ==="

# 1. Nagłówek AssociativeLearner.h – nowy przycisk i slot
sed -i '/QPushButton  \*m_importModelsBtn;/a\    QPushButton  *m_generateLuaBtn;' src/core/AssociativeLearner.h
sed -i '/void importModels();/a\    void generateLuaScript();' src/core/AssociativeLearner.h

# 2. Konstruktor AssociativeLearner.cpp – tworzenie przycisku (po importModelsBtn)
sed -i '/m_importModelsBtn = new QPushButton("📥 Importuj modele");/a\
    m_generateLuaBtn = new QPushButton("📝 Generuj skrypt Lua");' src/core/AssociativeLearner.cpp

# Dodanie do layoutu
sed -i '/serLayout->addWidget(m_importModelsBtn);/a\    serLayout->addWidget(m_generateLuaBtn);' src/core/AssociativeLearner.cpp

# Podłączenie sygnału
sed -i '/connect(m_importModelsBtn, &QPushButton::clicked, this, &AssociativeLearner::importModels);/a\    connect(m_generateLuaBtn, \&QPushButton::clicked, this, \&AssociativeLearner::generateLuaScript);' src/core/AssociativeLearner.cpp

# 3. Implementacja generateLuaScript() – na końcu pliku .cpp
cat >> src/core/AssociativeLearner.cpp << 'EOF'

// ---------- Generator skryptów Lua ----------
void AssociativeLearner::generateLuaScript() {
    QString path = QFileDialog::getSaveFileName(this, "Zapisz skrypt Lua", "reguly.lua", "Skrypty Lua (*.lua)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Błąd", "Nie można zapisać pliku.");
        return;
    }

    QTextStream out(&file);
    out << "-- Automatycznie wygenerowane reguły asocjacyjne\n";
    out << "-- Zmienna: " << m_currentVariable << "\n\n";

    // Nagłówek funkcji onFrame
    out << "function onFrame(id, data, timestamp)\n";

    // Generuj reguły dla top 5 kandydatów z najwyższą pewnością
    if (m_candidateModel) {
        // Pobierz listę kandydatów (ręcznie przeglądamy tabelę)
        int count = m_candidateModel->rowCount();
        int rules = 0;
        for (int i = 0; i < count && rules < 5; ++i) {
            QModelIndex idIndex = m_candidateModel->index(i, 0);
            QModelIndex descIndex = m_candidateModel->index(i, 1);
            QModelIndex scoreIndex = m_candidateModel->index(i, 2);
            if (!idIndex.isValid()) continue;

            QString idStr = m_candidateModel->data(idIndex).toString();
            double score = m_candidateModel->data(scoreIndex).toDouble();
            if (score < 0.5) continue;  // tylko wysoka pewność

            // Znajdź ID
            bool ok;
            uint32_t id = idStr.toUInt(&ok, 16);
            if (!ok) continue;

            // Znajdź najbardziej skorelowany bajt dla tego ID (z tabeli korelacji)
            int bestByte = 0;
            double bestCorr = 0.0;
            for (int row = 0; row < m_correlationTable->rowCount(); ++row) {
                QTableWidgetItem *item = m_correlationTable->item(row, 0);
                if (item && item->text().toUInt(nullptr, 16) == id) {
                    QTableWidgetItem *byteItem = m_correlationTable->item(row, 1);
                    QTableWidgetItem *corrItem = m_correlationTable->item(row, 2);
                    if (byteItem && corrItem) {
                        double corr = corrItem->text().toDouble();
                        if (fabs(corr) > fabs(bestCorr)) {
                            bestCorr = corr;
                            bestByte = byteItem->text().toInt();
                        }
                    }
                }
            }

            // Generuj regułę
            out << "    -- Kandydat: " << idStr << " (pewność: " << (score*100) << "%)\n";
            out << "    if id == " << id << " then\n";
            out << "        log(string.format(\"Zdarzenie: ID 0x%X, bajt " << bestByte << " = %d (korelacja " << QString::number(bestCorr, 'f', 2) << ")\", id, data[" << (bestByte+1) << "]))\n";
            out << "    end\n\n";
            rules++;
        }
    }

    // Dodaj logowanie dla zmiennej (jeśli są obserwacje)
    QVector<ValueObservation> obs = currentObservations();
    if (!obs.isEmpty()) {
        double lastVal = obs.last().value;
        out << "    -- Zmienna \"" << m_currentVariable << "\" ostatnia wartość: " << lastVal << "\n";
        out << "    -- Możesz dodać własne reguły tutaj\n";
    }

    out << "end\n";
    file.close();

    Logger::log(QString("Wygenerowano skrypt Lua: %1").arg(path));
    QMessageBox::information(this, "Generator Lua", "Skrypt Lua został zapisany.");
}
EOF

echo "=== Generator Lua dodany. Kompiluj: cd build && make -j\$(nproc) ==="
