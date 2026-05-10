#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVector>
#include <QSet>
#include <QHash>
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <cstdint>

/**
 * @brief Panel statystyk CAN — filtr ID, liczniki ramek, FPS, burst detection, mini-wykres.
 *
 * Wydzielony z MainWindow w ramach Fazy 2.2 modernizacji.
 * Emituje sygnały: filterChanged (dla MainWindow), pauseToggled, statsUpdated (dla REST server).
 */
class CanStatsPanel : public QWidget {
    Q_OBJECT
public:
    explicit CanStatsPanel(QWidget *parent = nullptr);

    /// Wywołaj dla każdej nowej ramki (akumuluje statystyki per-ID).
    void onNewFrame(uint32_t canId, uint64_t timestamp);

    /// Czy pauza aktywna (ramki buforowane, tabela nieodświeżana).
    bool isPaused() const { return m_paused; }

    /// Zeruje liczniki i statystyki.
    void reset();

signals:
    /// Emitowany gdy użytkownik zmieni tekst filtra ID.
    void filterChanged(const QString &text);

    /// Emitowany przy przełączeniu pauzy.
    void pauseToggled(bool paused);

    /// Emitowany co 500ms z aktualnymi statystykami (dla REST server itp.).
    void statsUpdated(double fps, int uniqueIds);

private slots:
    void updateStats();
    void togglePause();

private:
    // UI
    QLineEdit   *m_filterEdit;
    QLabel      *m_statsLabel;
    QPushButton *m_pauseBtn;
    QChartView  *m_fpsChartView;
    QChart      *m_fpsChart;
    QLineSeries *m_fpsSeries;
    QTimer       m_timer;

    // Stan
    uint64_t      m_totalFrameCount = 0;
    uint64_t      m_lastStatsFrameCount = 0;
    QSet<uint32_t> m_uniqueIds;
    bool           m_paused = false;
    int            m_fpsHistoryCount = 0;

    // Per-ID
    struct IdStats { uint64_t count = 0; uint64_t lastTs = 0; double avgInterval = 0; };
    QHash<uint32_t, IdStats> m_idStats;

    // Łączny zbiór wszystkich unikalnych ID od początku sesji (nigdy nie czyszczony)
    QSet<uint32_t> m_totalUniqueIds;

    // Burst detection
    QVector<double> m_fpsWindow;
    double          m_fpsMean = 0.0;
    double          m_fpsStd  = 0.0;
    static constexpr int    BURST_WINDOW = 30;
    static constexpr double BURST_SIGMA  = 3.0;
};
