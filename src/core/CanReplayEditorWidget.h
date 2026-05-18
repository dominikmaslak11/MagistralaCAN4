#pragma once
#include <QWidget>
#include "CanPlayer.h"
#include "CanReplayEditorModel.h"
#include "CanFrame.h"

class QTableView;
class QPushButton;
class QComboBox;
class QLabel;
class QSlider;
class QLineEdit;

/// Widget edytora replay: tabela ramek z edycją ID/DLC/payload przed odtwarzaniem.
class CanReplayEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanReplayEditorWidget(QWidget *parent = nullptr);

signals:
    /// Emitowane zamiast frameEmitted z wewnętrznego CanPlayer — podpinamy do MainWindow::onNewFrame.
    void frameReady(const CanFrame &frame);

private slots:
    void loadFile();
    void togglePlayback();
    void stopPlayback();
    void onPositionChanged(int current, int total);
    void onPlaybackFinished();
    void onSpeedChanged(int index);
    void resetEdits();
    void onSliderMoved(int value);
    void filterRows(const QString &text);

private:
    void setupUi();
    void updatePlayButton();

    CanPlayer            m_player;
    CanReplayEditorModel m_model;

    QTableView  *m_table     = nullptr;
    QPushButton *m_loadBtn   = nullptr;
    QPushButton *m_playBtn   = nullptr;
    QPushButton *m_stopBtn   = nullptr;
    QPushButton *m_resetBtn  = nullptr;
    QComboBox   *m_speedCombo = nullptr;
    QLabel      *m_posLabel  = nullptr;
    QSlider     *m_slider    = nullptr;
    QLineEdit   *m_filterEdit = nullptr;

    bool m_sliderDragging = false;
};
