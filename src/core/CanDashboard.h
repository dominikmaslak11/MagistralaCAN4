#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QHash>
#include <QTimer>
#include <QPushButton>
#include <QFrame>
#include "CanFrame.h"
#include "DbcParser.h"
#include "CanGaugeWidget.h"

/**
 * @brief Nowoczesny dashboard CAN z grupowaniem ECU, wyszukiwarką
 *        i wskaźnikami graficznymi (bargraphy, digital).
 *
 * Funkcje:
 *  - Grupowanie sygnałów wg CAN ID (zwijane sekcje)
 *  - Wyszukiwarka sygnałów
 *  - Wskaźniki CanGaugeWidget (3 style: Bar, Digital, Compact)
 *  - Min/max obserwowane per sygnał
 *  - Staleness detection (szarzeje po 2s braku aktualizacji)
 */
class CanDashboard : public QWidget {
    Q_OBJECT
public:
    explicit CanDashboard(QWidget *parent = nullptr);

    void setDbcParser(const DbcParser *parser);
    void updateSignal(const CanFrame &frame);

private:
    void rebuildDashboard();
    void applyFilter(const QString &text);
    void onStalenessCheck();

    // Layout
    QVBoxLayout *m_mainLayout{};
    QScrollArea *m_scrollArea;
    QWidget      *m_scrollWidget;
    QVBoxLayout *m_groupsLayout;
    QLineEdit   *m_searchBox;
    QPushButton *m_columnsBtn;
    int           m_columns = 3;

    const DbcParser *m_dbcParser = nullptr;

    // Mapowanie: ID → lista nazw sygnałów
    QHash<uint32_t, QStringList> m_idToSignals;

    // Mapowanie: "EngineSpeed" → wskaźnik
    QHash<QString, CanGaugeWidget *> m_gauges;

    // Grupy (sekcje) – nagłówek + kontener
    struct GroupWidgets {
        QPushButton *header{};
        QFrame      *container{};
        bool         collapsed = false;
    };
    QHash<uint32_t, GroupWidgets> m_groups;

    // Staleness timer
    QTimer m_staleTimer;

    // Sygnały DBC
    QVector<DbcMessage> m_messages;
};
