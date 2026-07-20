#include "DbcEditorWidget.h"
#include "Logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <algorithm>

DbcEditorWidget::DbcEditorWidget(QWidget *parent) : QWidget(parent) {
    setupUi();
}

void DbcEditorWidget::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);

    // ── Górny toolbar ──
    auto *toolbar = new QHBoxLayout;
    m_loadBtn = new QPushButton("Wczytaj .dbc");
    m_saveBtn = new QPushButton("Zapisz .dbc");
    m_applyBtn = new QPushButton("Zastosuj do LIVE");
    toolbar->addWidget(m_loadBtn);
    toolbar->addWidget(m_saveBtn);
    toolbar->addWidget(m_applyBtn);
    toolbar->addStretch();
    mainLayout->addLayout(toolbar);

    // ── Splitter: lista wiadomości (lewo) + edytor sygnałów (prawo) ──
    auto *splitter = new QSplitter(Qt::Horizontal);

    // --- Lewy panel: lista wiadomości + przyciski ---
    auto *leftPanel = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0,0,0,0);

    auto *msgHeader = new QHBoxLayout;
    msgHeader->addWidget(new QLabel("Wiadomości:"));
    m_addMsgBtn = new QPushButton("+");
    m_addMsgBtn->setFixedWidth(40);
    m_delMsgBtn = new QPushButton("−");
    m_delMsgBtn->setFixedWidth(40);
    msgHeader->addStretch();
    msgHeader->addWidget(m_addMsgBtn);
    msgHeader->addWidget(m_delMsgBtn);
    leftLayout->addLayout(msgHeader);

    m_messageList = new QListWidget;
    m_messageList->setMinimumWidth(200);
    leftLayout->addWidget(m_messageList, 1);

    // Pola edycji wiadomości
    auto *msgEditLayout = new QHBoxLayout;
    msgEditLayout->addWidget(new QLabel("Nazwa:"));
    m_msgNameEdit = new QLineEdit;
    m_msgNameEdit->setPlaceholderText("np. EngineData");
    msgEditLayout->addWidget(m_msgNameEdit, 1);
    msgEditLayout->addWidget(new QLabel("ID:"));
    m_msgIdEdit = new QLineEdit;
    m_msgIdEdit->setPlaceholderText("hex");
    m_msgIdEdit->setMaximumWidth(70);
    msgEditLayout->addWidget(m_msgIdEdit);
    msgEditLayout->addWidget(new QLabel("DLC:"));
    m_msgDlcSpin = new QSpinBox;
    m_msgDlcSpin->setRange(0, 64);
    m_msgDlcSpin->setValue(8);
    m_msgDlcSpin->setMaximumWidth(55);
    msgEditLayout->addWidget(m_msgDlcSpin);
    leftLayout->addLayout(msgEditLayout);

    splitter->addWidget(leftPanel);

    // --- Prawy panel: tabela sygnałów ---
    auto *rightPanel = new QWidget;
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0,0,0,0);

    auto *sigHeader = new QHBoxLayout;
    sigHeader->addWidget(new QLabel("Sygnały:"));
    m_addSigBtn = new QPushButton("+ Sygnał");
    m_delSigBtn = new QPushButton("− Sygnał");
    sigHeader->addStretch();
    sigHeader->addWidget(m_addSigBtn);
    sigHeader->addWidget(m_delSigBtn);
    rightLayout->addLayout(sigHeader);

    // Tabela: Nazwa | Start | Długość | LE | Signed | Scale | Offset | Min | Max | Unit
    m_signalTable = new QTableWidget(0, 10);
    m_signalTable->setHorizontalHeaderLabels(
        {"Nazwa", "Start bit", "Długość (bit)", "LE", "Signed", "Scale", "Offset", "Min", "Max", "Unit"});
    m_signalTable->verticalHeader()->hide();
    m_signalTable->horizontalHeader()->setStretchLastSection(true);
    m_signalTable->setShowGrid(false);
    m_signalTable->setAlternatingRowColors(false);
    m_signalTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_signalTable->setSelectionMode(QAbstractItemView::SingleSelection);
    rightLayout->addWidget(m_signalTable, 1);

    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 5);
    mainLayout->addWidget(splitter, 1);

    // ── Podgląd DBC (raw text) ──
    auto *previewGroup = new QGroupBox("Podgląd DBC (tekst)");
    previewGroup->setStyleSheet(
        "QGroupBox { color: #ffaa00; font-weight: bold; border: 1px solid #e94560; "
        "border-radius: 4px; margin-top: 8px; padding-top: 16px; }");
    auto *previewLayout = new QVBoxLayout(previewGroup);
    m_previewText = new QTextEdit;
    m_previewText->setReadOnly(true);
    m_previewText->setMaximumHeight(150);
    m_previewText->setStyleSheet(
        "QTextEdit { background-color: #1a1a2e; color: #00ffaa; font-family: Consolas, monospace; "
        "font-size: 11px; border: 1px solid #2a2a3c; border-radius: 4px; }");
    previewLayout->addWidget(m_previewText);
    mainLayout->addWidget(previewGroup);

    // ── Walidacja ──
    m_validationLabel = new QLabel("Gotowy.");
    m_validationLabel->setStyleSheet("color: #00ffaa; font-weight: bold; padding: 4px;");
    mainLayout->addWidget(m_validationLabel);

    // ── Połączenia ──
    connect(m_messageList, &QListWidget::currentItemChanged, this, &DbcEditorWidget::onMessageSelected);
    connect(m_addMsgBtn, &QPushButton::clicked, this, &DbcEditorWidget::addMessage);
    connect(m_delMsgBtn, &QPushButton::clicked, this, &DbcEditorWidget::removeMessage);
    connect(m_addSigBtn, &QPushButton::clicked, this, &DbcEditorWidget::addSignal);
    connect(m_delSigBtn, &QPushButton::clicked, this, &DbcEditorWidget::removeSignal);
    connect(m_signalTable, &QTableWidget::cellChanged, this, &DbcEditorWidget::onSignalChanged);
    connect(m_applyBtn, &QPushButton::clicked, this, &DbcEditorWidget::applyToLive);
    connect(m_saveBtn, &QPushButton::clicked, this, &DbcEditorWidget::saveDbc);
    connect(m_loadBtn, &QPushButton::clicked, this, &DbcEditorWidget::loadDbcFile);

    // Aktualizuj nazwę/ID wiadomości przy edycji
    connect(m_msgNameEdit, &QLineEdit::textChanged, this, [this]() {
        if (m_currentMsgIdx >= 0 && m_currentMsgIdx < m_messages.size()) {
            m_messages[m_currentMsgIdx].name = m_msgNameEdit->text().trimmed();
            rebuildMessageList();
            refreshPreview();
        }
    });
    connect(m_msgIdEdit, &QLineEdit::textChanged, this, [this]() {
        if (m_currentMsgIdx >= 0 && m_currentMsgIdx < m_messages.size()) {
            bool ok;
            uint32_t id = m_msgIdEdit->text().toUInt(&ok, 16);
            if (ok) {
                m_messages[m_currentMsgIdx].id = id;
                rebuildMessageList();
                refreshPreview();
            }
        }
    });
    connect(m_msgDlcSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (m_currentMsgIdx >= 0 && m_currentMsgIdx < m_messages.size()) {
            m_messages[m_currentMsgIdx].dlc = val;
            refreshPreview();
        }
    });

    // Global QSS (style_dark/light.qss) handles widget styling
}

