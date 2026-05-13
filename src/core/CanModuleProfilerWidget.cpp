#include "CanModuleProfilerWidget.h"
#include "CanAlertEngine.h"
#include "CanPeriodicSender.h"
#include "CanSniffer.h"
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QListWidget>
#include <QLabel>
#include <QTextEdit>
#include <QFont>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

CanModuleProfilerWidget::CanModuleProfilerWidget(CanSniffer *sniffer, QWidget *parent)
    : QWidget(parent)
{
    if (sniffer)
        m_simSender = new CanPeriodicSender(sniffer, this);

    m_profiler = new CanModuleProfiler(this);
    connect(m_profiler, &CanModuleProfiler::learningProgress,
            this, &CanModuleProfilerWidget::onLearningProgress);
    connect(m_profiler, &CanModuleProfiler::learningFinished,
            this, &CanModuleProfilerWidget::onLearningFinished);
    connect(m_profiler, &CanModuleProfiler::moduleOffline,
            this, &CanModuleProfilerWidget::onModuleOffline);
    connect(m_profiler, &CanModuleProfiler::moduleOnline,
            this, &CanModuleProfilerWidget::onModuleOnline);

    // ── Learning group ─────────────────────────────────────────────────────
    auto *learnGroup  = new QGroupBox("Uczenie profilu modułu");
    auto *learnLayout = new QGridLayout(learnGroup);

    learnLayout->addWidget(new QLabel("Nazwa modułu:"), 0, 0);
    m_nameEdit = new QLineEdit("Moduł CAN");
    learnLayout->addWidget(m_nameEdit, 0, 1);

    learnLayout->addWidget(new QLabel("Czas uczenia (s):"), 1, 0);
    m_durationSpin = new QSpinBox;
    m_durationSpin->setRange(2, 60);
    m_durationSpin->setValue(8);
    learnLayout->addWidget(m_durationSpin, 1, 1);

    auto *learnBtns = new QHBoxLayout;
    m_learnBtn      = new QPushButton("▶ Start uczenia");
    m_cancelLearnBtn = new QPushButton("✕ Anuluj");
    m_cancelLearnBtn->setEnabled(false);
    learnBtns->addWidget(m_learnBtn);
    learnBtns->addWidget(m_cancelLearnBtn);
    learnLayout->addLayout(learnBtns, 2, 0, 1, 2);

    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setVisible(false);
    learnLayout->addWidget(m_progressBar, 3, 0, 1, 2);

    // ── Profile list group ─────────────────────────────────────────────────
    auto *profileGroup  = new QGroupBox("Wyuczone profile");
    auto *profileLayout = new QVBoxLayout(profileGroup);

    m_profileList = new QListWidget;
    m_profileList->setFixedHeight(100);
    profileLayout->addWidget(m_profileList);

    auto *actionRow1 = new QHBoxLayout;
    m_detectBtn     = new QPushButton("▶ Wykrywaj");
    m_stopDetectBtn = new QPushButton("■ Stop wykrywania");
    m_simulateBtn   = new QPushButton("▶ Symuluj");
    m_stopSimBtn    = new QPushButton("■ Stop symulacji");
    m_stopDetectBtn->setEnabled(false);
    m_stopSimBtn->setEnabled(false);
    if (!m_simSender) {
        m_simulateBtn->setEnabled(false);
        m_simulateBtn->setToolTip("Podłącz sniffer CAN żeby włączyć symulację");
    }
    actionRow1->addWidget(m_detectBtn);
    actionRow1->addWidget(m_stopDetectBtn);
    actionRow1->addWidget(m_simulateBtn);
    actionRow1->addWidget(m_stopSimBtn);
    profileLayout->addLayout(actionRow1);

    auto *actionRow2 = new QHBoxLayout;
    m_deleteBtn = new QPushButton("Usuń profil");
    m_saveBtn   = new QPushButton("Zapisz JSON");
    m_loadBtn   = new QPushButton("Wczytaj JSON");
    actionRow2->addWidget(m_deleteBtn);
    actionRow2->addStretch();
    actionRow2->addWidget(m_saveBtn);
    actionRow2->addWidget(m_loadBtn);
    profileLayout->addLayout(actionRow2);

    // ── Status label ───────────────────────────────────────────────────────
    m_statusLabel = new QLabel("Status: —");
    m_statusLabel->setStyleSheet("font-weight: bold; padding: 4px; "
                                  "border: 1px solid #555; border-radius: 3px;");

    // ── Details view ───────────────────────────────────────────────────────
    auto *detailGroup  = new QGroupBox("Szczegóły wybranego profilu");
    auto *detailLayout = new QVBoxLayout(detailGroup);
    m_detailsView = new QTextEdit;
    m_detailsView->setReadOnly(true);
    m_detailsView->setFont(QFont("Courier New", 9));
    m_detailsView->setFixedHeight(180);
    detailLayout->addWidget(m_detailsView);

    // ── Main layout ────────────────────────────────────────────────────────
    auto *main = new QVBoxLayout(this);
    main->addWidget(learnGroup);
    main->addWidget(profileGroup);
    main->addWidget(m_statusLabel);
    main->addWidget(detailGroup);
    main->addStretch();

    // ── Signal connections ─────────────────────────────────────────────────
    connect(m_learnBtn,       &QPushButton::clicked, this, &CanModuleProfilerWidget::onStartLearning);
    connect(m_cancelLearnBtn, &QPushButton::clicked, this, &CanModuleProfilerWidget::onCancelLearning);
    connect(m_detectBtn,      &QPushButton::clicked, this, &CanModuleProfilerWidget::onStartDetecting);
    connect(m_stopDetectBtn,  &QPushButton::clicked, this, &CanModuleProfilerWidget::onStopDetecting);
    connect(m_simulateBtn,    &QPushButton::clicked, this, &CanModuleProfilerWidget::onStartSimulation);
    connect(m_stopSimBtn,     &QPushButton::clicked, this, &CanModuleProfilerWidget::onStopSimulation);
    connect(m_deleteBtn,      &QPushButton::clicked, this, &CanModuleProfilerWidget::onDeleteProfile);
    connect(m_saveBtn,        &QPushButton::clicked, this, &CanModuleProfilerWidget::onSaveProfiles);
    connect(m_loadBtn,        &QPushButton::clicked, this, &CanModuleProfilerWidget::onLoadProfiles);
    connect(m_profileList, &QListWidget::currentRowChanged,
            this, [this](int) { onProfileSelectionChanged(); });
}

