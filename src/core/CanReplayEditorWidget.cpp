#include "CanReplayEditorWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QTableView>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QSortFilterProxyModel>

CanReplayEditorWidget::CanReplayEditorWidget(QWidget *parent)
    : QWidget(parent), m_model(&m_player)
{
    setupUi();

    connect(&m_player, &CanPlayer::frameEmitted,     this, &CanReplayEditorWidget::frameReady);
    connect(&m_player, &CanPlayer::positionChanged,  this, &CanReplayEditorWidget::onPositionChanged);
    connect(&m_player, &CanPlayer::playbackFinished, this, &CanReplayEditorWidget::onPlaybackFinished);
}

void CanReplayEditorWidget::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // ── Pasek górny: Wczytaj / Reset / Filtr ──────────────────────────────
    auto *topBar = new QHBoxLayout;
    m_loadBtn  = new QPushButton("Wczytaj nagranie (.mcan)");
    m_resetBtn = new QPushButton("Reset edycji");
    m_filterEdit = new QLineEdit;
    m_filterEdit->setPlaceholderText("Filtruj po ID (hex)...");
    m_filterEdit->setMaximumWidth(180);

    topBar->addWidget(m_loadBtn);
    topBar->addWidget(m_resetBtn);
    topBar->addStretch();
    topBar->addWidget(new QLabel("Filtr:"));
    topBar->addWidget(m_filterEdit);
    root->addLayout(topBar);

    // ── Tabela ──────────────────────────────────────────────────────────────
    m_table = new QTableView;
    m_table->setModel(&m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::AnyKeyPressed);
    m_table->setColumnWidth(CanReplayEditorModel::ColNum,       40);
    m_table->setColumnWidth(CanReplayEditorModel::ColEnabled,   60);
    m_table->setColumnWidth(CanReplayEditorModel::ColTimestamp, 100);
    m_table->setColumnWidth(CanReplayEditorModel::ColId,        80);
    m_table->setColumnWidth(CanReplayEditorModel::ColExt,       40);
    m_table->setColumnWidth(CanReplayEditorModel::ColDlc,       40);
    root->addWidget(m_table, 1);

    // ── Suwak postępu ───────────────────────────────────────────────────────
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setMinimum(0);
    m_slider->setMaximum(0);
    m_slider->setEnabled(false);
    root->addWidget(m_slider);

    // ── Pasek dolny: Play/Stop/Speed/Pozycja ────────────────────────────────
    auto *ctrlBar = new QHBoxLayout;
    m_playBtn = new QPushButton("▶ Odtwórz");
    m_stopBtn = new QPushButton("■ Stop");
    m_speedCombo = new QComboBox;
    m_speedCombo->addItems({"0.5x", "1x", "2x", "5x", "10x", "Max"});
    m_speedCombo->setCurrentIndex(1);
    m_posLabel = new QLabel("0 / 0");

    m_playBtn->setEnabled(false);
    m_stopBtn->setEnabled(false);

    ctrlBar->addWidget(m_playBtn);
    ctrlBar->addWidget(m_stopBtn);
    ctrlBar->addSpacing(12);
    ctrlBar->addWidget(new QLabel("Prędkość:"));
    ctrlBar->addWidget(m_speedCombo);
    ctrlBar->addStretch();
    ctrlBar->addWidget(m_posLabel);
    root->addLayout(ctrlBar);

    // ── Połączenia ──────────────────────────────────────────────────────────
    connect(m_loadBtn,   &QPushButton::clicked,  this, &CanReplayEditorWidget::loadFile);
    connect(m_resetBtn,  &QPushButton::clicked,  this, &CanReplayEditorWidget::resetEdits);
    connect(m_playBtn,   &QPushButton::clicked,  this, &CanReplayEditorWidget::togglePlayback);
    connect(m_stopBtn,   &QPushButton::clicked,  this, &CanReplayEditorWidget::stopPlayback);
    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CanReplayEditorWidget::onSpeedChanged);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &CanReplayEditorWidget::filterRows);
    connect(m_slider, &QSlider::sliderPressed,  this, [this]{ m_sliderDragging = true; });
    connect(m_slider, &QSlider::sliderReleased, this, [this]{
        m_sliderDragging = false;
        onSliderMoved(m_slider->value());
    });
}

void CanReplayEditorWidget::loadFile() {
    QString path = QFileDialog::getOpenFileName(
        this, "Wczytaj nagranie CAN", "",
        "Nagrania (*.mcan *.mcan.zst);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;

    int count = m_player.loadFile(path);
    if (count <= 0) {
        QMessageBox::warning(this, "Błąd", "Nie udało się wczytać nagrania.");
        return;
    }

    m_model.reload();
    m_slider->setMaximum(count - 1);
    m_slider->setValue(0);
    m_slider->setEnabled(true);
    m_posLabel->setText(QString("0 / %1").arg(count));
    m_playBtn->setEnabled(true);
    m_stopBtn->setEnabled(true);
    onSpeedChanged(m_speedCombo->currentIndex());
}

void CanReplayEditorWidget::togglePlayback() {
    if (m_player.isPlaying()) {
        m_player.pause();
    } else {
        m_player.play();
    }
    updatePlayButton();
}

void CanReplayEditorWidget::stopPlayback() {
    m_player.stop();
    m_slider->setValue(0);
    updatePlayButton();
}

void CanReplayEditorWidget::updatePlayButton() {
    m_playBtn->setText(m_player.isPlaying() ? "⏸ Pauza" : "▶ Odtwórz");
}

void CanReplayEditorWidget::onPositionChanged(int current, int total) {
    m_posLabel->setText(QString("%1 / %2").arg(current).arg(total));
    if (!m_sliderDragging && total > 0)
        m_slider->setValue(current);
}

void CanReplayEditorWidget::onPlaybackFinished() {
    updatePlayButton();
}

void CanReplayEditorWidget::onSpeedChanged(int index) {
    static const float speeds[] = { 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 0.0f };
    if (index >= 0 && index < 6)
        m_player.setSpeed(speeds[index]);
}

void CanReplayEditorWidget::resetEdits() {
    m_model.resetAllEdits();
}

void CanReplayEditorWidget::onSliderMoved(int value) {
    bool wasPlaying = m_player.isPlaying();
    if (wasPlaying) m_player.pause();
    m_player.seekTo(value);
    if (wasPlaying) m_player.play();
}

void CanReplayEditorWidget::filterRows(const QString &text) {
    // Prosta implementacja: iteruj przez model i ukryj wiersze nie pasujące do filtra hex ID
    QString filter = text.trimmed().toUpper();
    for (int row = 0; row < m_model.rowCount(); ++row) {
        bool show = filter.isEmpty();
        if (!show) {
            CanFrame f = m_player.effectiveFrameAt(row);
            QString idHex = QString("%1").arg(f.id, 0, 16).toUpper();
            show = idHex.contains(filter);
        }
        m_table->setRowHidden(row, !show);
    }
}
