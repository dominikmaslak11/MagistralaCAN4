#include "CanSignalMapperWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QPushButton>
#include <QHeaderView>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <algorithm>

CanSignalMapperWidget::CanSignalMapperWidget(QWidget *parent)
    : QWidget(parent)
    , m_model(new CanSignalMapperModel(this))
{
    setupUi();
}

void CanSignalMapperWidget::processFrame(const CanFrame &frame) {
    m_model->processFrame(frame);
}

void CanSignalMapperWidget::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // ── Info label ────────────────────────────────────────────────────────────
    auto *info = new QLabel("Ręczne mapowanie sygnałów CAN bez pliku DBC. "
                            "Dodaj sygnał i obserwuj wartości live z magistrali.");
    info->setWordWrap(true);
    info->setStyleSheet("color: #aaa; font-size: 11px; padding: 2px 4px;");
    root->addWidget(info);

    // ── Toolbar ───────────────────────────────────────────────────────────────
    auto *toolbar = new QHBoxLayout;

    auto *addBtn  = new QPushButton("+ Dodaj sygnał");
    auto *delBtn  = new QPushButton("− Usuń");
    auto *saveBtn = new QPushButton("Zapisz…");
    auto *loadBtn = new QPushButton("Wczytaj…");

    addBtn ->setMaximumWidth(130);
    delBtn ->setMaximumWidth(90);
    saveBtn->setMaximumWidth(90);
    loadBtn->setMaximumWidth(90);

    connect(addBtn,  &QPushButton::clicked, this, &CanSignalMapperWidget::addSignalDialog);
    connect(delBtn,  &QPushButton::clicked, this, &CanSignalMapperWidget::removeSelected);
    connect(saveBtn, &QPushButton::clicked, this, &CanSignalMapperWidget::saveToFile);
    connect(loadBtn, &QPushButton::clicked, this, &CanSignalMapperWidget::loadFromFile);

    toolbar->addWidget(addBtn);
    toolbar->addWidget(delBtn);
    toolbar->addStretch();
    toolbar->addWidget(saveBtn);
    toolbar->addWidget(loadBtn);
    root->addLayout(toolbar);

    // ── Table ─────────────────────────────────────────────────────────────────
    m_table = new QTableView;
    m_table->setModel(m_model);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

    using C = CanSignalMapperModel;
    m_table->setColumnWidth(C::ColName,     120);
    m_table->setColumnWidth(C::ColId,        70);
    m_table->setColumnWidth(C::ColStartBit,  65);
    m_table->setColumnWidth(C::ColLength,    55);
    m_table->setColumnWidth(C::ColEndian,    55);
    m_table->setColumnWidth(C::ColSigned,    70);
    m_table->setColumnWidth(C::ColScale,     65);
    m_table->setColumnWidth(C::ColOffset,    65);
    m_table->setColumnWidth(C::ColUnit,      70);

    root->addWidget(m_table, 1);
}

// ── "Add signal" dialog ───────────────────────────────────────────────────────

void CanSignalMapperWidget::addSignalDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("Dodaj sygnał CAN");
    dlg.setMinimumWidth(320);

    auto *form = new QFormLayout(&dlg);
    form->setContentsMargins(12, 12, 12, 8);
    form->setSpacing(8);

    auto *nameEdit   = new QLineEdit("Sygnał");
    auto *idEdit     = new QLineEdit("0x7FF");
    idEdit->setPlaceholderText("hex, np. 0x1A0");

    auto *startSpin  = new QSpinBox;
    startSpin->setRange(0, 511);
    startSpin->setValue(0);

    auto *lenSpin    = new QSpinBox;
    lenSpin->setRange(1, 64);
    lenSpin->setValue(8);

    auto *endianCmb  = new QComboBox;
    endianCmb->addItem("LE — Intel (little-endian)");
    endianCmb->addItem("BE — Motorola (big-endian)");

    auto *signedChk  = new QCheckBox;

    auto *scaleSpin  = new QDoubleSpinBox;
    scaleSpin->setRange(-1e9, 1e9);
    scaleSpin->setDecimals(6);
    scaleSpin->setSingleStep(0.001);
    scaleSpin->setValue(1.0);

    auto *offsetSpin = new QDoubleSpinBox;
    offsetSpin->setRange(-1e9, 1e9);
    offsetSpin->setDecimals(6);
    offsetSpin->setSingleStep(1.0);
    offsetSpin->setValue(0.0);

    auto *unitEdit   = new QLineEdit;
    unitEdit->setPlaceholderText("np. km/h, V, °C");

    form->addRow("Nazwa:",           nameEdit);
    form->addRow("CAN ID (hex):",    idEdit);
    form->addRow("Bit startowy:",    startSpin);
    form->addRow("Długość (bity):",  lenSpin);
    form->addRow("Endianness:",      endianCmb);
    form->addRow("Ze znakiem:",      signedChk);
    form->addRow("Współczynnik scale:", scaleSpin);
    form->addRow("Offset:",          offsetSpin);
    form->addRow("Jednostka:",       unitEdit);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btns);

    if (dlg.exec() != QDialog::Accepted) return;

    bool ok = false;
    QString idText = idEdit->text().trimmed();
    uint32_t id = idText.toUInt(&ok, 16);
    if (!ok) {
        QMessageBox::warning(this, "Błąd", "Nieprawidłowy CAN ID — podaj wartość hex (np. 0x1A0).");
        return;
    }

    MapperSignal sig;
    sig.name           = nameEdit->text().trimmed().isEmpty() ? "Sygnał" : nameEdit->text().trimmed();
    sig.canId          = id;
    sig.startBit       = startSpin->value();
    sig.length         = lenSpin->value();
    sig.isLittleEndian = (endianCmb->currentIndex() == 0);
    sig.isSigned       = signedChk->isChecked();
    sig.scale          = scaleSpin->value();
    sig.offset         = offsetSpin->value();
    sig.unit           = unitEdit->text().trimmed();

    m_model->addSignal(sig);
}

// ── Remove / save / load ──────────────────────────────────────────────────────

void CanSignalMapperWidget::removeSelected() {
    auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    QVector<int> rowNums;
    rowNums.reserve(rows.size());
    for (const auto &idx : rows) rowNums.append(idx.row());
    std::sort(rowNums.begin(), rowNums.end(), std::greater<int>());
    for (int r : rowNums) m_model->removeSignal(r);
}

void CanSignalMapperWidget::saveToFile() {
    QString path = QFileDialog::getSaveFileName(this,
        "Zapisz mapowanie sygnałów", {},
        "Signal Map (*.smap);;Wszystkie pliki (*.*)");
    if (path.isEmpty()) return;
    if (!m_model->saveToFile(path))
        QMessageBox::warning(this, "Błąd", "Nie można zapisać pliku mapowania.");
}

void CanSignalMapperWidget::loadFromFile() {
    QString path = QFileDialog::getOpenFileName(this,
        "Wczytaj mapowanie sygnałów", {},
        "Signal Map (*.smap);;Wszystkie pliki (*.*)");
    if (path.isEmpty()) return;
    if (!m_model->loadFromFile(path))
        QMessageBox::warning(this, "Błąd", "Nie można wczytać pliku mapowania — nieprawidłowy format.");
}