void CanModuleProfilerWidget::onFrame(const CanFrame &frame, uint64_t tsUs) {
    if (tsUs == 0)
        tsUs = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000;
    m_profiler->feed(frame, tsUs);
}

// ── Learning ──────────────────────────────────────────────────────────────────

void CanModuleProfilerWidget::onStartLearning() {
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) name = "Moduł CAN";
    int durationMs = m_durationSpin->value() * 1000;

    m_learnBtn->setEnabled(false);
    m_cancelLearnBtn->setEnabled(true);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
    m_statusLabel->setText(QString("Status: uczenie profilu '%1'...").arg(name));

    m_profiler->startLearning(name, durationMs);
}

void CanModuleProfilerWidget::onCancelLearning() {
    m_profiler->cancelLearning();
    m_learnBtn->setEnabled(true);
    m_cancelLearnBtn->setEnabled(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText("Status: uczenie anulowane");
}

void CanModuleProfilerWidget::onLearningProgress(int pct) {
    m_progressBar->setValue(pct);
}

void CanModuleProfilerWidget::onLearningFinished(const ModuleProfile &profile) {
    m_learnBtn->setEnabled(true);
    m_cancelLearnBtn->setEnabled(false);
    m_progressBar->setVisible(false);

    m_profiles.append(profile);
    updateProfileList();
    m_profileList->setCurrentRow(m_profiles.size() - 1);

    int nIds = profile.periodicIds.size();
    int nRr  = profile.reqResPairs.size();
    m_statusLabel->setText(
        QString("Status: uczenie gotowe — '%1' (%2 ID cyklicznych, %3 par req/resp)")
            .arg(profile.name).arg(nIds).arg(nRr));
}

// ── Detection ─────────────────────────────────────────────────────────────────

void CanModuleProfilerWidget::onStartDetecting() {
    int row = m_profileList->currentRow();
    if (row < 0 || row >= m_profiles.size()) {
        QMessageBox::warning(this, "Brak profilu", "Wybierz profil z listy.");
        return;
    }
    const ModuleProfile &prof = m_profiles[row];
    if (prof.isEmpty()) {
        QMessageBox::warning(this, "Pusty profil",
                             "Wybrany profil nie zawiera cyklicznych ID — brak co wykrywać.");
        return;
    }
    m_profiler->startDetecting(prof);
    m_detectBtn->setEnabled(false);
    m_stopDetectBtn->setEnabled(true);
    m_statusLabel->setText(QString("Status: ● ONLINE — '%1'").arg(prof.name));
    m_statusLabel->setStyleSheet("font-weight: bold; padding: 4px; color: #00cc44; "
                                  "border: 1px solid #555; border-radius: 3px;");
}

void CanModuleProfilerWidget::onStopDetecting() {
    m_profiler->stopDetecting();
    m_detectBtn->setEnabled(true);
    m_stopDetectBtn->setEnabled(false);
    m_statusLabel->setText("Status: wykrywanie zatrzymane");
    m_statusLabel->setStyleSheet("font-weight: bold; padding: 4px; "
                                  "border: 1px solid #555; border-radius: 3px;");
}

void CanModuleProfilerWidget::onModuleOffline(const QString &name, uint32_t id) {
    m_statusLabel->setText(
        QString("Status: ● OFFLINE — '%1' (brak 0x%2)").arg(name).arg(id, 0, 16));
    m_statusLabel->setStyleSheet("font-weight: bold; padding: 4px; color: #ff4444; "
                                  "border: 1px solid #555; border-radius: 3px;");

    if (m_alertEngine) {
        CanAlert alert;
        alert.ruleName    = "ModuleOffline";
        alert.description = QString("Moduł '%1': brak ramki 0x%2").arg(name).arg(id, 0, 16);
        CanFrame f{};
        f.id              = id;
        alert.frame       = f;
        alert.timestampUs = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000;
        m_alertEngine->submitExternalAlert(alert);
    }
}

void CanModuleProfilerWidget::onModuleOnline(const QString &name) {
    m_statusLabel->setText(QString("Status: ● ONLINE — '%1'").arg(name));
    m_statusLabel->setStyleSheet("font-weight: bold; padding: 4px; color: #00cc44; "
                                  "border: 1px solid #555; border-radius: 3px;");
}

// ── Simulation ────────────────────────────────────────────────────────────────

void CanModuleProfilerWidget::onStartSimulation() {
    if (!m_simSender) return;
    int row = m_profileList->currentRow();
    if (row < 0 || row >= m_profiles.size()) return;

    const ModuleProfile &prof = m_profiles[row];
    if (prof.periodicIds.isEmpty()) {
        QMessageBox::warning(this, "Brak ID", "Profil nie zawiera cyklicznych ID do symulacji.");
        return;
    }

    m_simSender->clearAll();
    for (const auto &pid : prof.periodicIds) {
        PeriodicEntry e;
        e.frame.id       = pid.id;
        e.frame.extended = pid.extended;
        e.frame.dlc      = 8;
        e.periodMs       = std::max(1, static_cast<int>(pid.avgIntervalMs));
        e.label          = QString("SIM 0x%1").arg(pid.id, 0, 16).toStdString();
        m_simSender->addEntry(e);
    }
    m_simSender->start();

    m_simulateBtn->setEnabled(false);
    m_stopSimBtn->setEnabled(true);
    m_statusLabel->setText(
        QString("Status: symulacja '%1' (%2 ID)").arg(prof.name).arg(prof.periodicIds.size()));
}

void CanModuleProfilerWidget::onStopSimulation() {
    if (m_simSender) {
        m_simSender->stop();
        m_simSender->clearAll();
    }
    m_simulateBtn->setEnabled(m_simSender != nullptr);
    m_stopSimBtn->setEnabled(false);
}

// ── Profile list management ───────────────────────────────────────────────────

void CanModuleProfilerWidget::onProfileSelectionChanged() {
    int row = m_profileList->currentRow();
    if (row >= 0 && row < m_profiles.size())
        updateDetails(m_profiles[row]);
    else
        m_detailsView->clear();
}

void CanModuleProfilerWidget::onDeleteProfile() {
    int row = m_profileList->currentRow();
    if (row < 0 || row >= m_profiles.size()) return;
    m_profiles.remove(row);
    updateProfileList();
}

void CanModuleProfilerWidget::onSaveProfiles() {
    QString path = QFileDialog::getSaveFileName(
        this, "Zapisz profile modułów", QString(), "JSON (*.json)");
    if (path.isEmpty()) return;

    QJsonArray arr;
    for (const auto &p : m_profiles)
        arr.append(p.toJson());

    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson());
    } else {
        QMessageBox::warning(this, "Błąd", "Nie można zapisać pliku: " + path);
    }
}