// ── Logika danych ───────────────────────────────────────────

void DbcEditorWidget::rebuildMessageList() {
    int oldRow = m_messageList->currentRow();
    m_messageList->blockSignals(true);
    m_messageList->clear();
    for (const auto &msg : m_messages) {
        m_messageList->addItem(
            QString("0x%1  %2  [DLC %3]")
                .arg(msg.id, 3, 16, QChar('0')).toUpper()
                .arg(msg.name)
                .arg(msg.dlc));
    }
    if (oldRow >= 0 && oldRow < m_messageList->count())
        m_messageList->setCurrentRow(oldRow);
    m_messageList->blockSignals(false);
}

void DbcEditorWidget::rebuildSignalTable() {
    if (m_currentMsgIdx < 0 || m_currentMsgIdx >= m_messages.size()) {
        m_signalTable->setRowCount(0);
        return;
    }
    const auto &msg = m_messages[m_currentMsgIdx];

    m_signalTable->blockSignals(true);
    m_signalTable->setRowCount(static_cast<int>(msg.sigList.size()));
    for (int i = 0; i < msg.sigList.size(); ++i) {
        const auto &s = msg.sigList[i];

        auto *nameItem = new QTableWidgetItem(s.name);
        nameItem->setForeground(QColor("#00ffaa"));
        m_signalTable->setItem(i, 0, nameItem);

        auto *startItem = new QTableWidgetItem(QString::number(s.startBit));
        m_signalTable->setItem(i, 1, startItem);

        auto *lenItem = new QTableWidgetItem(QString::number(s.length));
        m_signalTable->setItem(i, 2, lenItem);

        // Kolumna LE (checkbox w komórce)
        auto *leItem = new QTableWidgetItem;
        leItem->setCheckState(s.isLittleEndian ? Qt::Checked : Qt::Unchecked);
        leItem->setText(s.isLittleEndian ? "LE" : "BE");
        leItem->setForeground(s.isLittleEndian ? QColor("#00ffaa") : QColor("#ffaa00"));
        m_signalTable->setItem(i, 3, leItem);

        auto *signedItem = new QTableWidgetItem;
        signedItem->setCheckState(s.isSigned ? Qt::Checked : Qt::Unchecked);
        signedItem->setText(s.isSigned ? "±" : "+");
        signedItem->setForeground(s.isSigned ? QColor("#ff66cc") : QColor("#c0c0c0"));
        m_signalTable->setItem(i, 4, signedItem);

        m_signalTable->setItem(i, 5, new QTableWidgetItem(QString::number(s.scale, 'f', 4)));
        m_signalTable->setItem(i, 6, new QTableWidgetItem(QString::number(s.offset, 'f', 2)));
        m_signalTable->setItem(i, 7, new QTableWidgetItem(QString::number(s.minimum, 'f', 2)));
        m_signalTable->setItem(i, 8, new QTableWidgetItem(QString::number(s.maximum, 'f', 2)));
        m_signalTable->setItem(i, 9, new QTableWidgetItem(s.unit));
    }
    m_signalTable->blockSignals(false);
}

