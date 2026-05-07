#pragma once
#include <QWidget>
#include <QTableView>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QAbstractTableModel>
#include <QVector>
#include "J1939Frame.h"
#include "J1939Parser.h"

/**
 * @brief Model tabeli dla zakładki J1939.
 */
class J1939TableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { TIMESTAMP, PRIO, PGN, PGN_NAME, SA, DA, DLC, DATA, _COUNT };

    explicit J1939TableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation o, int role = Qt::DisplayRole) const override;

    void addFrame(const J1939Frame &frame);
    void clear();
    J1939Frame frameAt(int row) const;

    void setParser(const J1939Parser *parser) { m_parser = parser; }

private:
    QVector<J1939Frame> m_frames;
    const J1939Parser *m_parser = nullptr;
    static constexpr int MAX_ROWS = 5000;
};

/**
 * @brief Zakładka diagnostyczna J1939.
 *
 * Wyświetla tabelę ramek J1939 oraz szczegóły SPN dla zaznaczonej ramki.
 * Umożliwia filtrowanie po PGN i adresie źródłowym.
 */
class J1939Widget : public QWidget {
    Q_OBJECT
public:
    explicit J1939Widget(QWidget *parent = nullptr);

public slots:
    void processFrame(const CanFrame &frame);
    void onFrameSelected(const QModelIndex &index);
    void applyFilters();

private:
    void setupUi();

    J1939Parser m_parser;
    J1939TableModel *m_model;
    QTableView *m_tableView;

    // Filtry
    QComboBox *m_pgnFilter;
    QLineEdit *m_saFilter;
    QCheckBox *m_filterEnabled;
    QPushButton *m_clearBtn;
    QLabel *m_statusLabel;

    // Panel szczegółów
    QLabel *m_detailLabel;

    // Stan filtrów
    uint32_t m_filterPgn = 0;
    uint8_t  m_filterSa  = 0xFF;
    bool     m_filterActive = false;
};
