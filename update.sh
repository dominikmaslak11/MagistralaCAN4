#!/usr/bin/env bash
# add_tray_notifications.sh – tray, powiadomienia, jednolity design przycisków
set -e

echo "=== Dodawanie tray i powiadomień ==="

# 1. AssociativeLearner.h – dodajemy sygnał anomalyDetected
sed -i '/void eventMarked(int iteration);/a\    void anomalyDetected();' src/core/AssociativeLearner.h

# 2. AssociativeLearner.cpp – emitujemy sygnał w checkAnomaly() oraz checkAutoEvent()
sed -i '/m_anomalyTable->setItem(row,2,new QTableWidgetItem("Anomalia wykryta"));/a\        emit anomalyDetected();' src/core/AssociativeLearner.cpp
sed -i '/m_autoEventLabel->setText("Ostatnie auto-zdarzenie: OK");/a\        emit anomalyDetected();' src/core/AssociativeLearner.cpp

# 3. MainWindow.h – dodajemy QSystemTrayIcon oraz sloty
sed -i '/#include "core\/OfflineAnalyzer.h"/a #include <QSystemTrayIcon>' src/gui/MainWindow.h

# W klasie dodajemy składowe i sloty
sed -i '/private slots:/i\    void showTrayNotification(const QString &title, const QString &message);\
    void trayActivated(QSystemTrayIcon::ActivationReason reason);' src/gui/MainWindow.h

sed -i '/QLabel \*m_statusLabel;/a\    QSystemTrayIcon *m_trayIcon = nullptr;' src/gui/MainWindow.h

# 4. MainWindow.cpp – implementacja tray i powiadomień
sed -i '/#include <QTextStream>/a #include <QMenu>' src/gui/MainWindow.cpp
sed -i '/#include <QTextStream>/a #include <QSystemTrayIcon>' src/gui/MainWindow.cpp
sed -i '/#include <QTextStream>/a #include <QStyle>' src/gui/MainWindow.cpp

# Po utworzeniu m_offlineAnalyzer w konstruktorze, tworzymy tray
sed -i '/m_offlineAnalyzer = new OfflineAnalyzer/a\
    // --- System tray ---\
    m_trayIcon = new QSystemTrayIcon(this);\
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));\
    m_trayIcon->setToolTip("MagistralaCAN4");\
    auto *trayMenu = new QMenu(this);\
    trayMenu->addAction("Przywróć", this, &QMainWindow::show);\
    trayMenu->addAction("Zamknij", qApp, &QApplication::quit);\
    m_trayIcon->setContextMenu(trayMenu);\
    m_trayIcon->show();\
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::trayActivated);' src/gui/MainWindow.cpp

# Podłącz powiadomienia do sygnałów AssociativeLearner
sed -i '/connect(m_tableView, &QTableView::clicked, this, &MainWindow::onFrameSelected);/i\
    connect(m_learner, &AssociativeLearner::eventMarked, this, [this](int iteration) {\
        showTrayNotification("Zdarzenie", QString("Zarejestrowano zdarzenie #%1").arg(iteration));\
    });\
    connect(m_learner, &AssociativeLearner::anomalyDetected, this, [this]() {\
        showTrayNotification("Anomalia", "Wykryto anomalię na magistrali!");\
    });' src/gui/MainWindow.cpp

# Dodaj implementacje slotów (na końcu pliku, przed końcową klamrą klasy? Lepiej wstawić po setupStyle)
# Wstawiamy po definicji setupCentralWidget()
sed -i '/^void MainWindow::setupCentralWidget/,/^}/!b; /^}/a\
\
void MainWindow::showTrayNotification(const QString &title, const QString &message) {\
    if (m_trayIcon) m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 5000);\
}\
\
void MainWindow::trayActivated(QSystemTrayIcon::ActivationReason reason) {\
    if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {\
        show();\
        activateWindow();\
    }\
}' src/gui/MainWindow.cpp

# 5. Ujednolicenie stylu wszystkich przycisków (w setupStyle)
# Zastępujemy dotychczasowy arkusz CSS rozszerzoną wersją z osobnym stylem QToolButton
sed -i '/void MainWindow::setupStyle/,/^}/{
    /qApp->setStyleSheet/,/    )");/{
        # Usuwamy starą regułę QToolButton#clearButton (jeśli istnieje) i dodajemy ogólną
        /QToolButton#clearButton/,/QToolButton#clearButton:pressed/d
        s/QPushButton {/QPushButton, QToolButton {/
        s/QPushButton:hover/QPushButton:hover, QToolButton:hover/
        s/QPushButton:pressed/QPushButton:pressed, QToolButton:pressed/
    }
}' src/gui/MainWindow.cpp

# Upewniamy się, że w arkuszu nie ma już żadnego QToolButton#clearButton (zostały usunięte)
# Dla pewności możemy jeszcze raz wyczyścić
sed -i '/QToolButton#clearButton/,/}/d' src/gui/MainWindow.cpp

echo "=== Tray i powiadomienia dodane. Kompiluj: cd build && make -j\$(nproc) ==="