void CanModuleProfilerWidget::onLoadProfiles() {
    QString path = QFileDialog::getOpenFileName(
        this, "Wczytaj profile modułów", QString(), "JSON (*.json)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Błąd", "Nie można otworzyć pliku: " + path);
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) {
        QMessageBox::warning(this, "Błąd", "Nieprawidłowy format JSON.");
        return;
    }
    m_profiles.clear();
    for (const auto &v : doc.array())
        m_profiles.append(ModuleProfile::fromJson(v.toObject()));
    updateProfileList();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void CanModuleProfilerWidget::updateProfileList() {
    m_profileList->clear();
    for (const auto &p : m_profiles) {
        QString label = QString("%1  (%2 ID cykl., %3 req/resp)")
                            .arg(p.name)
                            .arg(p.periodicIds.size())
                            .arg(p.reqResPairs.size());
        m_profileList->addItem(label);
    }
}

void CanModuleProfilerWidget::updateDetails(const ModuleProfile &p) {
    QString txt;
    txt += QString("Profil: %1\n").arg(p.name);
    txt += QString("Czas uczenia: %1 s\n\n").arg(p.learnDurationMs / 1000.0, 0, 'f', 1);

    txt += "── Cykliczne ID (heartbeat) ─────────────────\n";
    if (p.periodicIds.isEmpty()) {
        txt += "  (brak)\n";
    } else {
        for (const auto &pid : p.periodicIds) {
            txt += QString("  0x%1  avg=%2 ms  jitter=%3 ms  frames=%4\n")
                       .arg(pid.id, 0, 16)
                       .arg(pid.avgIntervalMs, 0, 'f', 2)
                       .arg(pid.jitterMs, 0, 'f', 2)
                       .arg(pid.frameCount);
        }
    }

    txt += "\n── Request/Response ──────────────────────────\n";
    if (p.reqResPairs.isEmpty()) {
        txt += "  (nie wykryto)\n";
    } else {
        for (const auto &rr : p.reqResPairs) {
            txt += QString("  0x%1 → 0x%2  latency=%3 ms  n=%4\n")
                       .arg(rr.reqId, 0, 16)
                       .arg(rr.respId, 0, 16)
                       .arg(rr.avgLatencyMs, 0, 'f', 1)
                       .arg(rr.occurrences);
        }
    }

    m_detailsView->setPlainText(txt);
}
