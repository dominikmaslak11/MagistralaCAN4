#pragma once
#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QHash>
#include <QTimer>
#include "CanFrame.h"
#include "DbcParser.h"

class CanDashboard : public QWidget {
    Q_OBJECT
public:
    explicit CanDashboard(QWidget *parent = nullptr);

    void setDbcParser(const DbcParser *parser);
    void updateSignal(const CanFrame &frame);

private:
    void buildPanels();

    QGridLayout *m_grid;
    const DbcParser *m_dbcParser = nullptr;

    // mapa: "EngineSpeed" -> QLabel
    QHash<QString, QLabel*> m_valueLabels;
    // mapa: CAN ID -> lista nazw sygnałów
    QHash<uint32_t, QStringList> m_idToSignals;
};
