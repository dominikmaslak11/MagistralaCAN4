#!/usr/bin/env bash
# add_step_playback.sh – ręczne odtwarzanie ramek (Enter)
set -e

echo "=== Dodawanie trybu krokowego odtwarzania candump ==="

# 1. Nagłówek OfflineAnalyzer.h – nowe elementy
cat > src/core/OfflineAnalyzer.h << 'EOF'
#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QSlider>
#include <QCheckBox>
#include <QTimer>
#include <QVector>
#include "CanFrame.h"

class AssociativeLearner;
class LuaScriptEngine;

class OfflineAnalyzer : public QWidget {
    Q_OBJECT
public:
    explicit OfflineAnalyzer(AssociativeLearner *learner,
                             LuaScriptEngine *lua = nullptr,
                             QWidget *parent = nullptr);

public slots:
    void loadFile();
    void playPause();
    void stop();
    void setSpeed(int value);
    void nextFrame();           // NOWE: ręczne przejście do następnej ramki

protected:
    void keyPressEvent(QKeyEvent *event) override;  // przechwytywanie Enter

private:
    QVector<CanFrame> m_frames;
    int m_currentIndex = 0;
    bool m_playing = false;

    QPushButton *m_loadBtn;
    QPushButton *m_playPauseBtn;
    QPushButton *m_stopBtn;
    QPushButton *m_nextBtn;    // NOWY
    QSlider *m_speedSlider;
    QCheckBox *m_originalTimestampsCheck;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QTimer m_timer;

    AssociativeLearner *m_learner;
    LuaScriptEngine *m_luaEngine;
};
EOF

# 2. Implementacja
cat > src/core/OfflineAnalyzer.cpp << 'EOF'
#include "OfflineAnalyzer.h"
#include "AssociativeLearner.h"
#include "LuaScriptEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QKeyEvent>
#include <QRegularExpression>

