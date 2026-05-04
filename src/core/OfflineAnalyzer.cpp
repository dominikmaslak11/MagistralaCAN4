#include "OfflineAnalyzer.h"
#include "AssociativeLearner.h"
#include "LuaScriptEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QRegularExpression>

OfflineAnalyzer::OfflineAnalyzer(AssociativeLearner *learner,
                                 LuaScriptEngine *lua,
                                 QWidget *parent)
    : QWidget(parent), m_learner(learner), m_luaEngine(lua) {
    auto *layout = new QVBoxLayout(this);

    m_loadBtn = new QPushButton("📂 Wczytaj plik candump");
    layout->addWidget(m_loadBtn);

    auto *controls = new QHBoxLayout;
    m_playPauseBtn = new QPushButton("▶ Odtwarzaj");
    m_playPauseBtn->setEnabled(false);
    m_stopBtn = new QPushButton("⏹ Stop");
    m_stopBtn->setEnabled(false);
    controls->addWidget(m_playPauseBtn);
    controls->addWidget(m_stopBtn);
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
    connect(&m_timer, &QTimer::timeout, this, &OfflineAnalyzer::playNextFrame);

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
    m_currentIndex = 0;
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

        if (m_originalTimestampsCheck->isChecked() && m_frames.size() > 1 && m_currentIndex < m_frames.size()) {
            // Tryb z oryginalnymi timestampami: oblicz odstęp do następnej ramki
            uint64_t currentTs = m_frames.at(m_currentIndex).timestamp;
            uint64_t nextTs = (m_currentIndex + 1 < m_frames.size())
                               ? m_frames.at(m_currentIndex + 1).timestamp
                               : currentTs;
            int64_t diff = static_cast<int64_t>(nextTs - currentTs);
            if (diff < 0) diff = 0;

            // Skaluj przez prędkość (odwrotnie: suwak 1 → 100x wolniej, 100 → normalnie)
            double speedFactor = m_speedSlider->value() / 100.0;
            int intervalMs = static_cast<int>(diff / 1000.0 / speedFactor);  // diff w µs → ms
            if (intervalMs < 1) intervalMs = 1;

            m_timer.start(intervalMs);
        } else {
            // Stały interwał (jak poprzednio)
            int speed = m_speedSlider->value();
            m_timer.start(qMax(1, 100 - speed));
        }
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
        // Restart timera z nową prędkością
        m_timer.stop();
        if (m_originalTimestampsCheck->isChecked() && m_currentIndex > 0 && m_currentIndex < m_frames.size()) {
            uint64_t currentTs = m_frames.at(m_currentIndex).timestamp;
            uint64_t nextTs = (m_currentIndex + 1 < m_frames.size())
                               ? m_frames.at(m_currentIndex + 1).timestamp
                               : currentTs;
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

void OfflineAnalyzer::playNextFrame() {
    if (m_currentIndex >= m_frames.size()) {
        stop();
        m_statusLabel->setText("Odtwarzanie zakończone.");
        return;
    }

    const CanFrame &frame = m_frames.at(m_currentIndex);
    if (m_learner) m_learner->processFrame(frame);
    if (m_luaEngine) m_luaEngine->onNewFrame(frame);

    m_currentIndex++;
    m_progressBar->setValue(m_currentIndex);
    m_statusLabel->setText(QString("Ramka %1 / %2").arg(m_currentIndex).arg(m_frames.size()));

    // Przygotuj timer na następną ramkę, jeśli gra i używa oryginalnych timestampów
    if (m_playing && m_currentIndex < m_frames.size() && m_originalTimestampsCheck->isChecked()) {
        m_timer.stop();  // zatrzymaj obecny timer

        uint64_t currentTs = m_frames.at(m_currentIndex).timestamp;
        uint64_t nextTs = (m_currentIndex + 1 < m_frames.size())
                           ? m_frames.at(m_currentIndex + 1).timestamp
                           : currentTs;
        int64_t diff = static_cast<int64_t>(nextTs - currentTs);
        if (diff < 0) diff = 0;
        double speedFactor = m_speedSlider->value() / 100.0;
        int intervalMs = static_cast<int>(diff / 1000.0 / speedFactor);
        if (intervalMs < 1) intervalMs = 1;

        m_timer.start(intervalMs);
    }
    // Jeśli nie używa oryginalnych timestampów, timer już leci ze stałym interwałem
}
