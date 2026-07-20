#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QSplitter>
#include "CanFrame.h"

class LogComparatorWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogComparatorWidget(QWidget *parent = nullptr);

    // Public for unit testing
    static QVector<CanFrame> parseCandumpContent(const QString &content);

private slots:
    void loadLeft();
    void loadRight();
    void runCompare();
    void applyFilter();
    void exportReport();
    void onScrollSync(int value);
    void onModeChanged(int index);

private:
    enum CompareMode { ModePositional, ModeById };
    enum DiffStatus  { Identical, Different, LeftOnly, RightOnly, Skipped };

    struct IdSummary {
        uint32_t id{};
        int leftCount  = 0;
        int rightCount = 0;
        int diffCount  = 0;
    };

    void setupUi();
    void runPositional();
    void runById();
    void fillTable(QTableWidget *tbl, const QVector<CanFrame> &frames,
                   const QVector<DiffStatus> &status,
                   const QVector<QVector<bool>> &changedBytes);
    void updateStats();
    void fillIdSummaryTable();

    QVector<CanFrame> loadCandump(const QString &path);
    static QString dataHex(const CanFrame &f, int highlightByte = -1);

    // ── UI ──
    QPushButton  *m_loadLeftBtn{};
    QPushButton  *m_loadRightBtn{};
    QPushButton  *m_compareBtn{};
    QPushButton  *m_exportBtn{};
    QLabel       *m_leftFileLabel{};
    QLabel       *m_rightFileLabel{};
    QLabel       *m_statsLabel{};
    QComboBox    *m_modeCombo{};
    QCheckBox    *m_highlightBytesChk{};

    QTableWidget *m_leftTable{};
    QTableWidget *m_rightTable{};
    QTableWidget *m_idSummaryTable{};

    QLineEdit    *m_filterId{};
    QLineEdit    *m_filterTimeFrom{};
    QLineEdit    *m_filterTimeTo{};
    QPushButton  *m_filterApplyBtn{};

    // ── Dane ──
    QVector<CanFrame> m_leftFrames;
    QVector<CanFrame> m_rightFrames;

    QVector<DiffStatus>         m_leftStatus;
    QVector<DiffStatus>         m_rightStatus;
    QVector<QVector<bool>>      m_leftChangedBytes;
    QVector<QVector<bool>>      m_rightChangedBytes;
    QVector<IdSummary>          m_idSummary;

    int m_identical = 0;
    int m_different = 0;
    int m_leftOnly  = 0;
    int m_rightOnly = 0;
};
