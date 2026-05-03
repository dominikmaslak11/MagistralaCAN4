#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QTableView>
#include <QHash>
#include <deque>
#include "CanFrame.h"
#include "CandidateModel.h"
#include "GpuCorrelator.h"

struct EventRecord {
    QVector<CanFrame> windowFrames;
    // Mapa CAN ID -> wektor cech (zwrócony przez buildFeatureVectors)
    QHash<uint32_t, QVector<float>> idFeatures;
};

class AssociativeLearner : public QWidget {
    Q_OBJECT
public:
    explicit AssociativeLearner(QWidget *parent = nullptr);
    ~AssociativeLearner() override;

public slots:
    void processFrame(const CanFrame &frame);
    void markEvent();
    void resetLearning();

signals:
    void eventMarked(int iteration);

private:
    void updateCandidates();
    QHash<uint32_t, QVector<float>> buildFeatureVectors(const QVector<CanFrame> &window);

    static constexpr int64_t WINDOW_BEFORE = 500000;
    static constexpr int64_t WINDOW_AFTER  = 200000;

    QPushButton *m_markEventBtn;
    QPushButton *m_resetBtn;
    QLabel      *m_iterationLabel;
    QTableView  *m_candidatesView;

    CandidateModel *m_candidateModel;
    GpuCorrelator   m_correlator;

    std::deque<CanFrame> m_frameHistory;
    static constexpr int HISTORY_MAX = 10000;

    QVector<EventRecord> m_events;
    int m_iteration = 0;
};
