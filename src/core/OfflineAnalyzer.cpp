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
    setFocusPolicy(Qt::StrongFocus);
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

    // ── Binary Search section ───────────────────────────────
    auto *bsLayout = new QHBoxLayout;
    m_binarySearchBtn = new QPushButton("🔍 Wyszukiwanie binarne zdarzenia");
    m_binarySearchBtn->setEnabled(false);
    m_binarySearchBtn->setStyleSheet(
        "QPushButton { background: #e94560; color: #0a0e17; font-size: 14px; "
        "border: 2px solid #ff66cc; border-radius: 6px; padding: 8px 20px; font-weight: bold; }"
        "QPushButton:hover { background: #ff66cc; }"
        "QPushButton:disabled { background: #333; color: #666; border-color: #555; }");
    bsLayout->addWidget(m_binarySearchBtn);

    m_bsRangeLabel = new QLabel("");
    m_bsRangeLabel->setStyleSheet("color: #ffaa00; font-weight: bold; font-size: 12px;");
    bsLayout->addWidget(m_bsRangeLabel);
    bsLayout->addStretch();
    layout->addLayout(bsLayout);

    // ── Frame log ──────────────────────────────────────────
    auto *logLabel = new QLabel("📋 Ostatnie ramki (przed pauzą):");
    logLabel->setStyleSheet("color: #00ffaa; font-weight: bold; margin-top: 8px;");
    layout->addWidget(logLabel);

    m_frameLog = new QListWidget;
    m_frameLog->setMaximumHeight(400);
    m_frameLog->setStyleSheet(
        "QListWidget { background: #0a0e17; color: #c0c0c0; border: 1px solid #2a2a3c; "
        "font-family: 'Consolas', monospace; font-size: 11px; }"
        "QListWidget::item { padding: 2px 4px; border-bottom: 1px solid #1a1a2e; }");
    layout->addWidget(m_frameLog);

    // ── Connections ────────────────────────────────────────
    connect(m_loadBtn, &QPushButton::clicked, this, &OfflineAnalyzer::loadFile);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &OfflineAnalyzer::playPause);
    connect(m_stopBtn, &QPushButton::clicked, this, &OfflineAnalyzer::stop);
    connect(m_speedSlider, &QSlider::valueChanged, this, &OfflineAnalyzer::setSpeed);
    connect(&m_timer, &QTimer::timeout, this, &OfflineAnalyzer::nextFrame);
    connect(m_nextBtn, &QPushButton::clicked, this, &OfflineAnalyzer::nextFrame);
    connect(m_binarySearchBtn, &QPushButton::clicked, this, &OfflineAnalyzer::startBinarySearch);

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
    m_frameLog->clear();
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
    m_binarySearchBtn->setEnabled(true);
    m_currentIndex = 0;
    m_bsState = BsState::Idle;

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
        nextFrame();
    }
}

void OfflineAnalyzer::stop() {
    m_timer.stop();
    m_playing = false;
    m_currentIndex = 0;
    m_bsState = BsState::Idle;
    m_playPauseBtn->setText("▶ Odtwarzaj");
    m_progressBar->setValue(0);
    m_bsRangeLabel->setText("");
    m_statusLabel->setText("Zatrzymano.");
}

void OfflineAnalyzer::setSpeed(int value) {
    Q_UNUSED(value);
    if (m_playing) {
        m_timer.stop();
        if (m_currentIndex < m_frames.size()) {
            nextFrame();
        }
    }
}

void OfflineAnalyzer::addFrameToLog(const CanFrame &frame) {
    // Format: "timestamp ID:0xXXX DLC:N DATA:XX XX ..."
    QString entry = QString("[%1] ID:0x%2 DLC:%3  ")
        .arg(frame.timestamp)
        .arg(frame.id, 3, 16, QChar('0'))
        .arg(frame.dlc);

    for (int i = 0; i < frame.dlc && i < 8; ++i)
        entry += QString("%1 ").arg(frame.data[i], 2, 16, QChar('0')).toUpper();

    m_frameLog->addItem(entry.trimmed());

    // Trim to MAX_LOG_ENTRIES
    while (m_frameLog->count() > MAX_LOG_ENTRIES)
        delete m_frameLog->takeItem(0);

    // Scroll to bottom
    m_frameLog->scrollToBottom();
}

