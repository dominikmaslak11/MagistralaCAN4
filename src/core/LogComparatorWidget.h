#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QProgressBar>
#include "CanFrame.h"

/**
 * @brief Porównywarka logów CAN – ładuje dwa pliki candump,
 *        porównuje ramki i pokazuje różnice z kolorowaniem.
 *
 * Kolory:
 *  - Zielony  = identyczne (ten sam ID i dane na tej samej pozycji)
 *  - Czerwony = różne dane przy tym samym ID i pozycji
 *  - Żółty    = tylko w lewym pliku
 *  - Niebieski = tylko w prawym pliku
 *
 * Tryby porównania:
 *  - Exact position: ramka[i] vs ramka[i]
 *  - Timestamp window: dopasowanie w oknie czasowym
 */
class LogComparatorWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogComparatorWidget(QWidget *parent = nullptr);

private slots:
    void loadLeft();
    void loadRight();
    void runCompare();
    void applyFilter();
    void exportReport();
    void onScrollSync(int value);

private:
    void setupUi();
    void updateStats();

    QVector<CanFrame> loadCandump(const QString &path);

    // --- UI ---
    QPushButton *m_loadLeftBtn;
    QPushButton *m_loadRightBtn;
    QPushButton *m_compareBtn;
    QPushButton *m_exportBtn;
    QLabel      *m_leftFileLabel;
    QLabel      *m_rightFileLabel;
    QLabel      *m_statsLabel;

    QTableWidget *m_leftTable;
    QTableWidget *m_rightTable;

    // Filtry
    QLineEdit   *m_filterId;
    QLineEdit   *m_filterTimeFrom;
    QLineEdit   *m_filterTimeTo;
    QPushButton *m_filterApplyBtn;

    // Dane
    QVector<CanFrame> m_leftFrames;
    QVector<CanFrame> m_rightFrames;

    // Wynik porównania: dla każdej pozycji w lewej tabeli
    enum DiffStatus { Identical, Different, LeftOnly, RightOnly, Skipped };
    QVector<DiffStatus> m_leftStatus;
    QVector<DiffStatus> m_rightStatus;

    // Statystyki
    int m_identical = 0;
    int m_different = 0;
    int m_leftOnly  = 0;
    int m_rightOnly = 0;
};