OfflineAnalyzer::OfflineAnalyzer(AssociativeLearner *learner,
                                 LuaScriptEngine *lua,
                                 QWidget *parent)
    : QWidget(parent), m_learner(learner), m_luaEngine(lua) {
    setFocusPolicy(Qt::StrongFocus);   // potrzebne do przechwytywania klawiszy
    auto *layout = new QVBoxLayout(this);

    m_loadBtn = new QPushButton("📂 Wczytaj plik candump");
    layout->addWidget(m_loadBtn);

    auto *controls = new QHBoxLayout;
    m_playPauseBtn = new QPushButton("▶ Odtwarzaj");
    m_playPauseBtn->setEnabled(false);
    m_stopBtn = new QPushButton("⏹ Stop");
    m_stopBtn->setEnabled(false);
    m_nextBtn = new QPushButton("⏭ Następna ramka");
    m_nextBtn->setEnabled(false);
    controls->addWidget(m_playPauseBtn);
    controls->addWidget(m_stopBtn);
    controls->addWidget(m_nextBtn);
    layout->addLayout(controls);

    auto *speedLayout = new QHBoxLayout;
    speedLayout->addWidget(new QLabel("Prędkość:"));
    m_speedSlider = new QSlider(Qt::Horizontal);
    m_speedSlider->setRange(1, 100);
    m_speedSlider->setValue(50);
    speedLayout->addWidget(m_speedSlider);
    layout->addLayout(speedLayout);

    m_originalTimestampsCheck = new QCheckBox("Oryginalne znaczniki czasu");
    m_originalTimestampsCheck->setChecked(true);
    m_originalTimestampsCheck->setStyleSheet("color: #ff66cc; font-weight: bold;");
    layout->addWidget(m_originalTimestampsCheck);

    m_progressBar = new QProgressBar;
    m_progressBar->setMinimum(0);
    layout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Gotowy.");
    m_statusLabel->setStyleSheet("color: #c0c0c0;");
    layout->addWidget(m_statusLabel);

    connect(m_loadBtn, &QPushButton::clicked, this, &OfflineAnalyzer::loadFile);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &OfflineAnalyzer::playPause);
    connect(m_stopBtn, &QPushButton::clicked, this, &OfflineAnalyzer::stop);
    connect(m_speedSlider, &QSlider::valueChanged, this, &OfflineAnalyzer::setSpeed);
    connect(&m_timer, &QTimer::timeout, this, &OfflineAnalyzer::nextFrame);
    connect(m_nextBtn, &QPushButton::clicked, this, &OfflineAnalyzer::nextFrame);

    setStyleSheet(R"(
        QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; border-radius: 4px; padding: 6px 15px; font-weight: bold; }
        QPushButton:hover { background: #e94560; color: #0a0e17; }
        QPushButton:disabled { background: #333; color: #666; border-color: #555; }
        QSlider::groove:horizontal { background: #2a2a3c; height: 6px; border-radius: 3px; }
        QSlider::handle:horizontal { background: #e94560; width: 14px; height: 14px; margin: -4px 0; border-radius: 7px; }
    )");
}

void OfflineAnalyzer::loadFile() {
    QString path = QFileDialog::getOpenFileName(this, "Wczytaj candump", "",
                                                "Pliki candump (*.log *.txt);;Wszystkie (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Błąd", "Nie można otworzyć pliku.");
        return;
    }

    m_frames.clear();
    QTextStream in(&file);
    QString line;
    QRegularExpression re(R"(\((\d+)\)\s+\w+\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]+))");

    while (in.readLineInto(&line)) {
        auto match = re.match(line.trimmed());
        if (match.hasMatch()) {
            CanFrame f;
            f.timestamp = match.captured(1).toULongLong();
            f.id = match.captured(2).toUInt(nullptr, 16);
            QString dataHex = match.captured(3);
            f.dlc = dataHex.length() / 2;
            for (int i = 0; i < f.dlc && i < 64; ++i) {
                f.data[i] = dataHex.mid(i*2, 2).toUInt(nullptr, 16);
            }
            m_frames.append(f);
        }
    }
    file.close();

    m_progressBar->setMaximum(m_frames.size());
    m_statusLabel->setText(QString("Wczytano %1 ramek.").arg(m_frames.size()));
    m_playPauseBtn->setEnabled(true);
    m_stopBtn->setEnabled(true);
    m_nextBtn->setEnabled(true);
    m_currentIndex = 0;

    // Wyświetl info o pierwszym odstępie
    if (m_frames.size() > 1) {
        int64_t diff = m_frames[1].timestamp - m_frames[0].timestamp;
        m_statusLabel->setText(QString("Wczytano %1 ramek. Następny odstęp: %2 µs")
                               .arg(m_frames.size())
                               .arg(diff));
    }
}

void OfflineAnalyzer::playPause() {
    if (m_playing) {
        m_timer.stop();
        m_playing = false;
        m_playPauseBtn->setText("▶ Odtwarzaj");
    } else {
        if (m_currentIndex >= m_frames.size()) m_currentIndex = 0;
        m_playing = true;
        m_playPauseBtn->setText("⏸ Pauza");
        nextFrame(); // Natychmiast wyślij pierwszą ramkę
    }
}

void OfflineAnalyzer::stop() {
    m_timer.stop();
    m_playing = false;
    m_currentIndex = 0;
    m_playPauseBtn->setText("▶ Odtwarzaj");
    m_progressBar->setValue(0);
    m_statusLabel->setText("Zatrzymano.");
}

void OfflineAnalyzer::setSpeed(int value) {
    Q_UNUSED(value);
    if (m_playing) {
        m_timer.stop();
        // W trybie automatycznym timer uruchomi się sam przy następnym nextFrame
        if (m_currentIndex < m_frames.size()) {
            nextFrame();
        }
    }
}

void OfflineAnalyzer::nextFrame() {
    if (m_currentIndex >= m_frames.size()) {
        if (m_playing) {
            stop();
            m_statusLabel->setText("Odtwarzanie zakończone.");
        }
        return;
    }

    const CanFrame &frame = m_frames.at(m_currentIndex);
    if (m_learner) m_learner->processFrame(frame);
    if (m_luaEngine) m_luaEngine->onNewFrame(frame);

    m_currentIndex++;
    m_progressBar->setValue(m_currentIndex);

    // Wyświetl informację o odstępie do następnej ramki
    QString status = QString("Ramka %1 / %2").arg(m_currentIndex).arg(m_frames.size());
    if (m_currentIndex < m_frames.size()) {
        int64_t diff = m_frames[m_currentIndex].timestamp - m_frames[m_currentIndex-1].timestamp;
        status += QString(" | Odstęp: %1 µs").arg(diff);
    } else {
        status += " | Koniec";
    }
    m_statusLabel->setText(status);

    // Jeśli automatyczne odtwarzanie aktywne, ustaw timer na następną ramkę
    if (m_playing && m_currentIndex < m_frames.size()) {
        if (m_originalTimestampsCheck->isChecked() && m_currentIndex > 0 && m_currentIndex < m_frames.size()) {
            uint64_t currentTs = m_frames.at(m_currentIndex-1).timestamp;
            uint64_t nextTs = m_frames.at(m_currentIndex).timestamp;
            int64_t diff = static_cast<int64_t>(nextTs - currentTs);
            if (diff < 0) diff = 0;
            double speedFactor = m_speedSlider->value() / 100.0;
            int intervalMs = static_cast<int>(diff / 1000.0 / speedFactor);
            if (intervalMs < 1) intervalMs = 1;
            m_timer.start(intervalMs);
        } else {
            int speed = m_speedSlider->value();
            m_timer.start(qMax(1, 100 - speed));
        }
    }
}

void OfflineAnalyzer::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (!m_playing && !m_frames.isEmpty()) {
            nextFrame();  // W trybie ręcznym Enter wysyła kolejną ramkę
        }
    }
    QWidget::keyPressEvent(event);
}
EOF

echo "=== Tryb krokowy dodany. Kompiluj: cd build && make -j\$(nproc) ==="