void DbcEditorWidget::refreshPreview() {
    m_previewText->setPlainText(DbcParser::serialize(m_messages));
    m_validationLabel->setText(validateAll());
}

QString DbcEditorWidget::validateAll() const {
    QStringList warnings;

    // Duplikaty ID
    QHash<uint32_t, int> idCount;
    for (const auto &msg : m_messages) idCount[msg.id]++;
    for (auto it = idCount.begin(); it != idCount.end(); ++it) {
        if (it.value() > 1)
            warnings.append(QString("Duplikat ID 0x%1 (%2×)").arg(it.key(), 3, 16, QChar('0')).arg(it.value()));
    }

    // Nakładające się bity
    for (const auto &msg : m_messages) {
        struct Range { int start; int end; QString name; };
        QVector<Range> ranges;
        for (const auto &s : msg.sigList) {
            ranges.append({s.startBit, s.startBit + s.length - 1, s.name});
        }
        for (int i = 0; i < ranges.size(); ++i) {
            for (int j = i+1; j < ranges.size(); ++j) {
                const auto &a = ranges[i], &b = ranges[j];
                if (a.end >= b.start && b.end >= a.start) {
                    warnings.append(QString("ID 0x%1: bity %2-%3 (%4) nachodzą na %5-%6 (%7)")
                        .arg(msg.id, 3, 16, QChar('0'))
                        .arg(a.start).arg(a.end).arg(a.name)
                        .arg(b.start).arg(b.end).arg(b.name));
                }
            }
        }
    }

    // Puste nazwy
    for (const auto &msg : m_messages) {
        if (msg.name.trimmed().isEmpty())
            warnings.append(QString("ID 0x%1: brak nazwy wiadomości").arg(msg.id, 3, 16, QChar('0')));
        for (const auto &s : msg.sigList) {
            if (s.name.trimmed().isEmpty())
                warnings.append(QString("ID 0x%1: sygnał bez nazwy").arg(msg.id, 3, 16, QChar('0')));
        }
    }

    if (warnings.isEmpty()) return "✅ Brak problemów";
    return "⚠ " + warnings.join(" | ");
}