void OfflineAnalyzer::nextFrame() {
    if (m_currentIndex >= m_frames.size()) {
        if (m_playing) {
            stop();
            m_statusLabel->setText("Odtwarzanie zakończone.");
        }

        // Binary search: half finished playing
        if (m_bsState == BsState::PlayingHalf) {
            m_bsState = BsState::AwaitingResponse;
            m_playing = false;
            m_playPauseBtn->setText("▶ Odtwarzaj");

            // Ask user
            int rangeSize = m_bsRight - m_bsLeft + 1;
            QString msg;
            if (m_bsPlayingFirstHalf)
                msg = QString("Odtworzono PIERWSZĄ połowę zakresu [%1–%2] (%3 ramek).\n\n"
                              "Czy zjawisko WYSTĄPIŁO?")
                          .arg(m_bsLeft).arg(m_bsMid).arg(rangeSize / 2);
            else
                msg = QString("Odtworzono DRUGĄ połowę zakresu [%1–%2] (%3 ramek).\n\n"
                              "Czy zjawisko WYSTĄPIŁO?")
                          .arg(m_bsMid + 1).arg(m_bsRight).arg(rangeSize - rangeSize / 2);

            auto answer = QMessageBox::question(
                this, "Wyszukiwanie binarne — pytanie", msg,
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

            onBinarySearchResponse(answer == QMessageBox::Yes);
        }
        return;
    }

    const CanFrame &frame = m_frames.at(m_currentIndex);

    // Always log frame for binary search context
    addFrameToLog(frame);

    if (m_learner) m_learner->processFrame(frame);
    if (m_luaEngine) m_luaEngine->onNewFrame(frame);

    // Binary search: check if we reached the end of the current half
    if (m_bsState == BsState::PlayingHalf && m_currentIndex >= m_bsPlayEnd - 1) {
        // We'll stop after this frame; signal via m_currentIndex >= m_frames.size() on next call
        m_currentIndex = m_frames.size(); // force end-of-file on next nextFrame()
    } else {
        m_currentIndex++;
    }

    m_progressBar->setValue(m_currentIndex >= m_frames.size() ? m_frames.size() : m_currentIndex);

    QString status = QString("Ramka %1 / %2").arg(m_currentIndex).arg(m_frames.size());
    if (m_bsState == BsState::PlayingHalf) {
        status += QString(" | BS: [%1–%2]").arg(m_bsLeft).arg(m_bsRight);
    }
    if (m_currentIndex < m_frames.size()) {
        int64_t diff = m_frames[m_currentIndex].timestamp - m_frames[m_currentIndex-1].timestamp;
        status += QString(" | Odstęp: %1 µs").arg(diff);
    } else {
        status += " | Koniec";
    }
    m_statusLabel->setText(status);

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
            nextFrame();
        }
    }
    QWidget::keyPressEvent(event);
}

// ── Binary Search implementation ────────────────────────────

void OfflineAnalyzer::startBinarySearch() {
    if (m_frames.isEmpty()) return;

    // Reset binary search state
    m_bsLeft = 0;
    m_bsRight = static_cast<int>(m_frames.size()) - 1;
    m_bsState = BsState::PlayingHalf;
    m_bsPlayingFirstHalf = true;
    m_currentIndex = 0;
    m_frameLog->clear();

    m_bsRangeLabel->setText(QString("Szukam w [0–%1]").arg(m_bsRight));

    bsPlayHalf();
}

void OfflineAnalyzer::bsPlayHalf() {
    int rangeSize = m_bsRight - m_bsLeft + 1;
    if (rangeSize <= 1) {
        // FOUND: single frame
        m_bsState = BsState::Found;
        m_bsRangeLabel->setText(QString("✅ Znaleziono! Ramka indeks=%1").arg(m_bsLeft));

        // Show the frame
        const CanFrame &f = m_frames.at(m_bsLeft);
        QString msg = QString("Znaleziono ramkę:\n\n"
                              "Indeks: %1\n"
                              "Timestamp: %2 µs\n"
                              "ID: 0x%3\n"
                              "DLC: %4\n"
                              "Data: %5")
            .arg(m_bsLeft)
            .arg(f.timestamp)
            .arg(f.id, 3, 16, QChar('0'))
            .arg(f.dlc);

        QString dataStr;
        for (int i = 0; i < f.dlc && i < 8; ++i)
            dataStr += QString("%1 ").arg(f.data[i], 2, 16, QChar('0')).toUpper();
        msg = msg.arg(dataStr.trimmed());

        QMessageBox::information(this, "Wyszukiwanie binarne — znaleziono", msg);
        return;
    }

    m_bsMid = m_bsLeft + (m_bsRight - m_bsLeft) / 2;

    if (m_bsPlayingFirstHalf) {
        // Play first half: [bsLeft, bsMid]
        m_currentIndex = m_bsLeft;
        m_bsPlayEnd = m_bsMid + 1; // exclusive
        m_bsRangeLabel->setText(QString("▶ 1-sza połowa: [%1–%2] z [%3–%4]")
                                .arg(m_bsLeft).arg(m_bsMid).arg(m_bsLeft).arg(m_bsRight));
    } else {
        // Play second half: [bsMid+1, bsRight]
        m_currentIndex = m_bsMid + 1;
        m_bsPlayEnd = m_bsRight + 1; // exclusive
        m_bsRangeLabel->setText(QString("▶ 2-ga połowa: [%1–%2] z [%3–%4]")
                                .arg(m_bsMid + 1).arg(m_bsRight).arg(m_bsLeft).arg(m_bsRight));
    }

    m_playing = true;
    m_bsState = BsState::PlayingHalf;
    m_playPauseBtn->setText("⏸ Pauza");
    nextFrame();
}

void OfflineAnalyzer::onBinarySearchResponse(bool phenomenonOccurred) {
    if (phenomenonOccurred) {
        // Zjawisko wystąpiło — zawęź do pierwszej połowy
        if (m_bsPlayingFirstHalf) {
            m_bsRight = m_bsMid;
            m_bsPlayingFirstHalf = true;
        } else {
            // In second-half mode, this shouldn't normally happen
            // but handle gracefully: narrow to the second half range
            m_bsLeft = m_bsMid + 1;
            m_bsPlayingFirstHalf = true;
        }
    } else {
        // Zjawisko NIE wystąpiło — przeszukaj drugą połowę
        if (m_bsPlayingFirstHalf) {
            m_bsLeft = m_bsMid + 1;
            m_bsPlayingFirstHalf = true;  // play first half of the new range
        } else {
            // Second half also didn't have it — shouldn't happen with proper user input
            m_bsRight = m_bsMid;
            m_bsPlayingFirstHalf = true;
        }
    }

    bsPlayHalf();
}
