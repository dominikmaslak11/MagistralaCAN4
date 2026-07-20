#pragma once
#include <QWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTextEdit>
#include "DbcParser.h"

/**
 * @brief Zakładka edytora DBC.
 *
 * Umożliwia tworzenie i edycję plików .dbc (Vector CANdb++):
 *  - lista wiadomości (QListWidget)
 *  - tabela sygnałów dla zaznaczonej wiadomości (QTableWidget)
 *  - dodawanie/usuwanie/edycja wiadomości i sygnałów
 *  - walidacja: nakładające się bity, duplikaty ID
 *  - podgląd LIVE wartości sygnału
 *  - import / eksport .dbc
 *  - Zastosuj → podmiana w DbcParser (widoczne w Dashboardzie i Szczegółach)
 */
class DbcEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit DbcEditorWidget(QWidget *parent = nullptr);

    /// Ładuje plik .dbc do edytora.
    void loadDbc(const QString &path);

    /// Zwraca aktualny parser (do podpięcia pod dashboard).
    const DbcParser *parser() const { return &m_parser; }

    /// Importuje wiadomości z zewnątrz (np. z DbcAutoWidget).
    void setMessages(const QVector<DbcMessage> &msgs);

signals:
    /// Emitowany gdy użytkownik kliknie "Zastosuj" – dashboard powinien się odświeżyć.
    void dbcApplied();

private slots:
    void onMessageSelected(QListWidgetItem *item);
    void addMessage();
    void removeMessage();
    void addSignal();
    void removeSignal();
    void onSignalChanged(int row, int col);
    void applyToLive();
    void saveDbc();
    void loadDbcFile();
    void refreshPreview();

private:
    void setupUi();
    void rebuildMessageList();
    void rebuildSignalTable();
    QString validateAll() const;

    // --- UI ---
    QListWidget   *m_messageList{};
    QTableWidget  *m_signalTable{};
    QLineEdit     *m_msgNameEdit{};
    QLineEdit     *m_msgIdEdit{};
    QSpinBox      *m_msgDlcSpin{};
    QPushButton   *m_addMsgBtn{};
    QPushButton   *m_delMsgBtn{};
    QPushButton   *m_addSigBtn{};
    QPushButton   *m_delSigBtn{};
    QPushButton   *m_applyBtn{};
    QPushButton   *m_saveBtn{};
    QPushButton   *m_loadBtn{};
    QLabel        *m_validationLabel{};
    QTextEdit     *m_previewText{};

    // --- Dane ---
    DbcParser           m_parser;
    QVector<DbcMessage> m_messages;  // robocza kopia
    int                 m_currentMsgIdx = -1;
};