// ── Sloty ────────────────────────────────────────────────────

void DbcEditorWidget::onMessageSelected(QListWidgetItem *item) {
    if (!item) { m_currentMsgIdx = -1; rebuildSignalTable(); return; }
    m_currentMsgIdx = m_messageList->row(item);
    if (m_currentMsgIdx < 0 || m_currentMsgIdx >= m_messages.size()) return;

    const auto &msg = m_messages[m_currentMsgIdx];
    m_msgNameEdit->setText(msg.name);
    m_msgIdEdit->setText(QString::number(msg.id, 16).toUpper());
    m_msgDlcSpin->setValue(msg.dlc);
    rebuildSignalTable();
}

void DbcEditorWidget::addMessage() {
    DbcMessage msg;
    msg.name = QString("NewMsg_%1").arg(m_messages.size());
    msg.id = m_messages.isEmpty() ? 0x100 : m_messages.last().id + 1;
    msg.dlc = 8;
    m_messages.append(msg);
    rebuildMessageList();
    m_messageList->setCurrentRow(static_cast<int>(m_messages.size()) - 1);
    refreshPreview();
}

void DbcEditorWidget::removeMessage() {
    if (m_currentMsgIdx < 0 || m_currentMsgIdx >= m_messages.size()) return;
    m_messages.removeAt(m_currentMsgIdx);
    m_currentMsgIdx = -1;
    rebuildMessageList();
    rebuildSignalTable();
    refreshPreview();
}

void DbcEditorWidget::addSignal() {
    if (m_currentMsgIdx < 0 || m_currentMsgIdx >= m_messages.size()) return;
    DbcSignal sig;
    sig.name = QString("Sig_%1").arg(m_messages[m_currentMsgIdx].sigList.size());
    sig.startBit = m_messages[m_currentMsgIdx].sigList.isEmpty()
        ? 0 : m_messages[m_currentMsgIdx].sigList.last().startBit
              + m_messages[m_currentMsgIdx].sigList.last().length;
    sig.length = 8;
    sig.isLittleEndian = true;
    sig.isSigned = false;
    sig.scale = 1.0;
    sig.offset = 0.0;
    sig.minimum = 0.0;
    sig.maximum = 255.0;
    sig.unit = "";
    m_messages[m_currentMsgIdx].sigList.append(sig);
    rebuildSignalTable();
    refreshPreview();
}

void DbcEditorWidget::removeSignal() {
    int row = m_signalTable->currentRow();
    if (m_currentMsgIdx < 0 || row < 0) return;
    auto &sigList = m_messages[m_currentMsgIdx].sigList;
    if (row >= sigList.size()) return;
    sigList.removeAt(row);
    rebuildSignalTable();
    refreshPreview();
}

