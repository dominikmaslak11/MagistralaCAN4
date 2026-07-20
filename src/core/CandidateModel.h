#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include <QString>

struct Candidate {
    uint32_t canId = 0;
    QString   description;       // opis (np. "ID + bajty")
    float     score = 0.0f;      // prawdopodobieństwo (0..1)
    int       occurrenceCount = 0;
};

class CandidateModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ID, DESCRIPTION, SCORE, OCCURRENCES, Count };

    explicit CandidateModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setCandidates(const QVector<Candidate> &candidates);
    void clear();

private:
    QVector<Candidate> m_candidates;
};
