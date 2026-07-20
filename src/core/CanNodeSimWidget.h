#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include "CanNodeSimulator.h"

class CanSniffer;
class LuaScriptEngine;

/**
 * @brief Zakładka "Symulacja CAN" – zarządzanie węzłami.
 *
 * Umożliwia tworzenie, edycję i monitorowanie symulowanych węzłów CAN.
 * Każdy węzeł reaguje na zadany trigger (ID + opcjonalne dane)
 * i wysyła odpowiedź (statyczną lub przez Lua).
 */
class CanNodeSimWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanNodeSimWidget(CanSniffer *sniffer,
                              LuaScriptEngine *lua = nullptr,
                              QWidget *parent = nullptr);

    CanNodeSimulator *simulator() { return &m_simulator; }

private slots:
    void addNode();
    void removeNode();
    void onNodeSelected(int row, int col);
    void onCellChanged(int row, int col);
    void loadConfig();
    void saveConfig();
    void refreshTable();

private:
    void setupUi();
    void populateRow(int row, const CanNodeDefinition &node);
    CanNodeDefinition nodeFromRow(int row) const;

    CanNodeSimulator  m_simulator;
    CanSniffer       *m_sniffer;
    LuaScriptEngine  *m_lua;
    QTableWidget     *m_nodeTable{};
    QPushButton      *m_addBtn{};
    QPushButton      *m_delBtn{};
    QPushButton      *m_loadBtn{};
    QPushButton      *m_saveBtn{};
    QLabel           *m_statusLabel{};

    enum Column {
        COL_ENABLED, COL_NAME, COL_TRIG_ID, COL_TRIG_DATA,
        COL_RESP_ID, COL_RESP_DATA, COL_DELAY, COL_MATCHES,
        COL_RESPONSES, Count
    };
};