void DbcEditorWidget::onSignalChanged(int row, int col) {
    if (m_currentMsgIdx < 0 || m_currentMsgIdx >= m_messages.size()) return;
    auto &sigList = m_messages[m_currentMsgIdx].sigList;
    if (row >= sigList.size()) return;

    auto *item = m_signalTable->item(row, col);
    if (!item) return;

    auto &sig = sigList[row];
    bool ok;

    switch (col) {
    case 0: sig.name = item->text().trimmed(); break;
    case 1: {
        int v = item->text().toInt(&ok);
        if (ok) sig.startBit = v;
        break;
    }
    case 2: {
        int v = item->text().toInt(&ok);
        if (ok && v > 0) sig.length = v;
        break;
    }
    case 3:
        sig.isLittleEndian = (item->checkState() == Qt::Checked);
        item->setText(sig.isLittleEndian ? "LE" : "BE");
        item->setForeground(sig.isLittleEndian ? QColor("#00ffaa") : QColor("#ffaa00"));
        break;
    case 4:
        sig.isSigned = (item->checkState() == Qt::Checked);
        item->setText(sig.isSigned ? "±" : "+");
        item->setForeground(sig.isSigned ? QColor("#ff66cc") : QColor("#c0c0c0"));
        break;
    case 5: sig.scale  = item->text().toDouble(&ok); if (!ok) sig.scale = 1.0; break;
    case 6: sig.offset = item->text().toDouble(&ok); if (!ok) sig.offset = 0.0; break;
    case 7: sig.minimum = item->text().toDouble(&ok); if (!ok) sig.minimum = 0.0; break;
    case 8: sig.maximum = item->text().toDouble(&ok); if (!ok) sig.maximum = 255.0; break;
    case 9: sig.unit = item->text().trimmed(); break;
    }
    refreshPreview();
}

void DbcEditorWidget::applyToLive() {
    m_parser.setMessages(m_messages);
    emit dbcApplied();
    m_validationLabel->setText("✅ Zastosowano do LIVE – dashboard i szczegóły ramek odświeżone.");
    Logger::log("Edytor DBC: zastosowano zmiany do LIVE");
}

void DbcEditorWidget::saveDbc() {
    QString path = QFileDialog::getSaveFileName(this, "Zapisz DBC", "", "Pliki DBC (*.dbc)");
    if (path.isEmpty()) return;
    m_parser.setMessages(m_messages);
    if (m_parser.save(path)) {
        m_validationLabel->setText(QString("✅ Zapisano: %1").arg(path));
        Logger::log(QString("Edytor DBC: zapisano %1").arg(path));
    } else {
        QMessageBox::warning(this, "Błąd", "Nie udało się zapisać pliku.");
    }
}

void DbcEditorWidget::loadDbcFile() {
    QString path = QFileDialog::getOpenFileName(this, "Wczytaj DBC", "", "Pliki DBC (*.dbc)");
    if (path.isEmpty()) return;
    loadDbc(path);
}

void DbcEditorWidget::loadDbc(const QString &path) {
    DbcParser tmp;
    if (!tmp.load(path)) {
        QMessageBox::warning(this, "Błąd", "Nie udało się wczytać pliku DBC.");
        return;
    }
    m_messages = tmp.messages();
    m_currentMsgIdx = -1;
    rebuildMessageList();
    rebuildSignalTable();
    refreshPreview();
    m_validationLabel->setText(QString("✅ Wczytano: %1 (%2 wiadomości)").arg(path).arg(m_messages.size()));
    Logger::log(QString("Edytor DBC: wczytano %1").arg(path));
}

void DbcEditorWidget::setMessages(const QVector<DbcMessage> &msgs) {
    m_messages = msgs;
    m_currentMsgIdx = -1;
    rebuildMessageList();
    rebuildSignalTable();
    refreshPreview();
    m_validationLabel->setText(QString("✅ Zaimportowano %1 wiadomości z Auto-Generatora").arg(m_messages.size()));
}
