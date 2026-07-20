#include "CandidateModel.h"
#include <QColor>

CandidateModel::CandidateModel(QObject *parent) : QAbstractTableModel(parent) {}

int CandidateModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_candidates.size());
}

int CandidateModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return Column::Count;
}

QVariant CandidateModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_candidates.size())
        return {};

    const Candidate &cand = m_candidates.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Column::ID:          return QString("0x%1").arg(cand.canId, 3, 16, QChar('0')).toUpper();
        case Column::DESCRIPTION: return cand.description;
        case Column::SCORE:       return QString("%1 %").arg(cand.score * 100.0f, 0, 'f', 1);
        case Column::OCCURRENCES: return cand.occurrenceCount;
        }
    } else if (role == Qt::ForegroundRole) {
        if (index.column() == Column::SCORE) {
            return (cand.score > 0.8f) ? QColor("#00ffaa") : QColor("#ffaa00");
        }
        return QColor("#c0c0c0");
    } else if (role == Qt::BackgroundRole) {
        return (index.row() % 2) ? QColor("#0d1117") : QColor("#161b22");
    } else if (role == Qt::TextAlignmentRole) {
        return Qt::AlignCenter;
    }
    return {};
}

QVariant CandidateModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case Column::ID:          return "CAN ID";
        case Column::DESCRIPTION: return "Opis";
        case Column::SCORE:       return "Pewność";
        case Column::OCCURRENCES: return "Wystąpienia";
        }
    }
    return {};
}

void CandidateModel::setCandidates(const QVector<Candidate> &candidates) {
    beginResetModel();
    m_candidates = candidates;
    endResetModel();
}

void CandidateModel::clear() {
    beginResetModel();
    m_candidates.clear();
    endResetModel();
}
